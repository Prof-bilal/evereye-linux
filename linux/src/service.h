#pragma once
#include "core.h"
#include <QDBusContext>

namespace insomnia {
inline constexpr auto Service = "io.github.ProfBilal.Insomnia1";
inline constexpr auto ObjectPath = "/io/github/ProfBilal/Insomnia";
inline constexpr auto Interface = "io.github.ProfBilal.Insomnia.Controller1";
class ServiceObject final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.ProfBilal.Insomnia.Controller1")
public:
    explicit ServiceObject(Controller &controller, QObject *parent = nullptr);
public slots:
    Q_SCRIPTABLE QString Request(const QString &action, const QString &json);
signals:
    void Changed();
    void JobsChanged();
    void JobOutput(const QString &id, const QByteArray &chunk);
private:
    Controller &controller;
};
QJsonObject callController(const QString &action, const QJsonObject &args = {}, bool launch = true);
QString stateDirectory();
QString jobsDirectory();
QString controllerExecutable();
int runService();
}
