// Test fixtures only: registered exclusively on the caller's private D-Bus.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <fcntl.h>
#include <unistd.h>
#ifdef EVEREYE_LEGACY_PROFILES
#define PROFILE_SERVICE "net.hadess.PowerProfiles"
#define PROFILE_PATH "/net/hadess/PowerProfiles"
#else
#define PROFILE_SERVICE "org.freedesktop.UPower.PowerProfiles"
#define PROFILE_PATH "/org/freedesktop/UPower/PowerProfiles"
#endif

class Login final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")
    Q_PROPERTY(bool LidClosed MEMBER lid)
    Q_PROPERTY(int ActiveLocks READ activeLocks)
public:
    bool lid = false, denied = false; QList<int> readers;
    ~Login() override { for (int fd : readers) ::close(fd); }
    int activeLocks() {
        for (int i = readers.size() - 1; i >= 0; --i) { char byte; if (::read(readers[i], &byte, 1) == 0) { ::close(readers[i]); readers.removeAt(i); } }
        return readers.size();
    }
public slots:
    QDBusUnixFileDescriptor Inhibit(const QString &what, const QString &, const QString &, const QString &mode) {
        if (denied || mode != "block" || (what != "sleep" && what != "sleep:handle-lid-switch")) {
            sendErrorReply(QDBusError::AccessDenied, "Test: inhibitor denied"); return {};
        }
        int fds[2]; if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) { sendErrorReply(QDBusError::Failed, "pipe failed"); return {}; }
        readers.append(fds[0]); QDBusUnixFileDescriptor result(fds[1]); ::close(fds[1]); return result;
    }
    void Deny(bool value) { denied = value; }
    void SetLid(bool value) { lid = value; }
};
class Battery final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower.Device")
    Q_PROPERTY(uint Type READ type)
    Q_PROPERTY(bool PowerSupply READ yes)
    Q_PROPERTY(bool IsPresent READ yes)
    Q_PROPERTY(double Percentage MEMBER percent)
    Q_PROPERTY(qulonglong UpdateTime READ updated)
public:
    double percent = 80; bool stale = false;
    uint type() const { return 2; } bool yes() const { return true; }
    qulonglong updated() const { return QDateTime::currentSecsSinceEpoch() - (stale ? 500 : 0); }
public slots:
    void SetPercentage(double value) { percent = value; }
    void SetStale(bool value) { stale = value; }
};
class UPower final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UPower")
    Q_PROPERTY(bool OnBattery MEMBER onBattery)
public:
    bool onBattery = false;
public slots:
    QList<QDBusObjectPath> EnumerateDevices() { return {QDBusObjectPath("/org/freedesktop/UPower/devices/battery_BAT0")}; }
    void SetOnBattery(bool value) { onBattery = value; }
};
class Profiles final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", PROFILE_SERVICE)
    Q_PROPERTY(uint Holds READ count)
    Q_PROPERTY(QString ActiveProfile READ activeProfile)
public:
    QMap<uint, QString> holds; uint sequence = 0;
    uint count() const { return holds.size(); }
    QString activeProfile() const { return holds.isEmpty() ? "balanced" : "power-saver"; }
    explicit Profiles(QObject *parent = nullptr) : QObject(parent) {
        QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged", this, SLOT(ownerChanged(QString,QString,QString)));
    }
public slots:
    uint HoldProfile(const QString &profile, const QString &, const QString &) {
        if (profile != "power-saver") { sendErrorReply(QDBusError::InvalidArgs, "unexpected profile"); return 0; }
        const auto cookie = ++sequence; holds.insert(cookie, message().service()); return cookie;
    }
    void ReleaseProfile(uint cookie) {
        if (holds.value(cookie) != message().service()) { sendErrorReply(QDBusError::AccessDenied, "not your hold"); return; }
        holds.remove(cookie);
    }
    void ManualOverride() { const auto keys = holds.keys(); holds.clear(); for (auto key : keys) emit ProfileReleased(key); }
    void ownerChanged(const QString &name, const QString &, const QString &next) {
        if (next.isEmpty()) { const auto keys = holds.keys(); for (auto key : keys) if (holds.value(key) == name) holds.remove(key); }
    }
signals:
    void ProfileReleased(uint cookie);
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return 1;
    Login login; UPower upower; Battery battery; Profiles profiles;
    for (const auto &name : {"org.freedesktop.login1", "org.freedesktop.UPower", PROFILE_SERVICE}) if (!bus.registerService(name)) return 2;
    const auto flags = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals;
    if (!bus.registerObject("/org/freedesktop/login1", &login, flags) || !bus.registerObject("/org/freedesktop/UPower", &upower, flags) ||
        !bus.registerObject("/org/freedesktop/UPower/devices/battery_BAT0", &battery, flags) || !bus.registerObject(PROFILE_PATH, &profiles, flags)) return 3;
    return app.exec();
}
#include "fake_services.moc"
