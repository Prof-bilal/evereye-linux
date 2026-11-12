#include "terminal.h"
#include <QFileInfo>
#include <QStandardPaths>

namespace insomnia {
namespace {
TerminalLaunch named(const QString &program, const QStringList &options, const QStringList &full) {
    return {program, options + full, true};
}
}

TerminalLaunch terminalCommand(const QString &binary, const QStringList &command, const QString &cwd,
                               const QString &title, const QString &preferred, ExecutableLookup lookup) {
    if (!lookup) lookup = [](const QString &name) { return QStandardPaths::findExecutable(name); };
    const QStringList full = QStringList{binary} + command;
    const auto resolvePreferred = [&]() -> QString {
        if (preferred.isEmpty()) return {};
        const QFileInfo info(preferred);
        if (info.isAbsolute()) return info.isExecutable() ? preferred : QString();
        return lookup(preferred);
    };
    const auto launchFor = [&](const QString &program) -> TerminalLaunch {
        if (program.isEmpty()) return {};
        const auto name = QFileInfo(program).fileName();
        if (name == "xdg-terminal-exec") return named(program, {"--dir=" + cwd, "--title=" + title, "--"}, full);
        if (name == "foot") return named(program, {"-T", title, "-D", cwd}, full);
        if (name == "kitty") return named(program, {"--directory", cwd, "--title", title}, full);
        if (name == "alacritty") return named(program, {"--working-directory", cwd, "--title", title, "-e"}, full);
        if (name == "wezterm") return named(program, {"start", "--cwd", cwd, "--"}, full);
        if (name == "konsole") return named(program, {"--workdir", cwd, "-e"}, full);
        if (name == "gnome-terminal") return named(program, {"--working-directory", cwd, "--title", title, "--"}, full);
        if (name == "xterm") return named(program, {"-T", title, "-e"}, full);
        if (name == "omarchy-launch-terminal") return named(program, {}, full);
        return {};
    };

    // An explicit terminal in $TERMINAL wins when we know how to drive it.
    if (auto launch = launchFor(resolvePreferred()); launch.ok) return launch;
    // The cross-desktop launcher understands --dir/--title on every desktop.
    if (auto launch = launchFor(lookup("xdg-terminal-exec")); launch.ok) return launch;
    for (const auto &candidate : {QString("foot"), QString("kitty"), QString("alacritty"), QString("wezterm"),
                                  QString("konsole"), QString("gnome-terminal"), QString("xterm"),
                                  QString("omarchy-launch-terminal")}) {
        if (auto launch = launchFor(lookup(candidate)); launch.ok) return launch;
    }
    return {};
}
}
