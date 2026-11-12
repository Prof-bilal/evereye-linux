#pragma once
#include <QWidget>

class QWebChannel;
class QWebEngineView;

namespace insomnia {
class TerminalBridge;
class TerminalView final : public QWidget {
    Q_OBJECT
public:
    explicit TerminalView(QWidget *parent = nullptr);
    void attach(const QString &jobId);
signals:
    void statusChanged(const QString &status);
private:
    QWebEngineView *view = nullptr;
    QWebChannel *channel = nullptr;
    TerminalBridge *bridge = nullptr;
};
}
