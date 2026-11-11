#include "jobs.h"
#include "core.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace insomnia {
namespace {
constexpr int RingMax = 256 * 1024;
constexpr int ChunkMax = 4096;
QString statusOf(int code, int signal, bool killed) {
    if (killed) return "Killed";
    if (signal) return "Failed";
    return code == 0 ? "Exited" : "Failed";
}
bool pidAlive(qint64 pid) { return pid > 1 && ::kill(pid_t(pid), 0) == 0; }
QString jsonError(const QJsonParseError &e, const QString &path) {
    return e.errorString() + ": " + path;
}
}

JobManager::JobManager(QString state, QString config, QObject *parent)
    : QObject(parent), stateDir(std::move(state)), configDir(std::move(config)), journalPath(stateDir + "/jobs.json") {
    if (configDir.isEmpty()) {
        auto base = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.config";
        configDir = base + "/evereye";
    }
}

JobManager::~JobManager() {
    for (auto &job : records) {
        if (job.pid > 1 || job.masterFd >= 0) {
            requestKill(job, SIGTERM);
            requestKill(job, SIGKILL);
            if (job.pid > 1) { int status = 0; ::waitpid(pid_t(job.pid), &status, 0); job.pid = 0; }
            closeMaster(job);
            if (job.status == "Running" || job.status == "Starting" || job.status == "Killed") {
                job.status = "Interrupted";
                job.endedMs = bootMilliseconds();
            }
        }
    }
    persist();
}

int JobManager::runningCount() const {
    int n = 0;
    for (auto it = records.cbegin(); it != records.cend(); ++it)
        if (it->status == "Running" || it->status == "Starting") ++n;
    return n;
}

QJsonObject JobManager::fail(const QString &message) const { return {{"ok", false}, {"error", message}}; }

QJsonObject JobManager::snapshot(const Record &job) const {
    return {{"id", job.id}, {"name", job.name}, {"command", job.command}, {"args", QJsonArray::fromStringList(job.args)},
            {"cwd", job.cwd}, {"status", job.status}, {"pid", job.pid}, {"pgid", job.pgid},
            {"exitCode", job.exitCode}, {"termSignal", job.termSignal}, {"startedAt", job.startedMs},
            {"endedAt", job.endedMs}, {"logPath", job.logPath}, {"bytesTotal", job.bytesTotal}};
}

QJsonObject JobManager::list() const {
    QJsonArray jobs;
    for (const auto &id : order) if (records.contains(id)) jobs.append(snapshot(records[id]));
    return {{"ok", true}, {"jobs", jobs}, {"jobCount", records.size()}, {"runningJobs", runningCount()}};
}

QJsonObject JobManager::get(const QString &id) const {
    if (!records.contains(id)) return fail("Unknown job");
    auto o = snapshot(records[id]); o.insert("ok", true); return o;
}

QJsonArray JobManager::loadTasks() const {
    QFile f(configDir + "/tasks.json");
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError e; const auto d = QJsonDocument::fromJson(f.readAll(), &e);
    return e.error == QJsonParseError::NoError && d.isArray() ? d.array() : QJsonArray{};
}

QJsonObject JobManager::tasks() const { return {{"ok", true}, {"tasks", loadTasks()}, {"allowArbitraryCommands", arbitrary}}; }

QString JobManager::loadConfig() {
    QFile f(configDir + "/jobs.json");
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return f.errorString();
    QJsonParseError e; const auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) return jsonError(e, f.fileName());
    const auto o = d.object();
    if (o.contains("maxJobs")) {
        const double n = o.value("maxJobs").toDouble(-1);
        if (!o.value("maxJobs").isDouble() || n < 1 || n > 64) return "maxJobs must be 1..64";
        maxJobs = int(n);
    }
    if (o.contains("logMaxBytes")) {
        const double n = o.value("logMaxBytes").toDouble(-1);
        if (!o.value("logMaxBytes").isDouble() || n < 1024) return "logMaxBytes is too small";
        logMaxBytes = qint64(n);
    }
    if (o.contains("allowArbitraryCommands")) {
        if (!o.value("allowArbitraryCommands").isBool()) return "allowArbitraryCommands must be boolean";
        arbitrary = o.value("allowArbitraryCommands").toBool();
    }
    return {};
}

void JobManager::reconcile() {
    loadConfig();
    QDir().mkpath(stateDir);
    QFile f(journalPath);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError e; const auto d = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject() || d.object().value("schema").toInt() != 1) return;
    for (const auto &value : d.object().value("jobs").toArray()) {
        const auto o = value.toObject();
        Record job;
        job.id = o.value("id").toString();
        if (job.id.isEmpty() || records.contains(job.id)) continue;
        job.name = o.value("name").toString();
        job.command = o.value("command").toString();
        for (const auto &a : o.value("args").toArray()) job.args << a.toString();
        job.cwd = o.value("cwd").toString();
        job.logPath = o.value("logPath").toString();
        job.status = o.value("status").toString("Interrupted");
        job.pid = o.value("pid").toInteger();
        job.pgid = o.value("pgid").toInteger();
        job.exitCode = o.value("exitCode").toInt(-1);
        job.termSignal = o.value("termSignal").toInt();
        job.startedMs = o.value("startedAt").toInteger();
        job.endedMs = o.value("endedAt").toInteger();
        job.bytesTotal = o.value("bytesTotal").toInteger();
        if (job.status == "Running" || job.status == "Starting") {
            if (!pidAlive(job.pid)) {
                job.status = "Interrupted";
                job.endedMs = bootMilliseconds();
                job.pid = 0;
            } else {
                // shortcut: live PIDs are not reattached; a restart cannot reclaim the PTY.
                job.status = "Interrupted";
                job.endedMs = bootMilliseconds();
            }
        }
        records.insert(job.id, std::move(job));
        order.append(job.id);
    }
    persist();
}

