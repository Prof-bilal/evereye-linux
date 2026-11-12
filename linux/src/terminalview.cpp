#include "terminalview.h"
#include "jobs.h"
#include <QLocalSocket>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtEndian>

namespace insomnia {
// Bridges xterm.js to a job's attach socket: bytes come back base64 encoded so
// a split UTF-8 sequence is decoded by xterm rather than mangled by QString.
class TerminalBridge final : public QObject {
    Q_OBJECT
public:
    explicit TerminalBridge(QObject *parent = nullptr) : QObject(parent) {
        connect(&socket, &QLocalSocket::connected, this, [this] {
            emit status(QStringLiteral("connected"));
            sendSize(80, 24);
        });
        connect(&socket, &QLocalSocket::readyRead, this, [this] {
            pending += socket.readAll();
            if (pending.size() > 512 * 1024) pending = pending.right(512 * 1024);
            flush();
        });
        connect(&socket, &QLocalSocket::disconnected, this, [this] { emit status(QStringLiteral("closed")); });
        connect(&socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { emit status(QStringLiteral("unavailable")); });
    }
    void attach(const QString &jobId) {
        socket.abort();
        pending.clear();
        emit reset();
        emit status(QStringLiteral("connecting"));
        socket.connectToServer(jobSocketPath(jobId));
    }
signals:
    void output(const QString &base64);
    void status(const QString &text);
    void reset();
public slots:
    void setReady() { ready = true; flush(); }
    void sendInput(const QString &data) {
        if (socket.state() == QLocalSocket::ConnectedState && !data.isEmpty()) sendFrame(1, data.toUtf8());
    }
    void resize(int columns, int rows) { sendSize(columns, rows); }
private:
    void flush() {
        if (!ready || pending.isEmpty()) return;
        emit output(QString::fromLatin1(pending.toBase64()));
        pending.clear();
    }
    void sendFrame(quint8 type, const QByteArray &payload) {
        QByteArray frame;
        frame.append(char(type));
        char header[4];
        qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(header));
        frame.append(header, 4);
        frame.append(payload);
        socket.write(frame);
        socket.flush();
    }
    void sendSize(int columns, int rows) {
        if (socket.state() != QLocalSocket::ConnectedState) return;
        QByteArray size(4, '\0');
        qToBigEndian<quint16>(quint16(qBound(1, columns, 65535)), reinterpret_cast<uchar *>(size.data()));
        qToBigEndian<quint16>(quint16(qBound(1, rows, 65535)), reinterpret_cast<uchar *>(size.data()) + 2);
        sendFrame(2, size);
    }
    QLocalSocket socket;
    QByteArray pending;
    bool ready = false;
};

TerminalView::TerminalView(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view = new QWebEngineView(this);
    view->setMinimumHeight(280);
    view->setContextMenuPolicy(Qt::NoContextMenu);
    view->page()->setBackgroundColor(QColor("#0e1416"));
    bridge = new TerminalBridge(this);
    channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("bridge"), bridge);
    view->page()->setWebChannel(channel);
    layout->addWidget(view);
    connect(bridge, &TerminalBridge::status, this, &TerminalView::statusChanged);
    view->load(QUrl(QStringLiteral("qrc:/terminal/index.html")));
}

void TerminalView::attach(const QString &jobId) {
    if (jobId.isEmpty()) return;
    bridge->attach(jobId);
    view->setFocus();
}
}
#include "terminalview.moc"
