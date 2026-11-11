#include "service.h"
#include "guardian.h"
#include "jobs.h"
#include "platform.h"
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace insomnia {
ServiceObject::ServiceObject(Controller &c, QObject *parent) : QObject(parent), controller(c) {
    connect(&controller, &Controller::changed, this, &ServiceObject::Changed);
    connect(&controller, &Controller::changed, this, &ServiceObject::JobsChanged);
    connect(&controller, &Controller::jobOutput, this, [this](const QString &id, const QByteArray &chunk) {
        emit JobOutput(id, chunk.size() > 4096 ? chunk.left(4096) : chunk);
    });
}
QString ServiceObject::Request(const QString &action, const QString &json) {
    const auto encode = [](const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); };
    if (!calledFromDBus()) return encode({{"ok", false}, {"error", "D-Bus request required"}});
    const QDBusReply<uint> uid = connection().interface()->serviceUid(message().service());
    if (!uid.isValid() || uid.value() != getuid()) return encode({{"ok", false}, {"error", "Caller must own this user session"}});
    if (json.size() > 8192 || action.size() > 32) return encode({{"ok", false}, {"error", "Request exceeds size limit"}});
    QJsonParseError e; auto d = QJsonDocument::fromJson(json.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) return encode({{"ok", false}, {"error", "Arguments must be a JSON object"}});
    if (action == "shutdown") {
        auto result = controller.request("end");
        // Exit closes the system-bus connection, including any failed profile release.
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
        return encode(result);
    }
    return encode(controller.request(action, d.object()));
}
QString stateDirectory() {
    auto base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.local/state";
    return base + "/insomnia-linux";
}
QString jobsDirectory() { return stateDirectory() + "/jobs"; }
QString controllerExecutable() {
    const auto executable = QCoreApplication::applicationFilePath();
    const QFileInfo image(qEnvironmentVariable("APPIMAGE"));
    const auto appdir = QFileInfo(qEnvironmentVariable("APPDIR")).canonicalFilePath();
    // A detached controller needs its own AppImage runtime/mount. Its watchdog
    // remains a normal child of the controller and shares that live mount.
    if (!appdir.isEmpty() && QFileInfo(executable).canonicalFilePath().startsWith(appdir + '/') && image.isFile() && image.isExecutable()) return image.absoluteFilePath();
    return executable;
}
QJsonObject callController(const QString &action, const QJsonObject &args, bool launch) {
    const auto error = [](const QString &s) { return QJsonObject{{"ok", false}, {"error", s}, {"connected", false}}; };
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return error("Session D-Bus unavailable: " + bus.lastError().message());
    if (launch && !bus.interface()->isServiceRegistered(Service).value()) {
        QProcess launcher;
        launcher.setProgram(controllerExecutable()); launcher.setArguments({"--daemon"});
        // A detached daemon must not retain a CLI caller's capture pipes or terminal.
        launcher.setStandardInputFile(QProcess::nullDevice());
        launcher.setStandardOutputFile(QProcess::nullDevice()); launcher.setStandardErrorFile(QProcess::nullDevice());
        if (!launcher.startDetached()) return error("Cannot launch session controller");
        bool ready = false;
        for (int i = 0; i < 40; ++i) {
            if (bus.interface()->isServiceRegistered(Service).value()) { ready = true; break; }
            QThread::msleep(50);
        }
        if (!ready) return error("Session controller did not start; run evereye --daemon for diagnostics");
    }
    auto message = QDBusMessage::createMethodCall(Service, ObjectPath, Interface, "Request");
    message.setArguments({action, QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))});
    QDBusReply<QString> reply(bus.call(message, QDBus::Block, 12000));
    if (!reply.isValid()) return error(reply.error().message());
    QJsonParseError e; const auto d = QJsonDocument::fromJson(reply.value().toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) return error("Controller returned an invalid reply");
    auto o = d.object(); o.insert("connected", true); return o;
}
static volatile sig_atomic_t stopping = 0;
static void stopSignal(int) { stopping = 1; }
int runService() {
    ::umask(0077);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { qCritical("Session D-Bus unavailable: %s", qPrintable(bus.lastError().message())); return 1; }
    // Register the name first: only its owner may inspect or mutate the journal.
    if (!bus.registerService(Service)) { qCritical("Controller already running or service name unavailable"); return 1; }
    LinuxPlatform platform; ProcessGuardian guardian; DiskJournal journal(stateDirectory());
    JobManager jobs(jobsDirectory()); jobs.reconcile();
    Controller controller(platform, guardian, journal, bootMilliseconds, nullptr, &jobs); controller.reconcile();
    ServiceObject object(controller);
    if (!bus.registerObject(ObjectPath, &object, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportAllSignals)) return 1;
    ::signal(SIGTERM, stopSignal); ::signal(SIGINT, stopSignal); ::signal(SIGHUP, stopSignal);
    QTimer tick; tick.setInterval(1000); QObject::connect(&tick, &QTimer::timeout, [&] {
        if (stopping || !bus.isConnected()) { controller.request("end"); QCoreApplication::quit(); return; }
        controller.tick();
    }); tick.start();
    QTimer power; power.setInterval(15000); QObject::connect(&power, &QTimer::timeout, &controller, &Controller::pollPower); power.start();
    return QCoreApplication::exec();
}
}
