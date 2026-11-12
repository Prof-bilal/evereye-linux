#include "core.h"
#include "guardian.h"
#include "jobs.h"
#include "platform.h"
#include "service.h"
#include "terminal.h"
#include "window.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QWheelEvent>
#include <QScrollBar>
#include <QPushButton>
#include <QScrollArea>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QStackedWidget>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QScopeGuard>
#include <atomic>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits>

using namespace insomnia;
class FakePlatform final : public Platform {
public:
    Power snapshot;
    QString inhibitError, saverError, releaseError;
    bool held = false, valid = true;
    int readFd = -1, writeFd = -1, acquisitions = 0, saverAcquisitions = 0, notifications = 0;
    QStringList events;
    std::function<void()> beforeProfileRelease;
    FakePlatform() { snapshot.battery = Power::Present; snapshot.percent = 80; snapshot.acKnown = true; snapshot.onBattery = false; }
    ~FakePlatform() override { releaseInhibitor(); }
    QJsonObject capabilities() override { return {{"systemBus", true}}; }
    QString inhibit(bool lid) override {
        acquisitions++; events << (lid ? "lid" : "sleep");
        if (!inhibitError.isEmpty()) return inhibitError;
        int fds[2]; if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return "pipe failed";
        readFd = fds[0]; writeFd = fds[1]; return {};
    }
    void releaseInhibitor() override {
        if (writeFd >= 0) { ::close(writeFd); writeFd = -1; }
        if (readFd >= 0) { ::close(readFd); readFd = -1; }
    }
    bool inhibitorValid() override { return valid && writeFd >= 0; }
    Power power() override { return snapshot; }
    QString holdPowerSaver() override { saverAcquisitions++; if (saverError.isEmpty()) held = true; return saverError; }
    QString releasePowerSaver() override { if (beforeProfileRelease) beforeProfileRelease(); if (releaseError.isEmpty()) held = false; return releaseError; }
    bool powerSaverHeld() override { return held; }
    void notify(const QString &, const QString &) override { notifications++; }
};
class FakeGuardian final : public Guardian {
public:
    QString error; qint64 deadline = 0; bool armed = false; int arms = 0;
    QString arm(qint64 d) override { arms++; deadline = d; armed = error.isEmpty(); return error; }
    QString heartbeat(qint64 d) override { deadline = d; return error; }
    void stop() override { armed = false; }
};
class MemoryJournal final : public Journal {
public:
    QJsonObject saved; QString readError, writeError; int writes = 0;
    QJsonObject read(QString &error) override { error = readError; return saved; }
    QString write(const QJsonObject &o) override { writes++; if (writeError.isEmpty()) saved = o; return writeError; }
};
struct Fixture {
    FakePlatform platform; FakeGuardian guardian; MemoryJournal journal; qint64 now = 10000;
    Controller controller{platform, guardian, journal, [this] { return now; }};
    QJsonObject start(qint64 seconds = 3600, QJsonObject extra = {}) { extra.insert("seconds", seconds); return controller.request("start", extra); }
};

