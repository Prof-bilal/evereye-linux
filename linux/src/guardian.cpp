#include "guardian.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace insomnia {
static QString pidfdAvailabilityError() {
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const int fd = int(::syscall(SYS_pidfd_open, getpid(), 0));
    if (fd < 0) return "Safe session supervision requires pidfd support (Linux 5.3+ or a compatible backport): " + QString::fromLocal8Bit(std::strerror(errno));
    const int result = int(::syscall(SYS_pidfd_send_signal, fd, 0, nullptr, 0));
    const int saved = errno; ::close(fd);
    if (result != 0) return "Session supervisor signaling is blocked by this environment: " + QString::fromLocal8Bit(std::strerror(saved));
    return {};
#else
    return "This build's Linux headers do not expose pidfd supervision";
#endif
}
static QString createKernelTimer(timer_t &timer) {
    sigevent event{}; event.sigev_notify = SIGEV_SIGNAL; event.sigev_signo = SIGKILL;
    if (::timer_create(CLOCK_BOOTTIME, &event, &timer) != 0)
        return "Kernel deadline timer unavailable: " + QString::fromLocal8Bit(std::strerror(errno));
    return {};
}
static QString kernelTimerAvailabilityError() {
    timer_t timer{}; const auto error = createKernelTimer(timer); if (!error.isEmpty()) return error;
    if (::timer_delete(timer) != 0) return "Cannot safely release a kernel deadline timer";
    return {};
}
QString guardianBackend() {
    const auto requested = qEnvironmentVariable("EVEREYE_SUPERVISOR");
    if (requested != "kernel-timer" && pidfdAvailabilityError().isEmpty()) return "pidfd-process";
    if (requested != "pidfd" && kernelTimerAvailabilityError().isEmpty()) return "kernel-timer";
    return "unavailable";
}
QString guardianAvailabilityError() {
    if (guardianBackend() != "unavailable") return {};
    return pidfdAvailabilityError() + "; " + kernelTimerAvailabilityError();
}
ProcessGuardian::ProcessGuardian(bool kernel) : forceKernel(kernel || qEnvironmentVariable("EVEREYE_SUPERVISOR") == "kernel-timer") {}
ProcessGuardian::~ProcessGuardian() { stop(); }
QString ProcessGuardian::arm(qint64 deadlineMs) {
    stop();
    const auto pidfdError = forceKernel ? QString("Kernel backend selected") : pidfdAvailabilityError();
    if (!pidfdError.isEmpty() && (forceKernel || qEnvironmentVariable("EVEREYE_SUPERVISOR") != "pidfd")) {
        timer_t timer{}; const auto error = createKernelTimer(timer);
        if (!error.isEmpty()) return pidfdError + "; " + error;
        kernelTimer = timer;
        const auto armed = heartbeat(deadlineMs);
        if (!armed.isEmpty()) stop();
        return armed;
    }
    if (!pidfdError.isEmpty()) return pidfdError;
    process.setProgram(QCoreApplication::applicationFilePath()); process.setArguments({"--watchdog"});
    process.setProcessChannelMode(QProcess::SeparateChannels); process.start();
    if (!process.waitForStarted(1500)) return "Cannot launch deadline supervisor: " + process.errorString();
    return heartbeat(deadlineMs);
}
QString ProcessGuardian::heartbeat(qint64 deadlineMs) {
    if (kernelTimer) {
        const auto now = bootMilliseconds();
        if (deadlineMs <= now || deadlineMs > now + MaxSeconds * 1000) return "Kernel supervisor rejected the deadline";
        const auto until = qMin(deadlineMs + 2000, now + 10000);
        itimerspec expiry{}; expiry.it_value.tv_sec = until / 1000; expiry.it_value.tv_nsec = (until % 1000) * 1000000;
        if (::timer_settime(*kernelTimer, TIMER_ABSTIME, &expiry, nullptr) != 0)
            return "Cannot update kernel session supervisor: " + QString::fromLocal8Bit(std::strerror(errno));
        return {};
    }
    if (process.state() != QProcess::Running) return "Deadline supervisor is not running";
    const auto command = QByteArray::number(deadlineMs) + '\n';
    if (process.write(command) != command.size() || !process.waitForBytesWritten(1000)) return "Cannot update deadline supervisor";
    if (!process.canReadLine() && !process.waitForReadyRead(1000)) return "Deadline supervisor did not acknowledge the deadline";
    if (process.readLine().trimmed() != "OK") return "Deadline supervisor rejected the deadline";
    return {};
}
void ProcessGuardian::stop() {
    if (kernelTimer) {
        itimerspec disarmed{};
        ::timer_settime(*kernelTimer, 0, &disarmed, nullptr);
        ::timer_delete(*kernelTimer); kernelTimer.reset();
    }
    if (process.state() == QProcess::NotRunning) return;
    process.closeWriteChannel();
    if (!process.waitForFinished(1000)) { process.terminate(); if (!process.waitForFinished(500)) { process.kill(); process.waitForFinished(500); } }
}
int runGuardian() {
#if !defined(SYS_pidfd_open) || !defined(SYS_pidfd_send_signal)
    return 1; // Older headers use the in-process kernel timer backend instead.
#else
    const pid_t parent = getppid();
    if (parent <= 1) return 1;
    struct stat inputInfo{}, outputInfo{};
    if (::fstat(STDIN_FILENO, &inputInfo) != 0 || ::fstat(STDOUT_FILENO, &outputInfo) != 0 ||
        !S_ISFIFO(inputInfo.st_mode) || !S_ISFIFO(outputInfo.st_mode)) return 1;
    // This private entry point may supervise only another copy of this executable.
    // An accidental invocation from a terminal must never target the user's shell.
    auto parentExecutable = QFileInfo(QString("/proc/%1/exe").arg(parent)).symLinkTarget();
    if (parentExecutable.endsWith(" (deleted)")) parentExecutable.chop(10); // Atomic upgrade of a still-running controller.
    if (parentExecutable != QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()) return 1;
    const int pidfd = int(syscall(SYS_pidfd_open, parent, 0));
    if (pidfd < 0) return 1; // Refuse operation without race-free parent targeting.
    if (getppid() != parent) { ::close(pidfd); return 1; }
    ::fcntl(pidfd, F_SETFD, FD_CLOEXEC);
    ::fcntl(STDIN_FILENO, F_SETFL, ::fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    qint64 deadline = 0; qint64 lastBeat = bootMilliseconds(); QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QSocketNotifier parentExit(pidfd, QSocketNotifier::Read);
    QObject::connect(&parentExit, &QSocketNotifier::activated, [] { QCoreApplication::quit(); });
    QObject::connect(&input, &QSocketNotifier::activated, [&] {
        char data[128]; const auto n = ::read(STDIN_FILENO, data, sizeof data);
        if (n == 0) { QCoreApplication::quit(); return; }
        if (n < 0) return;
        buffer.append(data, int(n));
        if (buffer.size() > 1024) { QCoreApplication::exit(1); return; }
        while (buffer.contains('\n')) {
            const auto line = buffer.first(buffer.indexOf('\n')); buffer.remove(0, buffer.indexOf('\n') + 1);
            bool ok = false; const auto value = line.toLongLong(&ok); const auto now = bootMilliseconds();
            if (!ok || value <= now || value > now + MaxSeconds * 1000) {
                if (deadline) syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0);
                QCoreApplication::exit(1); return;
            }
            deadline = value; lastBeat = now;
            if (::write(STDOUT_FILENO, "OK\n", 3) != 3) QCoreApplication::exit(1);
        }
    });
    QTimer timer; timer.setInterval(250);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        const auto now = bootMilliseconds();
        // Allow the normal one-second controller tick to finish journal/lease cleanup.
        if ((deadline && now >= deadline + 2000) || now - lastBeat > 10000) {
            // SIGKILL closes every descriptor even if the controller's event loop is hung.
            if (deadline) syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0);
            QCoreApplication::quit();
        }
    });
    timer.start(); const int result = QCoreApplication::exec(); ::close(pidfd); return result;
#endif
}
}
