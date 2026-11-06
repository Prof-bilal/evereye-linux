#include "platform.h"
#include "guardian.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QJsonArray>
#include <QDateTime>
#include <QSysInfo>
#include <fcntl.h>
#include <cmath>

namespace insomnia {
static constexpr auto Login = "org.freedesktop.login1";
static constexpr auto LoginPath = "/org/freedesktop/login1";
static constexpr auto LoginInterface = "org.freedesktop.login1.Manager";
static QDBusMessage invoke(const QDBusConnection &bus, const QString &service, const QString &path,
                           const QString &interface, const QString &method, const QVariantList &args = {}) {
    auto msg = QDBusMessage::createMethodCall(service, path, interface, method); msg.setArguments(args);
    return bus.call(msg, QDBus::Block, 1500);
}
static QString owner(const QDBusConnection &bus, const QString &name) {
    QDBusReply<QString> reply(invoke(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", {name}));
    return reply.isValid() ? reply.value() : QString();
}
static QVariantMap properties(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, QString &error) {
    QDBusReply<QVariantMap> reply(invoke(bus, service, path, "org.freedesktop.DBus.Properties", "GetAll", {interface}));
    if (!reply.isValid()) { error = reply.error().message(); return {}; }
    return reply.value();
}
LinuxPlatform::LinuxPlatform(QObject *parent) : QObject(parent), bus(QDBusConnection::systemBus()) {
}
LinuxPlatform::~LinuxPlatform() { releasePowerSaver(); releaseInhibitor(); }
void LinuxPlatform::profileReleased(uint value) { if (cookie && *cookie == value) cookie.reset(); }
bool LinuxPlatform::discoverProfiles() {
    if (cookie) return true; // A live cookie belongs to its original unique owner.
    if (!profileOwner.isEmpty()) bus.disconnect(profileOwner, profilePath, profileInterface, "ProfileReleased", this, SLOT(profileReleased(uint)));
    profileOwner.clear(); profileService.clear(); profilePath.clear(); profileInterface.clear();
    const QList<QPair<QString, QString>> endpoints{
        {"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles"},
        {"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles"}
    };
    for (const auto &endpoint : endpoints) {
        QString error;
        // Calling the well-known name permits normal D-Bus service activation.
        const auto props = properties(bus, endpoint.first, endpoint.second, endpoint.first, error);
        if (!error.isEmpty() || props.isEmpty()) continue;
        const auto unique = owner(bus, endpoint.first); if (unique.isEmpty()) continue;
        profileService = endpoint.first; profileInterface = endpoint.first; profilePath = endpoint.second; profileOwner = unique;
        bus.connect(profileOwner, profilePath, profileInterface, "ProfileReleased", this, SLOT(profileReleased(uint)));
        return true;
    }
    return false;
}
QJsonObject LinuxPlatform::capabilities() {
    QString error;
    const auto login = properties(bus, Login, LoginPath, LoginInterface, error);
    const auto loginError = error; error.clear();
    const bool profilesAvailable = discoverProfiles();
    if (!profilesAvailable) error = "No compatible current or legacy power-profile service";
    const auto snapshot = power();
    const auto supervisorError = guardianAvailabilityError();
    return {{"systemBus", bus.isConnected()}, {"unixFdPassing", bool(bus.connectionCapabilities() & QDBusConnection::UnixFileDescriptorPassing)},
            {"logind", !login.isEmpty()}, {"logindError", loginError}, {"lidKnown", snapshot.lidKnown},
            {"upower", snapshot.battery != Power::Unknown}, {"upowerError", snapshot.error},
            {"powerProfiles", profilesAvailable}, {"powerProfilesError", error}, {"powerProfilesService", profileService},
            {"inhibitorProvider", "org.freedesktop.login1 (systemd-logind or elogind)"},
            {"supervisor", supervisorError.isEmpty()}, {"supervisorError", supervisorError}, {"supervisorBackend", guardianBackend()},
            {"kernel", QSysInfo::kernelVersion()}, {"architecture", QSysInfo::currentCpuArchitecture()},
            {"qtRuntime", qVersion()}, {"qtBuild", QT_VERSION_STR},
            {"packageRuntime", qEnvironmentVariableIsSet("FLATPAK_ID") ? "flatpak" : qEnvironmentVariableIsSet("APPIMAGE") ? "appimage" : "native"},
            {"thermalRules", false}, {"appFreezing", false},
            {"lidCompatibility", "Requires desktop validation: another desktop/compositor may implement its own lid policy."}};
}
QString LinuxPlatform::inhibit(bool lid) {
    releaseInhibitor();
    if (!bus.isConnected()) return "System D-Bus unavailable: " + bus.lastError().message();
    if (!(bus.connectionCapabilities() & QDBusConnection::UnixFileDescriptorPassing)) return "System D-Bus cannot pass Unix file descriptors";
    loginOwner = owner(bus, Login);
    if (loginOwner.isEmpty()) return "No logind-compatible service is available (systemd-logind or elogind required)";
    QDBusReply<QDBusUnixFileDescriptor> reply(invoke(bus, loginOwner, LoginPath, LoginInterface, "Inhibit",
        {lid ? "sleep:handle-lid-switch" : "sleep", "EverEye", "Timed awake session", "block"}));
    if (!reply.isValid()) return "Cannot inhibit sleep: " + reply.error().message();
    inhibitor = reply.value();
    if (!inhibitor.isValid()) return "logind returned an invalid inhibitor file descriptor";
    const int flags = ::fcntl(inhibitor.fileDescriptor(), F_GETFD);
    if (flags < 0 || ::fcntl(inhibitor.fileDescriptor(), F_SETFD, flags | FD_CLOEXEC) < 0) {
        releaseInhibitor(); return "Cannot prevent inhibitor inheritance";
    }
    if (owner(bus, Login) != loginOwner) { releaseInhibitor(); return "logind restarted during inhibitor acquisition"; }
    return {};
}
void LinuxPlatform::releaseInhibitor() { inhibitor = QDBusUnixFileDescriptor(); loginOwner.clear(); }
bool LinuxPlatform::inhibitorValid() { return bus.isConnected() && inhibitor.isValid() && !loginOwner.isEmpty() && owner(bus, Login) == loginOwner; }
Power interpretPower(const QVariantMap &root, const QList<QVariantMap> &devices, qint64 now) {
    Power p; p.acKnown = root.value("OnBattery").metaType().id() == QMetaType::Bool;
    p.onBattery = p.acKnown ? root.value("OnBattery").toBool() : true;
    p.battery = Power::None; double minimum = 100; bool unreadable = false;
    for (const auto &d : devices) {
        if (!d.contains("Type") || !d.contains("PowerSupply") || !d.contains("IsPresent")) {
            p.battery = Power::Unknown; p.error = "Incomplete power-device metadata"; return p;
        }
        if (d.value("Type").toUInt() != 2 || !d.value("PowerSupply").toBool() || !d.value("IsPresent").toBool()) continue;
        p.battery = Power::Present;
        bool ok = false; const double percent = d.value("Percentage").toDouble(&ok);
        const auto updated = d.value("UpdateTime").toULongLong();
        if (!ok || !std::isfinite(percent) || percent < 0 || percent > 100 || updated == 0 || updated > quint64(now + 60) || qint64(updated) < now - 180) unreadable = true;
        else minimum = qMin(minimum, percent);
    }
    if (p.battery == Power::Present) p.percent = unreadable ? -1 : minimum;
    if (unreadable) p.error = "Battery percentage is unavailable or stale";
    if (!p.acKnown) p.error = "AC power state is unavailable";
    if (p.battery == Power::None && p.acKnown && p.onBattery) { p.battery = Power::Unknown; p.error = "UPower reports battery power but no system battery"; }
    return p;
}
Power LinuxPlatform::power() {
    Power p; QString error;
    const auto login = properties(bus, Login, LoginPath, LoginInterface, error);
    const bool lidKnown = login.value("LidClosed").metaType().id() == QMetaType::Bool;
    const bool lidClosed = login.value("LidClosed").toBool();
    error.clear();
    const auto root = properties(bus, "org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.UPower", error);
    if (!error.isEmpty()) { p.error = error; return p; }
    QDBusReply<QList<QDBusObjectPath>> paths(invoke(bus, "org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.UPower", "EnumerateDevices"));
    if (!paths.isValid() || paths.value().size() > 32) { p.error = "Cannot enumerate system batteries"; return p; }
    QList<QVariantMap> devices;
    for (const auto &path : paths.value()) {
        devices.append(properties(bus, "org.freedesktop.UPower", path.path(), "org.freedesktop.UPower.Device", error));
        if (!error.isEmpty()) { p.error = error; return p; }
    }
    p = interpretPower(root, devices, QDateTime::currentSecsSinceEpoch());
    p.lidKnown = lidKnown; p.lidClosed = lidClosed;
    const auto currentOwner = cookie ? owner(bus, profileService) : QString();
    if (cookie && !currentOwner.isEmpty() && currentOwner != profileOwner) cookie.reset();
    return p;
}
QString LinuxPlatform::holdPowerSaver() {
    if (cookie) return {};
    if (!discoverProfiles()) return "Power saver unavailable: no compatible current or legacy power-profile service";
    QDBusReply<uint> reply(invoke(bus, profileOwner, profilePath, profileInterface, "HoldProfile", {"power-saver", "EverEye battery/lid policy", "io.github.ProfBilal.EverEye"}));
    if (!reply.isValid()) return "Cannot hold power-saver profile: " + reply.error().message();
    cookie = reply.value();
    if (owner(bus, profileService) != profileOwner) { cookie.reset(); return "Power-profile service restarted during acquisition"; }
    return {};
}
QString LinuxPlatform::releasePowerSaver() {
    if (!cookie) return {};
    const uint value = *cookie;
    const auto currentOwner = owner(bus, profileService);
    if (currentOwner.isEmpty()) return "Power-saver service could not be reached; release will be retried";
    if (currentOwner != profileOwner) { cookie.reset(); return {}; }
    const auto reply = invoke(bus, profileOwner, profilePath, profileInterface, "ReleaseProfile", {value});
    if (reply.type() == QDBusMessage::ErrorMessage) return "Power-saver release failed: " + reply.errorMessage();
    cookie.reset(); return {};
}
void LinuxPlatform::notify(const QString &title, const QString &body) {
    auto msg = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
    msg.setArguments({"EverEye", uint(0), "io.github.ProfBilal.EverEye", title, body, QStringList(), QVariantMap(), 8000});
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}
}
