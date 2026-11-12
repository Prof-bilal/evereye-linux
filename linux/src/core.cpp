#include "core.h"
#include "agents.h"
#include "jobs.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

namespace insomnia {
qint64 bootMilliseconds() {
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) qFatal("CLOCK_BOOTTIME unavailable");
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
QString bootId() {
    QFile f("/proc/sys/kernel/random/boot_id");
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
qint64 parseDuration(const QString &text) {
    const auto value = text.trimmed().toLower();
    static const QRegularExpression whole("^[0-9]+$");
    if (whole.match(value).hasMatch()) {
        bool ok = false; auto n = value.toLongLong(&ok); return ok && n <= MaxSeconds ? n : -1;
    }
    static const QRegularExpression part("([0-9]+)([dhms])");
    auto it = part.globalMatch(value); qint64 seconds = 0; int position = 0; QSet<QString> units;
    while (it.hasNext()) {
        auto m = it.next();
        if (m.capturedStart() != position || units.contains(m.captured(2))) return -1;
        units.insert(m.captured(2)); position = m.capturedEnd();
        bool ok = false; qint64 n = m.captured(1).toLongLong(&ok);
        qint64 multiplier = m.captured(2) == "d" ? 86400 : m.captured(2) == "h" ? 3600 : m.captured(2) == "m" ? 60 : 1;
        if (!ok || n > MaxSeconds / multiplier) return -1;
        seconds += n * multiplier;
        if (seconds > MaxSeconds) return -1;
    }
    return position == value.size() && position > 0 ? seconds : -1;
}
QString formatDuration(qint64 seconds) {
    seconds = qMax(0LL, seconds);
    const auto hours = seconds / 3600;
    return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg((seconds / 60) % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'));
}
QJsonObject Options::json() const {
    return {{"lid", lid}, {"powerSaver", powerSaver}, {"endFloor", endFloor}, {"lowPowerFloor", lowPowerFloor}};
}
bool Options::parse(const QJsonObject &o, Options &out, QString &error) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key() == "seconds") continue;
        if (it.key() == "lid" || it.key() == "powerSaver") {
            if (!it.value().isBool()) { error = it.key() + " must be boolean"; return false; }
            if (it.key() == "lid") out.lid = it.value().toBool(); else out.powerSaver = it.value().toBool();
        } else if (it.key() == "endFloor" || it.key() == "lowPowerFloor") {
            const double n = it.value().toDouble(-1);
            if (!it.value().isDouble() || !std::isfinite(n) || std::floor(n) != n || n < 0 || n > 100) {
                error = it.key() + " must be an integer from 0 to 100"; return false;
            }
            if (it.key() == "endFloor") out.endFloor = int(n); else out.lowPowerFloor = int(n);
        } else { error = "Unknown option: " + it.key(); return false; }
    }
    if (out.endFloor > 0 && out.endFloor >= out.lowPowerFloor) {
        error = "Battery cutoff must be below the power-saver threshold (or 0 to disable)"; return false;
    }
    return true;
}
DiskJournal::DiskJournal(QString directory) : directory(std::move(directory)) {}
DiskJournal::~DiskJournal() { if (lockFd >= 0) ::close(lockFd); }
QString DiskJournal::path() const { return directory + "/session.json"; }
QString DiskJournal::ensureLock() {
    if (lockFd >= 0) return {};
    const QFileInfo info(directory);
    if (info.isSymLink() || (info.exists() && (!info.isDir() || info.ownerId() != getuid()))) return "Invalid private state directory";
    if (!QDir().mkpath(directory) || ::chmod(QFile::encodeName(directory).constData(), 0700) != 0) return "Cannot create/protect private state directory";
    const auto lockPath = QFile::encodeName(directory + "/controller.lock");
    const int fd = ::open(lockPath.constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return "Cannot open controller journal lock";
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_nlink != 1 || ::fchmod(fd, 0600) != 0 || ::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd); return "Journal lock is unsafe or held by another controller";
    }
    lockFd = fd; return {};
}
QJsonObject DiskJournal::read(QString &error) {
    error = ensureLock(); if (!error.isEmpty()) return {};
    QFileInfo info(path());
    if (info.isSymLink()) { error = "Session journal must not be a symlink"; return {}; }
    if (!info.exists()) return {};
    if (!info.isFile() || info.size() > 65536 || info.ownerId() != getuid()) { error = "Invalid session journal file"; return {}; }
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return {}; }
    QJsonParseError e; auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject() || d.object().value("schema").toInt() != 1 || !d.object().value("active").isBool()) {
        error = "Unreadable or unsupported session journal; preserve it for inspection: " + path(); return {};
    }
    return d.object();
}
QString DiskJournal::write(const QJsonObject &o) {
    const auto lockError = ensureLock(); if (!lockError.isEmpty()) return lockError;
    const QFileInfo dirInfo(directory);
    if (dirInfo.isSymLink() || (dirInfo.exists() && (!dirInfo.isDir() || dirInfo.ownerId() != getuid()))) return "Invalid private state directory";
    if (!QDir().mkpath(directory)) return "Cannot create private state directory";
    if (::chmod(QFile::encodeName(directory).constData(), 0700) != 0) return "Cannot protect private state directory";
    const QFileInfo target(path());
    if (target.isSymLink() || (target.exists() && (!target.isFile() || target.ownerId() != getuid()))) return "Invalid session journal target";
    QSaveFile file(path()); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return "Cannot protect session journal";
    const auto bytes = QJsonDocument(o).toJson();
    if (file.write(bytes) != bytes.size() || !file.flush() || ::fsync(file.handle()) != 0) return "Cannot durably write session journal";
    if (!file.commit()) return file.errorString();
    const int fd = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return "Cannot open state directory for synchronization";
    const int result = ::fsync(fd); ::close(fd);
    return result == 0 ? QString() : QString("Cannot synchronize state directory");
}
Controller::Controller(Platform &p, Guardian &g, Journal &j, std::function<qint64()> c, QObject *parent, JobManager *managed)
    : QObject(parent), platform(p), guardian(g), journal(j), clock(std::move(c)), jobs(managed) {
    if (jobs) {
        QObject::connect(jobs, &JobManager::jobChanged, this, &Controller::changed);
        QObject::connect(jobs, &JobManager::output, this, &Controller::jobOutput);
    }
}
Controller::~Controller() { if (running) end("Controller stopped"); platform.releaseInhibitor(); guardian.stop(); }
QJsonObject Controller::record(bool active, qint64 until, const QString &reason) const {
    auto o = options.json();
    o.insert("schema", 1); o.insert("active", active); o.insert("bootId", bootId());
    o.insert("deadlineBootMs", until);
    o.insert("endsAt", QDateTime::currentDateTimeUtc().addMSecs(qMax(0LL, until - clock())).toString(Qt::ISODate));
    o.insert("reason", reason); return o;
}
void Controller::reconcile() {
    QString error; auto prior = journal.read(error);
    if (!error.isEmpty()) { recoveryBlocked = true; lastError = error; return; }
    if (!prior.isEmpty()) {
        Options restored; QString parseError;
        QJsonObject settings;
        for (const auto &key : {"lid", "powerSaver", "endFloor", "lowPowerFloor"}) if (prior.contains(key)) settings.insert(key, prior.value(key));
        if (!Options::parse(settings, restored, parseError)) { recoveryBlocked = true; lastError = parseError; return; }
        options = restored;
        if (prior.value("active").toBool()) {
            lastReason = "Previous session interrupted; sleep inhibition was released. Start a new session to continue.";
            error = journal.write(record(false, 0, lastReason));
            if (!error.isEmpty()) { recoveryBlocked = true; lastError = error; }
        } else lastReason = prior.value("reason").toString("Ready");
    }
}
QJsonObject Controller::status() const {
    QJsonArray a; if (!profileWarning.isEmpty()) a.append(profileWarning);
    return {{"active", running}, {"remainingSeconds", running ? (qMax(0LL, deadline - clock()) + 999) / 1000 : 0},
            {"options", options.json()}, {"reason", lastReason}, {"error", lastError}, {"warnings", a},
            {"powerSaverHeld", platform.powerSaverHeld()}, {"batteryPresent", latest.battery == Power::Present},
            {"batteryKnown", latest.battery != Power::Unknown}, {"batteryPercent", latest.percent},
            {"acKnown", latest.acKnown}, {"onBattery", latest.onBattery},
            {"lidKnown", latest.lidKnown}, {"lidClosed", latest.lidClosed}, {"recoveryBlocked", recoveryBlocked},
            {"jobCount", jobs ? jobs->jobCount() : 0}, {"runningJobs", jobs ? jobs->runningCount() : 0}};
}
QJsonObject Controller::ok() const { auto o = status(); o.insert("ok", true); return o; }
QJsonObject Controller::fail(const QString &message) { lastError = message; emit changed(); auto o = status(); o.insert("ok", false); return o; }
QJsonObject Controller::request(const QString &action, const QJsonObject &args) {
    if (action == "status") return ok();
    if (action == "doctor") { auto o = ok(); o.insert("capabilities", platform.capabilities()); return o; }
    if (action == "start") return start(args);
    if (action == "extend") return extend(args);
    if (action == "end") return end("Ended by user");
    if (action == "agents.list") return scanAgents();
    if (action == "agents.capture") return captureAgent(args);
    if (action == "agents.send") return sendAgent(args);
    if (!jobs) return fail("Unknown action: " + action);
    if (action == "jobs.list") { auto o = jobs->list(); o.insert("connected", true); return o; }
    if (action == "jobs.get") return jobs->get(args.value("id").toString());
    if (action == "jobs.start") return jobs->start(args);
    if (action == "jobs.stop") return jobs->stop(args.value("id").toString());
    if (action == "jobs.input") return jobs->input(args);
    if (action == "jobs.log") return jobs->log(args);
    if (action == "tasks.list") return jobs->tasks();
    return fail("Unknown action: " + action);
}
static qint64 secondsFrom(const QJsonObject &args) {
    const auto v = args.value("seconds"); const double n = v.toDouble(-1);
    return v.isDouble() && std::isfinite(n) && std::floor(n) == n && n >= MinSeconds && n <= MaxSeconds ? qint64(n) : -1;
}
QJsonObject Controller::start(const QJsonObject &args) {
    if (recoveryBlocked) return fail("Journal needs attention: " + lastError);
    if (running) return fail("A session is already running; extend or end it first");
    const qint64 seconds = secondsFrom(args);
    if (seconds < 0) return fail("Duration must be between 60 seconds and 30 days");
    Options requested = options; QString error;
    if (!Options::parse(args, requested, error)) return fail(error);
    latest = platform.power();
    if (requested.endFloor > 0 && (latest.battery == Power::Unknown ||
        (latest.battery == Power::Present && (!latest.acKnown || (latest.onBattery && latest.percent < 0)))))
        return fail("Battery protection unavailable: " + latest.error + ". Check UPower, or explicitly set battery cutoff to 0.");
    if (latest.battery == Power::Present && latest.onBattery && latest.percent >= 0 && latest.percent < requested.endFloor)
        return fail("Battery is below the configured cutoff");
    options = requested; profileWarning.clear(); lastError.clear();
    const auto proposed = clock() + seconds * 1000;
    error = journal.write(record(true, proposed, "Starting"));
    if (!error.isEmpty()) return fail(error);
    error = guardian.arm(proposed);
    if (error.isEmpty()) error = platform.inhibit(options.lid);
    if (!error.isEmpty()) {
        platform.releaseInhibitor(); guardian.stop();
        const auto undoError = journal.write(record(false, 0, "Start failed"));
        if (!undoError.isEmpty()) { recoveryBlocked = true; error += "; journal: " + undoError; }
        return fail(error);
    }
    running = true; deadline = proposed; unreadableCount = 0; saverWanted = false; saverSuppressed = false;
    fiveMinuteNotice = seconds <= 300; lastReason = "Session running";
    pollPower(); emit changed();
    if (running) platform.notify("Session started", "Awake for " + formatDuration(seconds));
    return running ? ok() : fail(lastReason);
}
QJsonObject Controller::extend(const QJsonObject &args) {
    if (!running) return fail("No session is running");
    if (clock() >= deadline) { end("Timer expired"); return fail("The session has expired; start a new session"); }
    if (args.size() != 1 || !args.contains("seconds")) return fail("Extend accepts only seconds");
    const auto seconds = secondsFrom(args); if (seconds < 0) return fail("Extension must be between 60 seconds and 30 days");
    const qint64 proposed = qMin(deadline + seconds * 1000, clock() + MaxSeconds * 1000);
    auto error = journal.write(record(true, proposed, "Session extended"));
    if (!error.isEmpty()) return fail(error);
    error = guardian.heartbeat(proposed);
    if (!error.isEmpty()) { end("Deadline supervisor unavailable"); return fail(error); }
    deadline = proposed; fiveMinuteNotice = deadline - clock() <= 300000; lastError.clear(); emit changed(); return ok();
}
QJsonObject Controller::end(const QString &reason) {
    // Never retain inhibition because persistence or optional restoration failed.
    const bool wasRunning = running; running = false; deadline = 0;
    platform.releaseInhibitor(); guardian.stop();
    const auto saverError = platform.releasePowerSaver();
    const auto error = recoveryBlocked ? QString("Journal preserved for inspection") : journal.write(record(false, 0, reason));
    saverWanted = false; saverSuppressed = false;
    lastReason = reason;
    profileWarning = saverError;
    lastError = error;
    if (!error.isEmpty()) recoveryBlocked = true;
    if (wasRunning) platform.notify("Session ended", reason);
    emit changed(); return error.isEmpty() ? ok() : fail(error);
}
void Controller::tick() {
    if (!running) return;
    if (clock() >= deadline) { end("Timer expired"); return; }
    if (!platform.inhibitorValid()) { end("Sleep inhibition was lost; session ended"); return; }
    const auto error = guardian.heartbeat(deadline);
    if (!error.isEmpty()) { end("Deadline supervisor unavailable"); lastError = error; emit changed(); return; }
    if (!fiveMinuteNotice && deadline - clock() <= 300000) {
        fiveMinuteNotice = true; platform.notify("Five minutes remaining", "Extend the session if your work needs more time.");
    }
    emit changed();
}
void Controller::pollPower() {
    latest = platform.power();
    if (!running) {
        if (platform.powerSaverHeld()) {
            profileWarning = platform.releasePowerSaver();
        }
        return;
    }
    const bool unreadable = latest.battery == Power::Unknown ||
        (latest.battery == Power::Present && (!latest.acKnown || (latest.onBattery && latest.percent < 0)));
    unreadableCount = unreadable ? unreadableCount + 1 : 0;
    if (options.endFloor > 0 && unreadableCount >= 2) { end("Battery protection unavailable on two consecutive reads"); return; }
    if (options.endFloor > 0 && latest.battery == Power::Present && latest.acKnown && latest.onBattery && latest.percent >= 0 && latest.percent < options.endFloor) {
        end("Battery reached the configured cutoff"); return;
    }
    const bool want = options.powerSaver && ((latest.battery == Power::Present && latest.acKnown && latest.onBattery && latest.percent >= 0 && latest.percent < options.lowPowerFloor) ||
        (options.lid && latest.lidKnown && latest.lidClosed));
    if (saverWanted && !platform.powerSaverHeld()) saverSuppressed = true;
    if (!want) {
        profileWarning = platform.powerSaverHeld() ? platform.releasePowerSaver() : QString();
        saverWanted = false; saverSuppressed = false;
    } else if (!saverWanted && !saverSuppressed) {
        const auto error = platform.holdPowerSaver(); profileWarning = error;
        if (error.isEmpty()) saverWanted = true;
        else saverSuppressed = true;
    }
    emit changed();
}
}
