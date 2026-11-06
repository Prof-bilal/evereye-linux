#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

namespace insomnia {
constexpr qint64 MinSeconds = 60;
constexpr qint64 MaxSeconds = 30LL * 24 * 3600;
qint64 bootMilliseconds();
QString bootId();
qint64 parseDuration(const QString &text);
QString formatDuration(qint64 seconds);

struct Options {
    bool lid = true;
    bool powerSaver = true;
    int endFloor = 10;
    int lowPowerFloor = 40;
    QJsonObject json() const;
    static bool parse(const QJsonObject &, Options &, QString &error);
};
struct Power {
    enum Battery { Unknown, None, Present } battery = Unknown;
    bool acKnown = false;
    bool onBattery = true;
    double percent = -1;
    bool lidKnown = false;
    bool lidClosed = false;
    QString error;
};
class Platform {
public:
    virtual ~Platform() = default;
    virtual QJsonObject capabilities() = 0;
    virtual QString inhibit(bool lid) = 0;
    virtual void releaseInhibitor() = 0;
    virtual bool inhibitorValid() = 0;
    virtual Power power() = 0;
    virtual QString holdPowerSaver() = 0;
    virtual QString releasePowerSaver() = 0;
    virtual bool powerSaverHeld() = 0;
    virtual void notify(const QString &, const QString &) = 0;
};
class Guardian {
public:
    virtual ~Guardian() = default;
    virtual QString arm(qint64 deadlineMs) = 0;
    virtual QString heartbeat(qint64 deadlineMs) = 0;
    virtual void stop() = 0;
};
class Journal {
public:
    virtual ~Journal() = default;
    virtual QJsonObject read(QString &error) = 0;
    virtual QString write(const QJsonObject &) = 0;
};
class DiskJournal final : public Journal {
public:
    explicit DiskJournal(QString directory);
    ~DiskJournal() override;
    DiskJournal(const DiskJournal &) = delete;
    DiskJournal &operator=(const DiskJournal &) = delete;
    QJsonObject read(QString &error) override;
    QString write(const QJsonObject &) override;
    QString path() const;
private:
    QString ensureLock();
    QString directory;
    int lockFd = -1;
};

class Controller final : public QObject {
    Q_OBJECT
public:
    Controller(Platform &, Guardian &, Journal &, std::function<qint64()> clock = bootMilliseconds,
               QObject *parent = nullptr);
    ~Controller() override;
    QJsonObject request(const QString &action, const QJsonObject &args = {});
    void tick();
    void pollPower();
    void reconcile();
    QJsonObject status() const;
    bool active() const { return running; }
signals:
    void changed();
private:
    QJsonObject start(const QJsonObject &);
    QJsonObject extend(const QJsonObject &);
    QJsonObject end(const QString &reason);
    QJsonObject fail(const QString &message);
    QJsonObject record(bool active, qint64 deadline, const QString &reason) const;
    QJsonObject ok() const;
    Platform &platform;
    Guardian &guardian;
    Journal &journal;
    std::function<qint64()> clock;
    Options options;
    Power latest;
    bool running = false;
    bool saverWanted = false;
    bool saverSuppressed = false;
    bool fiveMinuteNotice = false;
    bool recoveryBlocked = false;
    qint64 deadline = 0;
    int unreadableCount = 0;
    QString lastError;
    QString lastReason = "Ready";
    QString profileWarning;
};
}