QString JobManager::persist() {
    QDir().mkpath(stateDir);
    QJsonArray jobs;
    for (const auto &id : order) if (records.contains(id)) jobs.append(snapshot(records[id]));
    QSaveFile file(journalPath); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"jobs", jobs}}).toJson();
    if (file.write(bytes) != bytes.size() || !file.flush()) return "Cannot write job journal";
    if (file.handle() >= 0) ::fsync(file.handle());
    return file.commit() ? QString() : file.errorString();
}

QString JobManager::spawn(Record &job) {
    winsize size{}; size.ws_row = 50; size.ws_col = 200;
    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &size);
    if (pid < 0) return QString("Cannot allocate PTY: ") + QString::fromLocal8Bit(std::strerror(errno));
    if (pid == 0) {
        ::setsid();
        ::setpgid(0, 0);
        const long maxFd = [] {
            const long n = ::sysconf(_SC_OPEN_MAX);
            return n > 0 && n < 1024 ? n : 1024;
        }();
        for (int fd = 3; fd < int(maxFd); ++fd) ::close(fd);
        if (!job.cwd.isEmpty() && ::chdir(QFile::encodeName(job.cwd).constData()) != 0) _exit(126);
        for (const auto &item : job.env) {
            const auto encoded = QFile::encodeName(item);
            ::putenv(qstrdup(encoded.constData()));
        }
        QByteArrayList argv;
        argv << QFile::encodeName(job.command);
        for (const auto &a : job.args) argv << QFile::encodeName(a);
        std::vector<char *> ptrs;
        for (auto &a : argv) ptrs.push_back(a.data());
        ptrs.push_back(nullptr);
        ::execvp(ptrs[0], ptrs.data());
        _exit(127);
    }
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    const int flags = ::fcntl(master, F_GETFL, 0);
    if (flags >= 0) ::fcntl(master, F_SETFL, flags | O_NONBLOCK);
    job.pid = pid;
    job.pgid = pid;
    job.masterFd = master;
    job.status = "Running";
    job.startedMs = bootMilliseconds();
    auto *notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    const QString id = job.id;
    QObject::connect(notifier, &QSocketNotifier::activated, this, [this, id] {
        if (!records.contains(id)) return;
        auto &current = records[id];
        char data[ChunkMax];
        const auto n = ::read(current.masterFd, data, sizeof data);
        if (n > 0) { ingest(current, QByteArray(data, int(n))); return; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        reap(current, current.status == "Killed");
    });
    job.notifier = notifier;
    return {};
}

void JobManager::ingest(Record &job, const QByteArray &chunk) {
    if (chunk.isEmpty()) return;
    job.bytesTotal += chunk.size();
    job.ring.append(chunk);
    if (job.ring.size() > RingMax) job.ring.remove(0, job.ring.size() - RingMax);
    if (!job.logPath.isEmpty()) {
        QFile log(job.logPath);
        if (log.size() < logMaxBytes && log.open(QIODevice::WriteOnly | QIODevice::Append)) {
            log.write(chunk);
            log.close();
        }
    }
    emit output(job.id, chunk.size() > ChunkMax ? chunk.left(ChunkMax) : chunk);
}

void JobManager::closeMaster(Record &job) {
    if (job.notifier) { job.notifier->setEnabled(false); job.notifier->deleteLater(); job.notifier = nullptr; }
    if (job.killTimer) { job.killTimer->stop(); job.killTimer->deleteLater(); job.killTimer = nullptr; }
    if (job.masterFd >= 0) { ::close(job.masterFd); job.masterFd = -1; }
}

void JobManager::requestKill(Record &job, int signal) {
    if (job.pgid > 1) ::kill(pid_t(-job.pgid), signal);
    else if (job.pid > 1) ::kill(pid_t(job.pid), signal);
}

void JobManager::reap(Record &job, bool killed) {
    if (job.status != "Running" && job.status != "Starting" && job.status != "Killed") {
        closeMaster(job);
        return;
    }
    int status = 0;
    if (job.pid > 0) {
        pid_t waited = 0;
        for (int i = 0; i < 20 && waited == 0; ++i) {
            waited = ::waitpid(pid_t(job.pid), &status, WNOHANG);
            if (waited == 0) ::usleep(20000);
        }
        if (waited == 0) {
            requestKill(job, SIGKILL);
            ::waitpid(pid_t(job.pid), &status, 0);
            killed = true;
        }
    }
    if (WIFEXITED(status)) job.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) { job.termSignal = WTERMSIG(status); job.exitCode = 128 + job.termSignal; }
    job.status = statusOf(job.exitCode, job.termSignal, killed || job.status == "Killed");
    job.endedMs = bootMilliseconds();
    job.pid = 0;
    closeMaster(job);
    persist();
    emit jobChanged(job.id);
    emit jobEnded(job.id, job.exitCode);
}

