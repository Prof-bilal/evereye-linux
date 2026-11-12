#pragma once
#include <QJsonObject>

namespace insomnia {
// Find AI agent processes owned by this user, with tmux pane mapping when the
// agent runs inside tmux. `captureAgent` reads a pane's recent output and
// `sendAgent` types into the pane (or the agent's tty as a fallback).
QJsonObject scanAgents();
QJsonObject captureAgent(const QJsonObject &args);
QJsonObject sendAgent(const QJsonObject &args);
}
