#pragma once
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QSocketNotifier;
class QTimer;

namespace insomnia {
struct JobSpec {
    QString name;
    QString command;
    QStringList args;
    QString cwd;
    QStringList env;
};

class JobManager final : public QObject {
    Q_OBJECT
public:
    explicit JobManager(QString stateDir, QString configDir = {}, QObject *parent = nullptr);
    ~JobManager() override;
    JobManager(const JobManager &) = delete;
    JobManager &operator=(const JobManager &) = delete;
    QJsonObject list() const;
    QJsonObject get(const QString &id) const;
    QJsonObject start(const QJsonObject &args);
    QJsonObject stop(const QString &id);
    QJsonObject log(const QJsonObject &args);
    QJsonObject tasks() const;
    int jobCount() const { return records.size(); }
    int runningCount() const;
    bool allowArbitraryCommands() const { return arbitrary; }
    void reconcile();
signals:
    void jobChanged(const QString &id);
    void jobEnded(const QString &id, int exitCode);
    void output(const QString &id, const QByteArray &chunk);
private:
    struct Record {
        QString id, name, command, cwd, logPath, status = "Starting";
        QStringList args, env;
        qint64 pid = 0, pgid = 0, startedMs = 0, endedMs = 0, bytesTotal = 0;
        int exitCode = -1, termSignal = 0, masterFd = -1;
        QByteArray ring;
        QSocketNotifier *notifier = nullptr;
        QTimer *killTimer = nullptr;
    };
    QJsonObject snapshot(const Record &) const;
    QJsonObject fail(const QString &message) const;
    QString persist();
    QString loadConfig();
    QJsonArray loadTasks() const;
    QString spawn(Record &);
    void ingest(Record &, const QByteArray &);
    void reap(Record &, bool killed);
    void closeMaster(Record &);
    void requestKill(Record &, int signal);
    QString stateDir, configDir, journalPath;
    int maxJobs = 8;
    qint64 logMaxBytes = 8LL * 1024 * 1024;
    bool arbitrary = false;
    QHash<QString, Record> records;
    QStringList order;
};
}
