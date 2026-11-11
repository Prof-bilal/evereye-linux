#include "core.h"
#include "gateway.h"
#include "guardian.h"
#include "platform.h"
#include "service.h"
#include "window.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <QStyleFactory>
#include <memory>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace insomnia;
static void output(const QJsonObject &o) { QTextStream(stdout) << QJsonDocument(o).toJson(QJsonDocument::Indented); }
// Replace only the registered UI, never the controller or its active session.
static bool replaceUi(QDBusConnection &bus) {
    const auto name = QString(Service) + ".UI";
    auto *interface = bus.interface();
    if (!interface) return false;
    const auto owner = interface->serviceOwner(name);
    if (!owner.isValid()) return true;
    const auto uid = interface->serviceUid(owner.value());
    if (!uid.isValid() || uid.value() != getuid()) return false;
    // A namespaced UI (Flatpak) may not be addressable by its host PID. Ask its
    // pinned unique bus owner to quit before considering the legacy fallback.
    const auto quit = bus.call(QDBusMessage::createMethodCall(owner.value(), "/Window", "io.github.ProfBilal.Insomnia.Window", "QuitWindow"), QDBus::Block, 1500);
    if (quit.type() != QDBusMessage::ErrorMessage) {
        for (int i = 0; i < 200; ++i) {
            if (interface->serviceOwner(name).value() != owner.value()) return true;
            QThread::msleep(20);
        }
        return false;
    }
    const auto pid = interface->servicePid(owner.value());
    if (!uid.isValid() || uid.value() != getuid() || !pid.isValid() || pid.value() <= 1) return false;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const int fd = int(::syscall(SYS_pidfd_open, pid.value(), 0));
    if (fd < 0) return false;
    QString executable = QFileInfo(QString("/proc/%1/exe").arg(pid.value())).symLinkTarget();
    if (executable.endsWith(" (deleted)")) executable.chop(10);
    const bool verified = (QFileInfo(executable).fileName() == "insomnia-linux" || QFileInfo(executable).fileName() == "evereye") && interface->serviceOwner(name).value() == owner.value();
    const bool stopped = verified && ::syscall(SYS_pidfd_send_signal, fd, SIGTERM, nullptr, 0) == 0;
    ::close(fd);
    if (!stopped) return false;
    for (int i = 0; i < 100; ++i) {
        if (!interface->isServiceRegistered(name).value()) return true;
        QThread::msleep(20);
    }
#endif
    return false;
}
static int cli(const QStringList &args) {
    if (args.isEmpty() || args.first() == "--help" || args.first() == "help") {
        QTextStream(stdout) << "EverEye 0.4.0\n\nUsage:\n  evereye                         Open controls and tray\n  evereye start 2h30m [--no-lid] [--no-power-saver]\n      [--battery-cutoff 10] [--power-saver-below 40]\n  evereye extend 15m\n  evereye status | end | shutdown | doctor\n  evereye run <preset> [--name N]\n  evereye run -- <command> [args...]\n  evereye jobs | jobs stop <id> | jobs log <id> [-f] [--offset N]\n  evereye tasks\n  evereye gateway [--bind 127.0.0.1] [--port 8737]\n  evereye --daemon                Run the controller in foreground\n  evereye --replace-ui            Reopen with this build; keep session\n  evereye --demo                  Preview without power changes\n  evereye --screenshot FILE.png   Render an offline UI preview\n\nDurations: seconds or d/h/m/s units; minimum 60s, maximum 30d.\n"; return 0;
    }
    if (args.first() == "--version") { QTextStream(stdout) << "EverEye 0.4.0\n"; return 0; }
    const auto action = args.first();
    if (action == "--daemon" && args.size() == 1) return runService();
    if (action == "--watchdog" && args.size() == 1) return runGuardian();
    if (action == "gateway") return runGateway(args.mid(1));
    if (action == "tasks" && args.size() == 1) {
        const auto result = callController("tasks.list"); output(result); return result.value("ok").toBool() ? 0 : 1;
    }
    if (action == "run") {
        QJsonObject params;
        int i = 1;
        if (i < args.size() && args[i] == "--") {
            if (args.size() < 3) { output({{"ok", false}, {"error", "A command is required"}}); return 2; }
            params.insert("command", args[2]);
            QJsonArray argv;
            for (int n = 3; n < args.size(); ++n) argv.append(args[n]);
            params.insert("args", argv);
        } else {
            if (i >= args.size() || args[i].startsWith("-")) { output({{"ok", false}, {"error", "A preset name is required"}}); return 2; }
            params.insert("preset", args[i++]);
            if (i < args.size() && args[i] == "--name" && i + 1 < args.size()) { params.insert("name", args[++i]); ++i; }
            else if (i < args.size()) { output({{"ok", false}, {"error", "Unknown or incomplete option: " + args[i]}}); return 2; }
        }
        const auto result = callController("jobs.start", params); output(result); return result.value("ok").toBool() ? 0 : 1;
    }
    if (action == "jobs") {
        if (args.size() == 1) { const auto result = callController("jobs.list"); output(result); return result.value("ok").toBool() ? 0 : 1; }
        if (args.size() >= 3 && args[1] == "stop") {
            const auto result = callController("jobs.stop", {{"id", args[2]}}); output(result); return result.value("ok").toBool() ? 0 : 1;
        }
        if (args.size() >= 3 && args[1] == "log") {
            QJsonObject params{{"id", args[2]}};
            bool follow = false;
            for (int i = 3; i < args.size(); ++i) {
                if (args[i] == "-f") follow = true;
                else if (args[i] == "--offset" && i + 1 < args.size()) params.insert("offset", args[++i].toLongLong());
                else { output({{"ok", false}, {"error", "Unknown jobs log option"}}); return 2; }
            }
            qint64 offset = params.value("offset").toInteger(0);
            do {
                params.insert("offset", offset);
                const auto result = callController("jobs.log", params);
                if (!result.value("ok").toBool()) { output(result); return 1; }
                const auto data = result.value("data").toString();
                if (follow && !data.isEmpty()) { QTextStream stream(stdout); stream << data << Qt::flush; }
                offset = result.value("nextOffset").toInteger();
                if (!follow) { output(result); return 0; }
                if (result.value("eof").toBool()) return 0;
                QThread::msleep(300);
            } while (true);
        }
        output({{"ok", false}, {"error", "Unknown jobs command; use jobs, jobs stop <id>, or jobs log <id>"}}); return 2;
    }
    if (action == "doctor" && args.size() == 1) {
        LinuxPlatform p; auto o = p.capabilities(); o.insert("stateDirectory", stateDirectory()); o.insert("ok", o.value("systemBus").toBool() && o.value("logind").toBool() && o.value("unixFdPassing").toBool() && o.value("supervisor").toBool()); output(o); return o.value("ok").toBool() ? 0 : 1;
    }
    QJsonObject params;
    if (action == "start" || action == "extend") {
        if (args.size() < 2) { output({{"ok", false}, {"error", "A duration is required"}}); return 2; }
        const auto seconds = parseDuration(args[1]);
        if (seconds < MinSeconds || seconds > MaxSeconds) { output({{"ok", false}, {"error", "Duration must be 1 minute to 30 days"}}); return 2; }
        params.insert("seconds", seconds);
        for (int i = 2; i < args.size(); ++i) {
            if (action == "extend") { output({{"ok", false}, {"error", "Extend accepts only a duration"}}); return 2; }
            const auto arg = args[i];
            if (arg == "--lid") params.insert("lid", true);
            else if (arg == "--no-lid") params.insert("lid", false);
            else if (arg == "--no-power-saver") params.insert("powerSaver", false);
            else if ((arg == "--battery-cutoff" || arg == "--power-saver-below") && i + 1 < args.size()) {
                bool valid; const auto n = args[++i].toInt(&valid);
                if (!valid) { output({{"ok", false}, {"error", "Battery thresholds must be integers"}}); return 2; }
                params.insert(arg == "--battery-cutoff" ? "endFloor" : "lowPowerFloor", n);
            } else { output({{"ok", false}, {"error", "Unknown or incomplete option: " + arg}}); return 2; }
        }
    } else if (!QStringList{"status", "end", "shutdown"}.contains(action) || args.size() != 1) { output({{"ok", false}, {"error", "Unknown command; use --help"}}); return 2; }
    const auto result = callController(action, params, action != "shutdown"); output(result); return result.value("ok").toBool() ? 0 : 1;
}
int main(int argc, char **argv) {
    QStringList args; for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    const bool preview = args.contains("--demo") || args.contains("--screenshot");
    const bool replace = args == QStringList{"--replace-ui"};
    if (!args.isEmpty() && !preview && !replace) { QCoreApplication app(argc, argv); app.setApplicationName("evereye"); return cli(args); }
    if (preview && !(args == QStringList{"--demo"} || (args.size() == 2 && args.first() == "--screenshot"))) {
        QTextStream(stderr) << "Use --demo or --screenshot FILE.png\n"; return 2;
    }
    QApplication app(argc, argv); app.setApplicationName("EverEye"); app.setDesktopFileName("io.github.ProfBilal.EverEye");
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setApplicationVersion("0.4.0"); app.setQuitOnLastWindowClosed(true);
    std::unique_ptr<Window> window;
    if (preview) {
        struct Preview { bool active = false; qint64 deadline = 0; Options options; };
        auto model = std::make_shared<Preview>();
        if (args.contains("--screenshot")) { model->active = true; model->deadline = bootMilliseconds() + 9258000; }
        window = std::make_unique<Window>([model](const QString &action, const QJsonObject &p) {
            if (action == "start") { QString error; if (!Options::parse(p, model->options, error)) return QJsonObject{{"ok", false}, {"error", error}}; model->active = true; model->deadline = bootMilliseconds() + p.value("seconds").toInteger() * 1000; }
            if (action == "extend" && model->active) model->deadline += p.value("seconds").toInteger() * 1000;
            if (action == "end") model->active = false;
            const auto remaining = (qMax(0LL, model->deadline - bootMilliseconds()) + 999) / 1000;
            if (remaining == 0) model->active = false;
            return QJsonObject{{"ok", true}, {"connected", true}, {"active", model->active}, {"remainingSeconds", model->active ? remaining : 0}, {"options", model->options.json()}, {"batteryKnown", true}, {"batteryPresent", true}, {"batteryPercent", 82}, {"acKnown", true}, {"onBattery", false}, {"reason", "Preview only. No sleep or power settings are changed."}};
        }, true);
    } else {
        auto bus = QDBusConnection::sessionBus();
        if (replace && (!bus.isConnected() || !replaceUi(bus))) {
            QTextStream(stderr) << "Could not replace the existing window. Close its UI process and try again.\n"; return 1;
        }
        if (bus.isConnected() && !bus.registerService(QString(Service) + ".UI")) {
            bus.call(QDBusMessage::createMethodCall(QString(Service) + ".UI", "/Window", "io.github.ProfBilal.Insomnia.Window", "Raise"), QDBus::NoBlock); return 0;
        }
        window = std::make_unique<Window>([](const QString &action, const QJsonObject &p) { return callController(action == "connect" ? "status" : action, p, action == "connect" || action == "status"); }, false, true);
        if (bus.isConnected()) bus.registerObject("/Window", "io.github.ProfBilal.Insomnia.Window", window.get(), QDBusConnection::ExportScriptableSlots);
    }
    if (preview) {
        window->show();
    } else {
        // The tray popup is the primary EverEye workflow. The full dashboard
        // remains available through Settings and Add time in the popup.
        window->OpenQuickPanel();
    }
    if (!args.isEmpty() && args.first() == "--screenshot") {
        const auto path = args[1]; QTimer::singleShot(200, [&] { const bool ok = window->grab().save(path); app.exit(ok ? 0 : 1); });
    }
    return app.exec();
}
