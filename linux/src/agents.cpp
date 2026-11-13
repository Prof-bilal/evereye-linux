#include "agents.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStringList>
#include <unistd.h>

namespace insomnia {
namespace {
QStringList defaultPatterns() {
    return {"claude", "codex", "aider", "opencode", "gemini", "goose", "cline",
            "cursor-agent", "copilot", "crush", "amp", "command-code", "commandcode",
            "windsurf", "continue", "qwen", "freebuff"};
}
QStringList patterns() {
    QStringList result = defaultPatterns();
    auto base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.config";
    QFile file(base + "/evereye/agents.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            const auto custom = document.object().value("patterns").toArray();
            if (!custom.isEmpty()) {
                result.clear();
                for (const auto &value : custom) if (value.isString()) result << value.toString().toLower();
            }
        }
    }
    return result;
}
QString joined(const QByteArray &raw) {
    QString text = QString::fromUtf8(raw);
    text.replace(QChar('\0'), ' ');
    return text.trimmed();
}
QString ttyFor(const QString &pid) {
    const auto target = QFileInfo(QString("/proc/%1/fd/0").arg(pid)).symLinkTarget();
    if (target.startsWith("/dev/pts/")) return target.mid(5);          // pts/3
    if (target.startsWith("/dev/tty") && target != "/dev/tty") return target.mid(5);  // tty1
    return {};
}
struct Parent {
    qint64 pid = 0, ppid = 0;
    QString command, comm, cwd, tty;
    QStringList tokens;
    char state = '?';
};
QString tmux(const QStringList &args, int timeout = 1500) {
    QProcess process;
    process.start("tmux", args);
    if (!process.waitForFinished(timeout)) { process.kill(); return {}; }
    return QString::fromUtf8(process.readAllStandardOutput());
}
}
QJsonObject scanAgents() {
    const auto wanted = patterns();
    QHash<qint64, Parent> processes;
    const qint64 self = getpid();
    QDir proc("/proc");
    for (const auto &entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok) continue;
        Parent parent;
        parent.pid = pid;
        QFile cmdline(QString("/proc/%1/cmdline").arg(pid));
        if (!cmdline.open(QIODevice::ReadOnly)) continue;
        const auto raw = cmdline.readAll();
        if (raw.isEmpty()) continue;
        parent.command = joined(raw);
        for (const auto &token : parent.command.split(' ', Qt::SkipEmptyParts)) parent.tokens << token;
        QFile comm(QString("/proc/%1/comm").arg(pid));
        if (comm.open(QIODevice::ReadOnly)) parent.comm = QString::fromUtf8(comm.readAll()).trimmed();
        QFile stat(QString("/proc/%1/stat").arg(pid));
        if (stat.open(QIODevice::ReadOnly)) {
            const auto text = QString::fromUtf8(stat.readAll());
            const auto open = text.indexOf('(');
            const auto close = text.lastIndexOf(')');
            if (open >= 0 && close > open) {
                const auto fields = text.mid(close + 2).split(' ', Qt::SkipEmptyParts);
                if (fields.size() > 1) {
                    parent.state = fields[0].isEmpty() ? '?' : fields[0].at(0).toLatin1();
                    parent.ppid = fields[1].toLongLong();
                }
            }
        }
        parent.cwd = QFileInfo(QString("/proc/%1/cwd").arg(pid)).symLinkTarget();
        parent.tty = ttyFor(entry);
        processes.insert(pid, parent);
    }

    QHash<QString, QString> ttyPane;
    QHash<qint64, QString> pidPane;
    const auto panes = tmux({"list-panes", "-a", "-F", "#{pane_id}\t#{pane_tty}\t#{pane_pid}"});
    for (const auto &line : panes.split('\n', Qt::SkipEmptyParts)) {
        const auto fields = line.split('\t');
        if (fields.size() < 3) continue;
        ttyPane.insert(fields[1], fields[0]);
        pidPane.insert(fields[2].toLongLong(), fields[0]);
    }
    const auto paneFor = [&](const Parent &parent, const QHash<qint64, Parent> &all) {
        if (ttyPane.contains("/dev/" + parent.tty)) return ttyPane.value("/dev/" + parent.tty);
        qint64 cursor = parent.pid;
        for (int depth = 0; depth < 12 && cursor > 1; ++depth) {
            if (pidPane.contains(cursor)) return pidPane.value(cursor);
            if (!all.contains(cursor)) break;
            cursor = all.value(cursor).ppid;
        }
        return QString();
    };

    QJsonArray agents;
    for (auto it = processes.cbegin(); it != processes.cend(); ++it) {
        const Parent &parent = it.value();
        if (parent.pid == self || parent.state == 'Z') continue;
        const auto command = parent.command.toLower();
        if (command.contains("evereye") || command.contains("insomnia-linux")) continue;
        QString matched;
        // Windows-style binaries (opencode.exe) match by their bare name.
        const auto bare = [](QString name) { name = name.toLower(); return name.endsWith(".exe") ? name.chopped(4) : name; };
        // v1 is for terminal agents: match the executable (or the script a runtime launches), not any argument.
        if (command.contains("claude-desktop") || command.contains("/.config/claude/")) continue;
        for (int i = 0; i < parent.tokens.size() && i < 2; ++i) {
            const auto base = bare(QFileInfo(parent.tokens[i]).fileName());
            if (wanted.contains(base)) { matched = base; break; }
        }
        if (matched.isEmpty() && wanted.contains(bare(parent.comm))) matched = bare(parent.comm);
        if (matched.isEmpty()) continue;
        const auto pane = paneFor(parent, processes);
        // Background helper daemons are not sessions you watch or task.
        if (command.contains("app-serve") || command.contains("app-server")) continue;
        QJsonObject agent;
        agent.insert("pid", parent.pid);
        agent.insert("ppid", parent.ppid);
        agent.insert("name", matched);
        agent.insert("command", parent.command);
        agent.insert("cwd", parent.cwd);
        agent.insert("tty", parent.tty);
        agent.insert("pane", pane);
        agent.insert("status", "Running");
        agents.append(agent);
    }
    return {{"ok", true}, {"agents", agents}};
}

