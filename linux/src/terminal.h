#pragma once
#include <QString>
#include <QStringList>
#include <functional>

namespace insomnia {
struct TerminalLaunch {
    QString program;
    QStringList args;
    bool ok = false;
};
using ExecutableLookup = std::function<QString(const QString &)>;

// Build the argv to open a terminal emulator running `binary command...`.
// Prefers $TERMINAL / xdg-terminal-exec, then known terminals. `lookup` is
// injectable for tests and defaults to QStandardPaths::findExecutable.
TerminalLaunch terminalCommand(const QString &binary, const QStringList &command, const QString &cwd,
                               const QString &title, const QString &preferred = {},
                               ExecutableLookup lookup = {});
}