class InsomniaTests final : public QObject {
    Q_OBJECT
private slots:
    void packagedControllerUsesItsOwnAppImageRuntime() {
        const auto priorImage=qgetenv("APPIMAGE"), priorDir=qgetenv("APPDIR");
        const bool hadImage=qEnvironmentVariableIsSet("APPIMAGE"), hadDir=qEnvironmentVariableIsSet("APPDIR");
        const auto restore=qScopeGuard([&] {
            if(hadImage) qputenv("APPIMAGE",priorImage); else qunsetenv("APPIMAGE");
            if(hadDir) qputenv("APPDIR",priorDir); else qunsetenv("APPDIR");
        });
        qunsetenv("APPIMAGE"); qunsetenv("APPDIR");
        QCOMPARE(controllerExecutable(),QCoreApplication::applicationFilePath());
        QTemporaryDir temp; QFile image(temp.path()+"/EverEye.AppImage");
        QVERIFY(image.open(QIODevice::WriteOnly)); image.write("#!/bin/sh\nexit 0\n"); image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        qputenv("APPIMAGE",QFile::encodeName(image.fileName()));
        qputenv("APPDIR",QFile::encodeName(temp.path()));
        QCOMPARE(controllerExecutable(),QCoreApplication::applicationFilePath());
        qputenv("APPDIR",QFile::encodeName(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath()));
        QCOMPARE(controllerExecutable(),image.fileName());
    }
    void duration_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<qint64>("expected");
        QTest::newRow("seconds") << QString("60") << qint64(60);
        QTest::newRow("mixed") << QString("2h30m") << qint64(9000);
        QTest::newRow("days") << QString("1d2h3m4s") << qint64(93784);
        QTest::newRow("case") << QString(" 1H ") << qint64(3600);
        QTest::newRow("limit") << QString("30d") << MaxSeconds;
        for (const auto &s : QStringList{"", "-1h", "1.5h", "1h1h", "1 hour", "999999999999999999999d", "30d1s", "NaN", "1h trailing", "60.0"})
            QTest::newRow(qPrintable("invalid-" + s)) << s << qint64(-1);
    }
    void duration() { QFETCH(QString, input); QFETCH(qint64, expected); QCOMPARE(parseDuration(input), expected); }
    void thresholdsValidate() {
        Options o; QString error; QVERIFY(!Options::parse({{"endFloor", 40}}, o, error));
        error.clear(); QVERIFY(Options::parse({{"endFloor", 0}, {"lowPowerFloor", 0}}, o, error));
        for (const QJsonValue &v : {QJsonValue(-1), QJsonValue(101), QJsonValue(3.2), QJsonValue("10"), QJsonValue(true)}) {
            o = Options{}; QVERIFY(!Options::parse({{"endFloor", v}}, o, error));
        }
        QVERIFY(!Options::parse({{"typo", true}}, o, error));
        QVERIFY(!Options::parse({{"lid", "true"}}, o, error));
    }
    void jobsRunAndCaptureOutput() {
        QTemporaryDir temp;
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config");
        const auto started = manager.start({{"command", "sh"}, {"args", QJsonArray{"-c", "printf hi"}}, {"cwd", temp.path()}});
        QVERIFY2(started.value("ok").toBool(), qPrintable(started.value("error").toString()));
        const auto id = started.value("id").toString();
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(manager.get(id).value("status").toString() == "Exited", 4000);
        QCOMPARE(manager.get(id).value("exitCode").toInt(), 0);
        const auto log = manager.log({{"id", id}, {"offset", 0}});
        QVERIFY(log.value("ok").toBool());
        QVERIFY(log.value("data").toString().contains("hi"));
        QVERIFY(QFile::exists(started.value("logPath").toString()));
    }
    void jobsStopKillsChild() {
        QTemporaryDir temp;
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config");
        const auto started = manager.start({{"command", "sleep"}, {"args", QJsonArray{"300"}}, {"cwd", temp.path()}});
        QVERIFY(started.value("ok").toBool());
        const auto id = started.value("id").toString();
        const auto pid = started.value("pid").toInteger();
        QVERIFY(pid > 1);
        QVERIFY(manager.stop(id).value("ok").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(::kill(pid_t(pid), 0) == -1, 6000);
        QCOMPARE(manager.get(id).value("status").toString(), QString("Killed"));
    }
    void jobsInputReachesPty() {
        QTemporaryDir temp;
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config");
        const auto started = manager.start({{"command", "cat"}, {"cwd", temp.path()}});
        QVERIFY2(started.value("ok").toBool(), qPrintable(started.value("error").toString()));
        const auto id = started.value("id").toString();
        const auto sent = manager.input({{"id", id}, {"data", "ping\n"}});
        QVERIFY2(sent.value("ok").toBool(), qPrintable(sent.value("error").toString()));
        QTRY_VERIFY_WITH_TIMEOUT(manager.log({{"id", id}, {"offset", 0}})["data"].toString().contains("ping"), 4000);
        manager.stop(id);
    }
    void attachBridgeEchoesAndDetaches() {
        QTemporaryDir temp;
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config", nullptr, temp.path() + "/run");
        const auto started = manager.start({{"command", "cat"}, {"cwd", temp.path()}});
        QVERIFY2(started.value("ok").toBool(), qPrintable(started.value("error").toString()));
        const auto id = started.value("id").toString();
        QLocalSocket client;
        client.connectToServer(temp.path() + "/run/" + id + ".sock");
        QVERIFY(client.waitForConnected(3000));
        QTest::qWait(50);  // let the controller accept the connection
        const auto frame = [](quint8 type, const QByteArray &payload) {
            QByteArray out; out.append(char(type));
            char header[4]; qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(header));
            out.append(header, 4); out.append(payload); return out;
        };
        client.write(frame(1, "attach-hi\n"));
        client.flush();
        QByteArray received;
        QElapsedTimer timer; timer.start();
        while (!received.contains("attach-hi") && timer.elapsed() < 4000) {
            QTest::qWait(20);
            received += client.readAll();
        }
        QVERIFY2(received.contains("attach-hi"), received.constData());
        client.disconnectFromServer();
        QTRY_COMPARE(manager.get(id).value("status").toString(), QString("Running"));
        manager.stop(id);
    }
    void terminalCommandPrefersConfiguredLauncher() {
        const auto lookup = [](const QString &name) { return name == "xdg-terminal-exec" ? QString("/usr/bin/xdg-terminal-exec") : QString(); };
        const auto launch = terminalCommand("/opt/evereye", {"attach", "abc"}, "/home/me", "EverEye · t", "xdg-terminal-exec", lookup);
        QVERIFY(launch.ok);
        QCOMPARE(launch.program, QString("/usr/bin/xdg-terminal-exec"));
        QVERIFY(launch.args.contains("--dir=/home/me"));
        QVERIFY(launch.args.contains("--title=EverEye · t"));
        QVERIFY(launch.args.contains("/opt/evereye"));
        QVERIFY(launch.args.contains("abc"));
    }
    void terminalCommandKnowsFootFlags() {
        const auto lookup = [](const QString &name) { return name == "foot" ? QString("/usr/bin/foot") : QString(); };
        const auto launch = terminalCommand("/opt/evereye", {"attach", "abc"}, "/home/me", "EverEye · t", "", lookup);
        QVERIFY(launch.ok);
        QCOMPARE(launch.program, QString("/usr/bin/foot"));
        QCOMPARE(launch.args.mid(0, 4), (QStringList{"-T", "EverEye · t", "-D", "/home/me"}));
    }
    void terminalCommandReportsNoTerminal() {
        const auto launch = terminalCommand("/opt/evereye", {"attach", "abc"}, "/home/me", "t", "", [](const QString &) { return QString(); });
        QVERIFY(!launch.ok);
    }
    void jobsShellAcceptsTypedCommands() {
        QTemporaryDir temp;
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config");
        const auto started = manager.start({{"shell", true}, {"cwd", temp.path()}});
        QVERIFY2(started.value("ok").toBool(), qPrintable(started.value("error").toString()));
        const auto id = started.value("id").toString();
        QVERIFY(manager.input({{"id", id}, {"data", "echo shell-ok\n"}}).value("ok").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(manager.log({{"id", id}, {"offset", 0}})["data"].toString().contains("shell-ok"), 6000);
        manager.stop(id);
    }
    void jobsJournalMarksDeadPidInterrupted() {
        QTemporaryDir temp;
        const auto dir = temp.path() + "/jobs";
        QVERIFY(QDir().mkpath(dir));
        QFile journal(dir + "/jobs.json");
        QVERIFY(journal.open(QIODevice::WriteOnly));
        journal.write(R"({"schema":1,"jobs":[{"id":"deadjob1","name":"x","command":"sleep","status":"Running","pid":1,"pgid":1}]})");
        journal.close();
        JobManager manager(dir, temp.path() + "/config");
        manager.reconcile();
        QCOMPARE(manager.get("deadjob1").value("status").toString(), QString("Interrupted"));
    }
    void jobsCapRefusesExtra() {
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.path() + "/config"));
        QFile cfg(temp.path() + "/config/jobs.json");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write(R"({"maxJobs":1})");
        cfg.close();
        JobManager manager(temp.path() + "/jobs", temp.path() + "/config");
        QVERIFY(manager.start({{"command", "sleep"}, {"args", QJsonArray{"30"}}, {"cwd", temp.path()}}).value("ok").toBool());
        const auto extra = manager.start({{"command", "sleep"}, {"args", QJsonArray{"30"}}, {"cwd", temp.path()}});
        QVERIFY(!extra.value("ok").toBool());
        QVERIFY(extra.value("error").toString().contains("Too many"));
    }
    void unknownJobActionWithoutManager() {
        Fixture f;
        QVERIFY(!f.controller.request("jobs.list").value("ok").toBool());
    }
    void startAndExplicitEndCloseActualDescriptor() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); QVERIFY(f.controller.active()); QVERIFY(f.guardian.armed);
        QVERIFY(f.journal.saved.value("active").toBool()); QCOMPARE(f.platform.acquisitions, 1);
        const int fd = f.platform.writeFd; QVERIFY(::fcntl(fd, F_GETFD) >= 0);
        QVERIFY(f.controller.request("end").value("ok").toBool()); QVERIFY(!f.controller.active());
        QCOMPARE(::fcntl(fd, F_GETFD), -1); QVERIFY(!f.guardian.armed); QVERIFY(!f.journal.saved.value("active").toBool());
        QVERIFY(f.controller.request("end").value("ok").toBool());
    }
    void deadlineUsesBootClock() {
        Fixture f; QVERIFY(f.start(60).value("ok").toBool()); f.now += 59999; f.controller.tick(); QVERIFY(f.controller.active());
        f.now += 1; f.controller.tick(); QVERIFY(!f.controller.active()); QCOMPARE(f.controller.status().value("reason").toString(), "Timer expired");
    }
    void invalidDurationNeverAcquires() {
        for (auto seconds : {qint64(-1), qint64(0), qint64(59), MaxSeconds + 1}) {
            Fixture f; QVERIFY(!f.start(seconds).value("ok").toBool()); QCOMPARE(f.platform.acquisitions, 0); QCOMPARE(f.journal.writes, 0);
        }
        Fixture f; QVERIFY(!f.controller.request("start", {{"seconds", "60"}}).value("ok").toBool());
    }
    void duplicateStartDoesNotReplaceSession() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); auto before = f.journal.saved;
        QVERIFY(!f.start(60).value("ok").toBool()); QCOMPARE(f.platform.acquisitions, 1); QCOMPARE(f.journal.saved, before);
    }
    void extensionUpdatesSupervisorAndDisk() {
        Fixture f; QVERIFY(f.start(60).value("ok").toBool()); f.now += 1000;
        QVERIFY(f.controller.request("extend", {{"seconds", 900}}).value("ok").toBool());
        QCOMPARE(f.guardian.deadline, qint64(970000)); QCOMPARE(f.journal.saved.value("deadlineBootMs").toInteger(), f.guardian.deadline);
        QCOMPARE(f.controller.status().value("remainingSeconds").toInteger(), qint64(959));
    }
    void extensionIsBoundedAndCannotResurrect() {
        Fixture f; QVERIFY(f.start(MaxSeconds).value("ok").toBool());
        QVERIFY(f.controller.request("extend", {{"seconds", MaxSeconds}}).value("ok").toBool());
        QCOMPARE(f.controller.status().value("remainingSeconds").toInteger(), MaxSeconds);
        f.now += MaxSeconds * 1000; QVERIFY(!f.controller.request("extend", {{"seconds", 60}}).value("ok").toBool()); QVERIFY(!f.controller.active());
    }
    void journalFailurePreventsSideEffects() {
        Fixture f; f.journal.writeError = "disk full"; QVERIFY(!f.start().value("ok").toBool());
        QCOMPARE(f.platform.acquisitions, 0); QCOMPARE(f.guardian.arms, 0);
    }
    void endPersistenceFailureStillReleases() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); const auto fd = f.platform.writeFd;
        f.journal.writeError = "disk full"; QVERIFY(!f.controller.request("end").value("ok").toBool());
        QVERIFY(!f.controller.active()); QCOMPARE(::fcntl(fd, F_GETFD), -1); QVERIFY(!f.guardian.armed);
        QVERIFY(!f.start().value("ok").toBool()); QVERIFY(f.controller.status().value("recoveryBlocked").toBool());
    }
    void extensionPersistenceFailureKeepsOldDeadline() {
        Fixture f; QVERIFY(f.start(60).value("ok").toBool()); const auto old = f.guardian.deadline;
        f.journal.writeError = "read only"; QVERIFY(!f.controller.request("extend", {{"seconds", 900}}).value("ok").toBool()); QCOMPARE(f.guardian.deadline, old);
        f.now = old; f.controller.tick(); QVERIFY(!f.controller.active());
    }
    void supervisorFailurePreventsInhibition() {
        Fixture f; f.guardian.error = "supervisor refused"; QVERIFY(!f.start().value("ok").toBool());
        QCOMPARE(f.platform.acquisitions, 0); QVERIFY(!f.controller.active()); QVERIFY(!f.journal.saved.value("active").toBool());
    }
    void inhibitorFailureRollsBack() {
        Fixture f; f.platform.inhibitError = "access denied"; QVERIFY(!f.start().value("ok").toBool());
        QVERIFY(!f.guardian.armed); QVERIFY(!f.journal.saved.value("active").toBool()); QVERIFY(!f.controller.active());
    }
    void lostInhibitorEndsSession() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); f.platform.valid = false; f.controller.tick(); QVERIFY(!f.controller.active());
    }
    void lostSupervisorEndsSession() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); f.guardian.error = "watchdog died"; f.controller.tick();
        QVERIFY(!f.controller.active()); QCOMPARE(f.platform.writeFd, -1);
    }
    void oldSessionNeverResumesAutomatically() {
        Fixture f; f.journal.saved = {{"schema", 1}, {"active", true}, {"deadlineBootMs", 99999999}, {"bootId", "previous-boot"}};
        f.controller.reconcile(); QVERIFY(!f.controller.active()); QCOMPARE(f.platform.acquisitions, 0); QVERIFY(!f.journal.saved.value("active").toBool());
    }
    void unreadableJournalIsPreserved() {
        Fixture f; f.journal.readError = "corrupt journal"; f.journal.saved = {{"evidence", true}}; f.controller.reconcile();
        QVERIFY(!f.start().value("ok").toBool()); QCOMPARE(f.journal.writes, 0); QVERIFY(f.journal.saved.value("evidence").toBool());
        f.controller.request("end"); QCOMPARE(f.journal.writes, 0);
    }
    void batteryCutoffAndTransientFailures() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = -1;
        f.controller.pollPower(); QVERIFY(f.controller.active()); f.platform.snapshot.percent = 80; f.controller.pollPower(); QVERIFY(f.controller.active());
        f.platform.snapshot.percent = -1; f.controller.pollPower(); QVERIFY(f.controller.active()); f.controller.pollPower(); QVERIFY(!f.controller.active());
    }
    void batteryBelowFloorRefusesStart() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 9; QVERIFY(!f.start().value("ok").toBool()); QCOMPARE(f.platform.acquisitions, 0);
    }
    void batteryAtFloorStaysActive() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 10; QVERIFY(f.start().value("ok").toBool());
        f.platform.snapshot.percent = 9; f.controller.pollPower(); QVERIFY(!f.controller.active());
    }
    void missingUpowerRequiresExplicitOverride() {
        Fixture f; f.platform.snapshot.battery = Power::Unknown; QVERIFY(!f.start().value("ok").toBool());
        QVERIFY(f.start(60, {{"endFloor", 0}}).value("ok").toBool()); f.controller.pollPower(); f.controller.pollPower(); QVERIFY(f.controller.active());
    }
    void desktopDoesNotRequireBattery() {
        Fixture f; f.platform.snapshot.battery = Power::None; QVERIFY(f.start().value("ok").toBool());
    }
    void upowerParsingRejectsStaleAndPeripheralData() {
        const qint64 now = 10000;
        QVariantMap battery{{"Type", uint(2)}, {"PowerSupply", true}, {"IsPresent", true}, {"Percentage", 65.0}, {"UpdateTime", qulonglong(now)}};
        QVariantMap root{{"OnBattery", true}};
        QCOMPARE(interpretPower(root, {battery}, now).percent, 65.0);
        auto second = battery; second["Percentage"] = 20.0;
        QCOMPARE(interpretPower(root, {battery, second}, now).percent, 20.0);
        second["PowerSupply"] = false;
        QCOMPARE(interpretPower(root, {battery, second}, now).percent, 65.0);
        QCOMPARE(interpretPower({{"OnBattery", false}}, {second}, now).battery, Power::None);
        QCOMPARE(interpretPower(root, {second}, now).battery, Power::Unknown);
        battery["UpdateTime"] = qulonglong(now - 181);
        QCOMPARE(interpretPower(root, {battery}, now).percent, -1.0);
        battery["UpdateTime"] = QVariant::fromValue(std::numeric_limits<qulonglong>::max());
        QCOMPARE(interpretPower(root, {battery}, now).percent, -1.0);
        battery["UpdateTime"] = qulonglong(now); battery["Percentage"] = 101;
        QCOMPARE(interpretPower(root, {battery}, now).percent, -1.0);
        battery.remove("PowerSupply"); QCOMPARE(interpretPower(root, {battery}, now).battery, Power::Unknown);
        QVERIFY(!interpretPower({}, {}, now).acKnown);
        QVERIFY(interpretPower({}, {}, now).onBattery);
    }
    void unknownAcCannotBypassCutoff() {
        Fixture f; f.platform.snapshot.acKnown = false; QVERIFY(!f.start().value("ok").toBool());
        f.platform.snapshot.acKnown = true; QVERIFY(f.start().value("ok").toBool()); f.platform.snapshot.acKnown = false;
        f.controller.pollPower(); f.controller.pollPower(); QVERIFY(!f.controller.active());
    }
    void saverHoldRespectsManualOverride() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 30; QVERIFY(f.start().value("ok").toBool());
        QVERIFY(f.platform.held); QCOMPARE(f.platform.saverAcquisitions, 1);
        f.platform.held = false; f.controller.pollPower(); f.controller.pollPower(); QCOMPARE(f.platform.saverAcquisitions, 1);
        f.platform.snapshot.onBattery = false; f.controller.pollPower(); f.platform.snapshot.onBattery = true; f.controller.pollPower(); QCOMPARE(f.platform.saverAcquisitions, 2);
    }
    void saverOptionalAndOffByChoice() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 30; f.platform.saverError = "unavailable";
        QVERIFY(f.start().value("ok").toBool()); QVERIFY(f.controller.active()); QVERIFY(!f.controller.status().value("warnings").toArray().isEmpty());
        f.controller.pollPower(); QCOMPARE(f.platform.saverAcquisitions, 1); f.controller.request("end");
        QVERIFY(f.start(60, {{"powerSaver", false}, {"lid", false}}).value("ok").toBool()); QCOMPARE(f.platform.events.last(), "sleep"); QCOMPARE(f.platform.saverAcquisitions, 1);
    }
    void saverReleaseIsRetried() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 30; QVERIFY(f.start().value("ok").toBool());
        f.platform.releaseError = "provider busy"; f.controller.request("end"); QVERIFY(f.platform.held); QCOMPARE(f.platform.writeFd, -1);
        f.platform.releaseError.clear(); f.controller.pollPower(); QVERIFY(!f.platform.held);
    }
    void saverReleaseRetriesDuringActiveSession() {
        Fixture f; f.platform.snapshot.onBattery = true; f.platform.snapshot.percent = 30;
        QVERIFY(f.start().value("ok").toBool()); QVERIFY(f.platform.held);
        f.platform.snapshot.onBattery = false; f.platform.releaseError = "temporarily unavailable";
        f.controller.pollPower(); QVERIFY(f.platform.held); QVERIFY(f.controller.active());
        QVERIFY(!f.controller.status().value("warnings").toArray().isEmpty());
        f.platform.releaseError.clear(); f.controller.pollPower();
        QVERIFY(!f.platform.held); QVERIFY(f.controller.active());
        QVERIFY(f.controller.status().value("warnings").toArray().isEmpty());
    }
    void disconnectedPowerDevicesNeverClearEvidence() {
        Fixture f; QVERIFY(f.start().value("ok").toBool()); f.platform.snapshot.battery = Power::Unknown;
        f.controller.pollPower(); QVERIFY(f.controller.active()); f.controller.pollPower(); QVERIFY(!f.controller.active()); QVERIFY(!f.journal.saved.value("active").toBool());
    }
    void endingReleasesSleepBeforeOptionalCleanup() {
        Fixture f; QVERIFY(f.start().value("ok").toBool());
        bool observed = false;
        f.platform.beforeProfileRelease = [&] {
            observed = true; QCOMPARE(f.platform.writeFd, -1); QVERIFY(!f.guardian.armed);
        };
        f.controller.request("end"); QVERIFY(observed);
        f.platform.beforeProfileRelease = {};
    }
    void fiveMinuteNotificationOnce() {
        Fixture f; QVERIFY(f.start(600).value("ok").toBool()); const auto initial = f.platform.notifications;
        f.now += 300000; f.controller.tick(); QCOMPARE(f.platform.notifications, initial + 1); f.controller.tick(); QCOMPARE(f.platform.notifications, initial + 1);
    }
    void diskJournalRoundtripAndPermissions() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); DiskJournal j(temp.path() + "/private");
        const QJsonObject o{{"schema", 1}, {"active", true}, {"reason", "test"}}; QVERIFY(j.write(o).isEmpty());
        QString error; QCOMPARE(j.read(error), o); QVERIFY(error.isEmpty());
        struct stat info{}; QVERIFY(::stat(QFile::encodeName(j.path()).constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0600));
        QVERIFY(::stat(QFile::encodeName(temp.path() + "/private").constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0700));
        QVERIFY(j.write({{"schema", 1}, {"active", false}}).isEmpty()); QVERIFY(!j.read(error).value("active").toBool());
    }
    void corruptAndSymlinkJournalsRefused() {
        QTemporaryDir temp; DiskJournal j(temp.path()); QFile f(j.path()); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("broken"); f.close();
        QString error; QVERIFY(j.read(error).isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), QByteArray("broken")); f.close(); f.remove();
        QFile target(temp.path() + "/evidence"); QVERIFY(target.open(QIODevice::WriteOnly)); target.write("keep"); target.close(); QVERIFY(target.link(j.path()));
        error.clear(); QVERIFY(j.read(error).isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(!j.write({{"schema", 1}, {"active", false}}).isEmpty());
        QVERIFY(target.open(QIODevice::ReadOnly)); QCOMPARE(target.readAll(), QByteArray("keep"));
    }
    void supervisorKillsAnExpiredRealProcess() { runVictim("deadline", 4000); }
    void supervisorKillsHungRealProcess() { runVictim("hang", 14000); }
    void supervisorAcceptsExtension() { runVictim("extend", 7500); }
    void kernelSupervisorKillsExpiredProcess() { runVictim("kernel-deadline",4500); }
    void kernelSupervisorKillsHungProcess() { runVictim("kernel-hang",14000); }
    void kernelSupervisorAcceptsExtension() { runVictim("kernel-extend",7500); }
    void kernelSupervisorDisarmsOnStop() {
        QProcess process; process.start(QCoreApplication::applicationFilePath(), {"--victim","kernel-stop"});
        QVERIFY(process.waitForStarted()); QVERIFY2(process.waitForFinished(5000),process.readAllStandardError().constData());
        QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
        QVERIFY(process.readAllStandardOutput().contains("DISARMED-SURVIVED"));
    }
    void normalExpiryKeepsControllerAlive() {
        QProcess automatic,kernel;
        automatic.start(QCoreApplication::applicationFilePath(), {"--normal-controller"});
        kernel.start(QCoreApplication::applicationFilePath(), {"--normal-controller-kernel"});
        QVERIFY(automatic.waitForStarted()); QVERIFY(kernel.waitForStarted());
        for(auto *process : {&automatic,&kernel}) {
            QVERIFY2(process->state()==QProcess::NotRunning || process->waitForFinished(65000),process->readAllStandardError().constData());
            QCOMPARE(process->exitStatus(),QProcess::NormalExit); QCOMPARE(process->exitCode(),0);
            QVERIFY(process->readAllStandardOutput().contains("NORMAL-EXPIRY-RELEASED"));
        }
    }
    void diskJournalExcludesSecondOwner() {
        QTemporaryDir temp; DiskJournal first(temp.path()), second(temp.path());
        QVERIFY(first.write({{"schema", 1}, {"active", false}}).isEmpty());
        QVERIFY(!second.write({{"schema", 1}, {"active", true}}).isEmpty());
    }
    void symlinkLockRefused() {
        QTemporaryDir temp; QFile target(temp.path() + "/evidence"); QVERIFY(target.open(QIODevice::WriteOnly)); target.write("keep"); target.close();
        QVERIFY(target.link(temp.path() + "/controller.lock")); DiskJournal j(temp.path()); QString error;
        QVERIFY(j.read(error).isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(!j.write({{"schema", 1}, {"active", false}}).isEmpty());
    }
    void uiStartExtendEnd() {
        Fixture f;
        Window window([&](const QString &action, const QJsonObject &args) { auto r = f.controller.request(action == "connect" ? "status" : action, args); r.insert("connected", true); return r; }, true);
        window.show(); navigate(window, 1); auto *start = window.findChild<QPushButton *>("primary"); auto *end = window.findChild<QPushButton *>("end");
        QVERIFY(start); QVERIFY(end); QVERIFY(!end->isEnabled());
        window.findChild<QSpinBox *>("hours")->setValue(1); QTest::mouseClick(start, Qt::LeftButton); QVERIFY(f.controller.active()); QVERIFY(end->isEnabled());
        const auto before = f.controller.status().value("remainingSeconds").toInteger(); navigate(window,1); QTest::mouseClick(start, Qt::LeftButton);
        QCOMPARE(f.controller.status().value("remainingSeconds").toInteger(), before + 3600);
        navigate(window,0); window.grab().save("ui-active.png"); QTest::mouseClick(end, Qt::LeftButton); QVERIFY(!f.controller.active());
        window.grab().save("ui-idle.png");
    }
    void uiErrorsRemainVisible() {
        Window window([](const QString &, const QJsonObject &) { return QJsonObject{{"connected", false}, {"ok", false}, {"error", "System D-Bus is unavailable. Reconnect after the service becomes available."}}; }, true);
        window.show(); QVERIFY(!window.findChild<QPushButton *>("primary")->isEnabled()); QVERIFY(window.findChild<QLabel *>("error")->isVisible()); window.grab().save("ui-error.png");
    }
    void tiledAndShortWindowsKeepOptionsUsable() {
        Window window([](const QString &, const QJsonObject &) { return QJsonObject{{"connected", true}, {"ok", true}}; }, true);
        window.resize(1100, 540); window.show(); QTest::qWait(50);
        auto *scroll = window.findChild<QScrollArea *>(); QVERIFY(scroll);
        auto *bar = scroll->verticalScrollBar(); QVERIFY(bar->maximum() > 0);
        QWheelEvent wheel(QPointF(100, 100), scroll->viewport()->mapToGlobal(QPoint(100, 100)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(scroll->viewport(), &wheel); QVERIFY(bar->value() > 0);
        bar->setValue(0); scroll->setFocus(); const auto before = bar->value(); QTest::keyClick(scroll, Qt::Key_PageDown); QVERIFY(bar->value() > before);
        window.grab().save("ui-tiled.png");
        window.resize(380, 500); QTest::qWait(50); QVERIFY(bar->maximum() > 0);
        navigate(window,3); QTest::qWait(30); auto *target = window.findChild<QSpinBox *>("lowPower"); QVERIFY(target);
        scroll->ensureWidgetVisible(target); QTest::qWait(20);
        QVERIFY(scroll->viewport()->rect().contains(target->mapTo(scroll->viewport(), target->rect().center())));
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        window.grab().save("ui-short.png");
        window.resize(360, 340); QTest::qWait(30);
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        scroll->ensureWidgetVisible(target); QTest::qWait(20);
        QVERIFY(scroll->viewport()->rect().contains(target->mapTo(scroll->viewport(), target->rect().center())));
    }
    void numericEditorsHaveReadableGeometry() {
        Window window([](const QString &, const QJsonObject &) { return QJsonObject{{"connected", true}, {"ok", true}}; }, true);
        window.show(); QTest::qWait(20);
        for (auto *spin : window.findChildren<QSpinBox *>()) {
            navigate(window, spin->objectName()=="cutoff" || spin->objectName()=="lowPower" ? 3 : 1);
            spin->setFocus(); QCoreApplication::processEvents();
            auto *edit = spin->findChild<QLineEdit *>(); QVERIFY(edit);
            QVERIFY2(edit->height() >= edit->fontMetrics().height(), qPrintable(QString("%1: spin %2x%3, editor %4x%5 font %6 text %7").arg(spin->objectName()).arg(spin->width()).arg(spin->height()).arg(edit->width()).arg(edit->height()).arg(edit->fontMetrics().height()).arg(edit->text())));
            QVERIFY(!edit->text().isEmpty());
            QVERIFY(edit->width() >= edit->fontMetrics().horizontalAdvance(edit->text()));
        }
    }
    void wheelOverDurationScrollsWithoutChangingValue() {
        Window window([](const QString &, const QJsonObject &) { return QJsonObject{{"connected", true}, {"ok", true}}; }, true);
        window.resize(380, 340); window.show(); navigate(window,1); QTest::qWait(30);
        auto *spin = window.findChild<QSpinBox *>("hours");
        auto *scroll = window.findChild<QScrollArea *>();
        scroll->ensureWidgetVisible(spin); const auto before = scroll->verticalScrollBar()->value();
        const auto value = spin->value(); spin->setFocus();
        QWheelEvent wheel(spin->rect().center(), spin->mapToGlobal(spin->rect().center()), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(spin, &wheel);
        QCOMPARE(spin->value(), value); QVERIFY(scroll->verticalScrollBar()->value() > before);
    }
    void slowControllerDoesNotBlockScrolling() {
        std::atomic<int> calls{0};
        Window window([&](const QString &, const QJsonObject &) {
            ++calls; QThread::msleep(300); return QJsonObject{{"connected", true}, {"ok", true}};
        }, true, true);
        window.resize(380, 500); window.show();
        bool responsive = false; QTimer::singleShot(30, [&] { responsive = true; });
        QTRY_VERIFY_WITH_TIMEOUT(responsive, 150);
        auto *scroll = window.findChild<QScrollArea *>();
        scroll->setFocus(); QTest::keyClick(scroll, Qt::Key_PageDown);
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
        window.refresh(); window.refresh(); QCOMPARE(calls.load(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton *>("primary")->isEnabled(), 1000);
    }
    void validationSurvivesStatusRefresh() {
        Window window([](const QString &, const QJsonObject &) { return QJsonObject{{"connected", true}, {"ok", true}}; }, true);
        window.show(); navigate(window,1); window.findChild<QSpinBox *>("hours")->setValue(0);
        QTest::mouseClick(window.findChild<QPushButton *>("primary"), Qt::LeftButton);
        auto *error = window.findChild<QLabel *>("error"); QVERIFY(error->isVisible());
        const auto message = error->text(); window.refresh(); QCOMPARE(error->text(), message); QVERIFY(error->isVisible());
    }
    void externalSessionOptionsAppearInWindow() {
        QJsonObject reply{{"connected", true}, {"ok", true}};
        Window window([&](const QString &, const QJsonObject &) { return reply; }, true);
        reply.insert("active", true); reply.insert("remainingSeconds", 60);
        reply.insert("options", QJsonObject{{"lid", false}, {"powerSaver", false}, {"endFloor", 15}, {"lowPowerFloor", 50}});
        window.refresh();
        QVERIFY(!window.findChild<QCheckBox *>("lidSwitch")->isChecked());
        QCOMPARE(window.findChild<QSpinBox *>("cutoff")->value(), 15);
        QVERIFY(!window.findChild<QSpinBox *>("cutoff")->isEnabled());
    }
    void dashboardToggleAndExtensionControlWork() {
        Fixture f;
        Window window([&](const QString &action,const QJsonObject &args){ auto r=f.controller.request(action,args); r.insert("connected",true); return r; },true);
        window.show(); auto *toggle=window.findChild<QCheckBox *>("sessionToggle");
        QTest::mouseClick(toggle,Qt::LeftButton); QVERIFY(f.controller.active()); QVERIFY(toggle->isChecked());
        auto *preset=window.findChild<QComboBox *>("durationPreset");
        const auto before=f.controller.status().value("remainingSeconds").toInteger();
        preset->setCurrentIndex(0); QMetaObject::invokeMethod(preset,"activated",Q_ARG(int,0));
        QCOMPARE(f.controller.status().value("remainingSeconds").toInteger(),before+900);
        QTest::mouseClick(toggle,Qt::LeftButton); QVERIFY(!f.controller.active()); QVERIFY(!toggle->isChecked());
    }
    void dashboardDurationMatchesCustomSessionFields() {
        Window window([](const QString &,const QJsonObject &){ return QJsonObject{{"connected",true},{"ok",true}}; },true);
        window.show(); navigate(window,1); window.findChild<QSpinBox *>("hours")->setValue(2);
        auto *picker=window.findChild<QComboBox *>("durationPreset"); QCOMPARE(picker->currentData().toInt(),7200);
        window.findChild<QSpinBox *>("minutes")->setValue(17); QCOMPARE(picker->currentData().toInt(),0);
        navigate(window,0); QVERIFY(picker->currentText().contains("Custom"));
    }
    void narrowNavigationReachesEveryPageWithKeyboard() {
        Window window([](const QString &,const QJsonObject &){ return QJsonObject{{"connected",true},{"ok",true}}; },true);
        window.resize(380,500); window.show(); QTest::qWait(30);
        auto *picker=window.findChild<QComboBox *>("pagePicker"); QVERIFY(picker->isVisible()); picker->setFocus();
        auto *stack=window.findChild<QStackedWidget *>("pages");
        for(int index=1;index<4;++index) { QTest::keyClick(picker,Qt::Key_Down); QCOMPARE(stack->currentIndex(),index); }
        QVERIFY(window.findChild<QSpinBox *>("cutoff")->isVisible());
        QTest::keyClick(picker,Qt::Key_Home); QCOMPARE(stack->currentIndex(),0);
    }
    void failedStartRestoresDashboardToggle() {
        Window window([](const QString &,const QJsonObject &){ return QJsonObject{{"connected",true},{"ok",false},{"error","Sleep inhibition denied"}}; },true);
        window.show(); auto *toggle=window.findChild<QCheckBox *>("sessionToggle");
        QTest::mouseClick(toggle,Qt::LeftButton); QVERIFY(!toggle->isChecked());
        QVERIFY(window.findChild<QLabel *>("error")->isVisible());
        QVERIFY(!window.findChild<QPushButton *>("end")->isEnabled());
    }
    void compactPanelCustomDurationStartExtendAndValidation() {
        QStringList actions; QJsonObject submitted; bool active=false;
        Window window([&](const QString &action,const QJsonObject &params){
            if(action=="start" || action=="extend") { actions.append(action); submitted=params; active=true; }
            return QJsonObject{{"connected",true},{"ok",true},{"active",active},{"remainingSeconds",active?3600:0}};
        });
        window.OpenQuickPanel(); QCoreApplication::processEvents();
        QWidget *panel=nullptr;
        for(auto *candidate : QApplication::topLevelWidgets()) if(candidate->objectName()=="quickPanel") panel=candidate;
        QVERIFY(panel); QVERIFY(panel->isVisible()); QVERIFY(!window.isVisible());
        auto *days=panel->findChild<QSpinBox *>("quickDays");
        auto *hours=panel->findChild<QSpinBox *>("quickHours");
        auto *minutes=panel->findChild<QSpinBox *>("quickMinutes");
        auto *submit=panel->findChild<QPushButton *>("quickCustom");
        QVERIFY(days && hours && minutes && submit);
        days->setValue(1); hours->setValue(2); minutes->setValue(3); submit->click();
        QCOMPARE(actions,QStringList{"start"}); QCOMPARE(submitted.value("seconds").toInteger(),93780LL);
        QVERIFY(submitted.contains("lid")); QVERIFY(submitted.contains("endFloor"));
        days->setValue(0); hours->setValue(0); minutes->setValue(15); submit->click();
        QCOMPARE(actions.last(),QString("extend")); QCOMPARE(submitted.value("seconds").toInteger(),900LL);
        const auto count=actions.size(); minutes->setValue(0); submit->click(); QCOMPARE(actions.size(),count);
        QVERIFY(panel->findChild<QLabel *>("quickDetail")->text().contains("1 minute"));
        days->setValue(30); hours->setValue(1); submit->click(); QCOMPARE(actions.size(),count);
        QVERIFY(!window.isVisible());
    }
    void referenceDashboardFitsDefaultWindow() {
        Window window([](const QString &,const QJsonObject &){ return QJsonObject{{"connected",true},{"ok",true},{"active",true},{"remainingSeconds",9258}}; },true);
        window.show(); QTest::qWait(30);
        auto *scroll=window.findChild<QScrollArea *>();
        QCOMPARE(scroll->verticalScrollBar()->maximum(),0);
        for(int index=0;index<5;++index) { navigate(window,index); window.grab().save(QString("evereye-page-%1.png").arg(index)); }
    }
private:
    static void navigate(Window &window,int index) {
        auto *picker=window.findChild<QComboBox *>("pagePicker");
        if(picker->isVisible()) { picker->setCurrentIndex(index); QMetaObject::invokeMethod(picker,"activated",Q_ARG(int,index)); }
        else QTest::mouseClick(window.findChild<QPushButton *>("nav"+QString::number(index)),Qt::LeftButton);
        QCoreApplication::processEvents();
    }
    void runVictim(const QString &mode, int timeout) {
        QProcess process; process.setProcessChannelMode(QProcess::SeparateChannels); process.start(QCoreApplication::applicationFilePath(), {"--victim", mode});
        QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(timeout), process.readAllStandardError().constData());
        QCOMPARE(process.exitStatus(), QProcess::CrashExit); QCOMPARE(process.exitCode(), SIGKILL);
        const auto output = process.readAllStandardOutput();
        QVERIFY2(output.contains("ARMED"), process.readAllStandardError().constData());
        if (mode.endsWith("extend")) QVERIFY(output.contains("SURVIVED-ORIGINAL-DEADLINE"));
    }
};
int main(int argc, char **argv) {
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode == "--watchdog") { QCoreApplication app(argc, argv); return runGuardian(); }
    if (mode == "--normal-controller" || mode == "--normal-controller-kernel") {
        QCoreApplication app(argc, argv); FakePlatform platform; ProcessGuardian guardian(mode.endsWith("-kernel")); MemoryJournal journal;
        Controller controller(platform, guardian, journal);
        if (!controller.request("start", {{"seconds", 60}}).value("ok").toBool()) return 5;
        QTimer timer; timer.setInterval(200);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            controller.tick();
            if (!controller.active()) {
                if (controller.status().value("reason").toString() == "Timer expired" && platform.writeFd == -1 && !journal.saved.value("active").toBool()) {
                    constexpr char message[] = "NORMAL-EXPIRY-RELEASED\n";
                    app.exit(::write(STDOUT_FILENO, message, sizeof(message) - 1) == ssize_t(sizeof(message) - 1) ? 0 : 7);
                } else app.exit(6);
            }
        });
        timer.start(); return app.exec();
    }
    if (mode == "--victim") {
        QCoreApplication app(argc, argv); const QString requested = QString::fromLocal8Bit(argv[2]);
        ProcessGuardian guardian(requested.startsWith("kernel-")); const QString kind = requested.startsWith("kernel-") ? requested.mid(7) : requested;
        const auto end = bootMilliseconds() + (kind == "hang" ? 30000 : 1200);
        const auto error = guardian.arm(end); if (!error.isEmpty()) { fprintf(stderr, "%s\n", qPrintable(error)); return 5; }
        if (::write(STDOUT_FILENO, "ARMED\n", 6) != 6) return 7;
        if (kind == "extend") {
            QTimer::singleShot(500, [&] { if (!guardian.heartbeat(bootMilliseconds() + 3200).isEmpty()) app.exit(6); });
            QTimer::singleShot(3500, [&] {
                constexpr char message[] = "SURVIVED-ORIGINAL-DEADLINE\n";
                if (::write(STDOUT_FILENO, message, sizeof(message) - 1) != ssize_t(sizeof(message) - 1)) app.exit(7);
            });
        }
        if (kind == "stop") {
            QTimer::singleShot(200,[&]{ guardian.stop(); });
            QTimer::singleShot(4000,[&]{
                constexpr char message[]="DISARMED-SURVIVED\n";
                app.exit(::write(STDOUT_FILENO,message,sizeof(message)-1)==ssize_t(sizeof(message)-1)?0:7);
            });
        }
        return app.exec(); // Deliberately stops heartbeats; independent enforcement must kill us.
    }
    QApplication app(argc, argv); InsomniaTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_insomnia.moc"