QJsonObject JobManager::start(const QJsonObject &args) {
    loadConfig();
    if (runningCount() >= maxJobs) return fail("Too many running jobs");
    JobSpec spec;
    if (args.contains("preset")) {
        if (!args.value("preset").isString() || args.value("preset").toString().isEmpty()) return fail("preset must be a name");
        const auto wanted = args.value("preset").toString();
        bool found = false;
        for (const auto &value : loadTasks()) {
            const auto task = value.toObject();
            if (task.value("name").toString() != wanted) continue;
            spec.name = task.value("name").toString();
            spec.command = task.value("command").toString();
            for (const auto &a : task.value("args").toArray()) spec.args << a.toString();
            spec.cwd = task.value("cwd").toString();
            for (const auto &e : task.value("env").toArray()) spec.env << e.toString();
            found = true;
            break;
        }
        if (!found) return fail("Unknown task preset");
    } else if (args.contains("command")) {
        if (!args.value("command").isString() || args.value("command").toString().isEmpty()) return fail("command is required");
        spec.command = args.value("command").toString();
        for (const auto &a : args.value("args").toArray()) spec.args << a.toString();
        spec.cwd = args.value("cwd").toString();
        spec.name = args.value("name").toString(spec.command);
    } else return fail("Provide a preset or a command");
    if (args.contains("name") && args.value("name").isString() && !args.value("name").toString().isEmpty())
        spec.name = args.value("name").toString();
    if (spec.command.contains(QChar('\0')) || spec.name.contains(QChar('\0'))) return fail("NUL is not allowed");
    if (spec.command.isEmpty()) return fail("command is required");
    if (spec.cwd.isEmpty()) spec.cwd = QDir::homePath();
    if (!QDir(spec.cwd).exists()) return fail("Working directory does not exist");
    while (order.size() > 50) {
        const auto oldest = order.takeFirst();
        if (records.contains(oldest) && records[oldest].status != "Running" && records[oldest].status != "Starting")
            records.remove(oldest);
    }
    if (!QDir().mkpath(stateDir)) return fail("Cannot create job directory");
    Record job;
    job.id = QUuid::createUuid().toString(QUuid::Id128).left(8);
    job.name = spec.name.isEmpty() ? spec.command : spec.name;
    job.command = spec.command;
    job.args = spec.args;
    job.env = spec.env;
    job.cwd = spec.cwd;
    job.logPath = stateDir + "/" + job.id + ".log";
    QFile log(job.logPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail("Cannot create job log");
    log.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    log.close();
    const auto error = spawn(job);
    if (!error.isEmpty()) return fail(error);
    const auto id = job.id;
    records.insert(id, std::move(job));
    order.append(id);
    persist();
    emit jobChanged(id);
    auto o = snapshot(records[id]); o.insert("ok", true); return o;
}

QJsonObject JobManager::stop(const QString &id) {
    if (!records.contains(id)) return fail("Unknown job");
    auto &job = records[id];
    if (job.status != "Running" && job.status != "Starting") return fail("Job is not running");
    job.status = "Killed";
    requestKill(job, SIGTERM);
    if (!job.killTimer) {
        job.killTimer = new QTimer(this);
        job.killTimer->setSingleShot(true);
        QObject::connect(job.killTimer, &QTimer::timeout, this, [this, id] {
            if (!records.contains(id)) return;
            auto &current = records[id];
            if (current.masterFd >= 0) requestKill(current, SIGKILL);
        });
    }
    job.killTimer->start(5000);
    persist();
    emit jobChanged(id);
    auto o = snapshot(job); o.insert("ok", true); return o;
}

QJsonObject JobManager::log(const QJsonObject &args) {
    const auto id = args.value("id").toString();
    if (!records.contains(id)) return fail("Unknown job");
    auto &job = records[id];
    qint64 offset = args.value("offset").toInteger(0);
    qint64 limit = args.value("limit").toInteger(64 * 1024);
    if (limit <= 0 || limit > 256 * 1024) limit = 64 * 1024;
    QByteArray data;
    QFile file(job.logPath);
    if (file.open(QIODevice::ReadOnly)) {
        if (offset < 0) offset = 0;
        if (offset > file.size()) offset = file.size();
        file.seek(offset);
        data = file.read(limit);
    } else {
        offset = qMax(0LL, qint64(job.ring.size()) - limit);
        data = job.ring.mid(int(offset), int(limit));
    }
    const bool running = job.status == "Running" || job.status == "Starting";
    return {{"ok", true}, {"id", id}, {"offset", offset}, {"nextOffset", offset + data.size()},
            {"eof", !running && data.size() < limit}, {"data", QString::fromUtf8(data)}, {"status", job.status}};
}
}