namespace {
QJsonObject agentByPid(qint64 pid) {
    for (const auto &value : scanAgents().value("agents").toArray())
        if (value.toObject().value("pid").toInteger() == pid) return value.toObject();
    return {};
}
}

QJsonObject captureAgent(const QJsonObject &args) {
    auto pane = args.value("pane").toString();
    if (pane.isEmpty() && args.contains("pid")) pane = agentByPid(args.value("pid").toInteger()).value("pane").toString();
    if (pane.isEmpty()) return {{"ok", false}, {"error", "This session is not inside a tmux pane"}};
    const auto text = tmux({"capture-pane", "-p", "-t", pane, "-S", "-200"});
    if (text.isNull()) return {{"ok", false}, {"error", "Could not read the tmux pane"}};
    return {{"ok", true}, {"pane", pane}, {"pid", QString::number(args.value("pid").toInteger())}, {"text", text}};
}

QJsonObject sendAgent(const QJsonObject &args) {
    const auto text = args.value("text").toString();
    if (text.isEmpty()) return {{"ok", false}, {"error", "Task text is required"}};
    auto pane = args.value("pane").toString();
    auto tty = args.value("tty").toString();
    if (pane.isEmpty() && tty.isEmpty() && args.contains("pid")) {
        const auto agent = agentByPid(args.value("pid").toInteger());
        pane = agent.value("pane").toString();
        tty = agent.value("tty").toString();
    }
    if (!pane.isEmpty()) {
        const auto sent = tmux({"send-keys", "-t", pane, "-l", "--", text});
        if (sent.isNull()) return {{"ok", false}, {"error", "Could not type into the tmux pane"}};
        tmux({"send-keys", "-t", pane, "Enter"});
        return {{"ok", true}};
    }
    if (tty.isEmpty()) return {{"ok", false}, {"error", "This process has no terminal to type into"}};
    QFile device("/dev/" + tty);
    if (!device.open(QIODevice::WriteOnly | QIODevice::Unbuffered)) return {{"ok", false}, {"error", "Cannot open the process terminal"}};
    const auto bytes = (text + "\r").toUtf8();  // Enter in raw-mode TUIs
    if (device.write(bytes) != bytes.size()) return {{"ok", false}, {"error", "Could not write to the process terminal"}};
    return {{"ok", true}};
}
}
