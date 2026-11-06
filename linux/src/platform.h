#pragma once
#include "core.h"
#include <QDBusConnection>
#include <QDBusUnixFileDescriptor>
#include <optional>
#include <QVariantMap>

namespace insomnia {
Power interpretPower(const QVariantMap &root, const QList<QVariantMap> &devices, qint64 now);
class LinuxPlatform final : public QObject, public Platform {
    Q_OBJECT
public:
    explicit LinuxPlatform(QObject *parent = nullptr);
    ~LinuxPlatform() override;
    QJsonObject capabilities() override;
    QString inhibit(bool lid) override;
    void releaseInhibitor() override;
    bool inhibitorValid() override;
    Power power() override;
    QString holdPowerSaver() override;
    QString releasePowerSaver() override;
    bool powerSaverHeld() override { return cookie.has_value(); }
    void notify(const QString &, const QString &) override;
private slots:
    void profileReleased(uint value);
private:
    bool discoverProfiles();
    QDBusConnection bus;
    QDBusUnixFileDescriptor inhibitor;
    QString loginOwner;
    QString profileOwner;
    QString profileService, profilePath, profileInterface;
    std::optional<uint> cookie;
};
}
