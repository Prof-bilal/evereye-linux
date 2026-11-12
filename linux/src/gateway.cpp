#include "gateway.h"
#include "jobs.h"
#include "service.h"
#include "websocket.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QtEndian>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <cstring>
#include <unistd.h>

namespace insomnia {
namespace {
constexpr int HeaderLimit = 8192;
constexpr int BodyLimit = 65536;
constexpr quint16 DefaultPort = 8737;

bool equalToken(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size()) return false;
    volatile unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i) diff = uchar(diff | uchar(a[i] ^ b[i]));
    return diff == 0;
}

bool loopback(const QString &bind) {
    return bind == "127.0.0.1" || bind == "::1" || bind == "localhost";
}

QString configDirectory() {
    auto base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.config";
    return base + "/evereye";
}

QString dataDirectory() {
    auto base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.local/share";
    const auto user = base + "/evereye/web";
    for (const auto &candidate : {user, QString("/usr/local/share/evereye/web"), QString("/usr/share/evereye/web")})
        if (QFile::exists(candidate + "/index.html")) return candidate;
    return user;
}

QByteArray randomToken() {
    static const char hex[] = "0123456789abcdef";
    QByteArray out(64, '0');
    for (int i = 0; i < out.size(); ++i) out[i] = hex[QRandomGenerator::global()->bounded(16)];
    return out;
}

class TlsServer final : public QTcpServer {
public:
    QSslCertificate cert;
    QSslKey key;
    bool tls = false;
protected:
    void incomingConnection(qintptr fd) override {
        if (!tls) { QTcpServer::incomingConnection(fd); return; }
        auto *ssl = new QSslSocket(this);
        if (!ssl->setSocketDescriptor(fd)) { ssl->deleteLater(); return; }
        ssl->setLocalCertificate(cert);
        ssl->setPrivateKey(key);
        ssl->startServerEncryption();
        addPendingConnection(ssl);
    }
};

struct GatewayState {
    QByteArray token;
    QString bind = "127.0.0.1";
    quint16 port = DefaultPort;
    bool bindGiven = false;
    bool portGiven = false;
    bool arbitrary = false;
    bool tls = false;
    QJsonArray notify;
    QString webRoot;
    QSslCertificate cert;
    QSslKey key;
    QNetworkAccessManager *http = nullptr;
};

QByteArray jsonBytes(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

void writeHttp(QTcpSocket *socket, int status, const QByteArray &ctype, const QByteArray &body, bool keep = false) {
    QByteArray reason = status == 200 ? "OK" : status == 204 ? "No Content" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden"
        : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : status == 413 ? "Payload Too Large"
        : status == 400 ? "Bad Request" : "Error";
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    head += "Content-Type: " + ctype + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Access-Control-Allow-Origin: *\r\n";
    head += "Access-Control-Allow-Headers: authorization, content-type\r\n";
    head += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += keep ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    socket->write(head);
    socket->write(body);
    if (!keep) QObject::connect(socket, &QTcpSocket::bytesWritten, socket, [socket] {
        if (socket->bytesToWrite() == 0) socket->disconnectFromHost();
    });
}

void writeJson(QTcpSocket *socket, int status, const QJsonObject &o) {
    writeHttp(socket, status, "application/json; charset=utf-8", jsonBytes(o) + '\n');
}

QJsonObject controller(const QString &action, const QJsonObject &args = {}) {
    return callController(action, args, true);
}

// One browser xterm.js view bridged to a running job's attach socket.
class WsTerminal final : public QObject {
public:
    WsTerminal(QTcpSocket *s, const QString &id, const QByteArray &initial, QObject *parent = nullptr)
        : QObject(parent), socket(s), jobId(id) {
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, &WsTerminal::onClientData);
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this] {
            local.abort();
            deleteLater();
        });
        QObject::connect(&local, &QLocalSocket::readyRead, this, &WsTerminal::onLocalData);
        QObject::connect(&local, &QLocalSocket::disconnected, this, [this] {
            sendClose(1000);
            socket->disconnectFromHost();
        });
        parser.append(initial);
        local.connectToServer(jobSocketPath(jobId));
        if (!local.waitForConnected(2000)) {
            sendClose(1011);
            socket->flush();
            socket->disconnectFromHost();
            return;
        }
        processClientFrames();
    }
private:
    QTcpSocket *socket;
    QLocalSocket local;
    QString jobId;
    WebSocketParser parser;

    void sendFrame(int opcode, const QByteArray &payload) { socket->write(webSocketFrame(opcode, payload)); }
    void sendClose(int code) {
        QByteArray payload(2, '\0');
        payload[0] = char((code >> 8) & 0xff);
        payload[1] = char(code & 0xff);
        sendFrame(0x8, payload);
    }
    void sendAttach(quint8 type, const QByteArray &payload) {
        QByteArray frame;
        frame.append(char(type));
        char header[4];
        qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(header));
        frame.append(header, 4);
        frame.append(payload);
        local.write(frame);
        local.flush();
    }
    void onLocalData() {
        const auto data = local.readAll();
        for (int offset = 0; offset < data.size(); offset += 32768)
            sendFrame(0x2, data.mid(offset, 32768));
    }
    void onClientData() {
        parser.append(socket->readAll());
        processClientFrames();
    }
    void processClientFrames() {
        while (true) {
            QByteArray payload;
            int opcode = 0;
            bool fatal = false;
            if (!parser.next(payload, opcode, fatal)) {
                if (fatal) sendClose(1002);
                if (fatal) socket->disconnectFromHost();
                return;
            }
            if (opcode == 0x2) {
                if (local.state() == QLocalSocket::ConnectedState) sendAttach(1, payload);
            } else if (opcode == 0x1) {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(payload, &error);
                if (error.error == QJsonParseError::NoError && document.isObject()) {
                    const auto object = document.object();
                    if (object.value("type").toString() == "resize") {
                        QByteArray size(4, '\0');
                        qToBigEndian<quint16>(quint16(object.value("cols").toInt(80)), reinterpret_cast<uchar *>(size.data()));
                        qToBigEndian<quint16>(quint16(object.value("rows").toInt(24)), reinterpret_cast<uchar *>(size.data()) + 2);
                        sendAttach(2, size);
                    }
                }
            } else if (opcode == 0x9) {
                sendFrame(0xA, payload);
            } else if (opcode == 0x8) {
                local.abort();
                socket->disconnectFromHost();
                return;
            }
        }
    }
};

class Client final : public QObject {
public:
    Client(QTcpSocket *s, GatewayState *st, QObject *parent = nullptr) : QObject(parent), socket(s), state(st) {
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, &Client::onRead);
        QObject::connect(socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
        QTimer::singleShot(15000, this, [this] { if (!complete) socket->disconnectFromHost(); });
    }
private:
    QTcpSocket *socket;
    GatewayState *state;
    QByteArray buffer;
    bool complete = false;
    bool streaming = false;
    QString streamId;
    qint64 streamOffset = 0;
    QTimer *pump = nullptr;

    void onRead() {
        if (complete) { socket->readAll(); return; }
        buffer += socket->readAll();
        if (buffer.size() > HeaderLimit + BodyLimit) { writeHttp(socket, 413, "text/plain", "too large\n"); complete = true; return; }
        const auto headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (buffer.size() > HeaderLimit) { writeHttp(socket, 413, "text/plain", "headers too large\n"); complete = true; }
            return;
        }
        if (headerEnd > HeaderLimit) { writeHttp(socket, 413, "text/plain", "headers too large\n"); complete = true; return; }
        const auto head = buffer.left(headerEnd);
        const auto lines = head.split('\n');
        if (lines.isEmpty()) { writeHttp(socket, 400, "text/plain", "bad request\n"); complete = true; return; }
        auto request = lines.first().trimmed();
        const auto parts = request.split(' ');
        if (parts.size() < 2) { writeHttp(socket, 400, "text/plain", "bad request\n"); complete = true; return; }
        const auto method = parts[0];
        QByteArray target = parts[1];
        const auto qpos = target.indexOf('?');
        if (qpos >= 0) target = target.left(qpos);
        QByteArray auth, type, upgrade, websocketKey, websocketProtocols;
        qint64 length = 0;
        bool sawLength = false;
        for (int i = 1; i < lines.size(); ++i) {
            auto line = lines[i].trimmed();
            const auto colon = line.indexOf(':');
            if (colon < 0) continue;
            const auto name = line.left(colon).toLower();
            auto value = line.mid(colon + 1).trimmed();
            if (name == "authorization") auth = value;
            else if (name == "upgrade") upgrade = value.toLower();
            else if (name == "sec-websocket-key") websocketKey = value;
            else if (name == "sec-websocket-protocol") websocketProtocols = value;
            else if (name == "content-length") {
                if (sawLength) { writeHttp(socket, 400, "text/plain", "duplicate length\n"); complete = true; return; }
                sawLength = true;
                bool ok = false; length = value.toLongLong(&ok);
                if (!ok || length < 0 || length > BodyLimit) { writeHttp(socket, 413, "text/plain", "bad length\n"); complete = true; return; }
            } else if (name == "content-type") type = value.toLower();
            else if (name == "transfer-encoding") { writeHttp(socket, 400, "text/plain", "transfer-encoding unsupported\n"); complete = true; return; }
        }
        if (method == "OPTIONS") { writeHttp(socket, 204, "text/plain", QByteArray()); complete = true; return; }
        if (method != "GET" && method != "POST") { writeHttp(socket, 405, "text/plain", "method not allowed\n"); complete = true; return; }
        if (upgrade == "websocket") {
            complete = true;
            upgradeWebSocket(QString::fromUtf8(target), websocketKey, websocketProtocols, buffer.mid(headerEnd + 4));
            return;
        }
        if (method == "POST" && !sawLength) { writeHttp(socket, 400, "text/plain", "content-length required\n"); complete = true; return; }
        if (buffer.size() < headerEnd + 4 + length) return;
        const auto body = buffer.mid(headerEnd + 4, int(length));
        complete = true;
        handle(method, QString::fromUtf8(target), auth, body);
    }

    void upgradeWebSocket(const QString &path, const QByteArray &key, const QByteArray &protocols, const QByteArray &leftover) {
        static const QString prefix = "/api/jobs/";
        static const QString suffix = "/terminal";
        if (key.isEmpty() || !path.startsWith(prefix) || !path.endsWith(suffix)) {
            writeHttp(socket, 404, "text/plain", "not found\n");
            return;
        }
        const auto id = path.mid(prefix.size(), path.size() - prefix.size() - suffix.size());
        if (id.isEmpty() || id.contains('/') || id.contains("..")) {
            writeHttp(socket, 404, "text/plain", "not found\n");
            return;
        }
        // Browsers cannot set Authorization on WebSocket requests; the token
        // rides as a subprotocol offered alongside the "evereye" label.
        bool labelled = false;
        QByteArray token;
        for (const auto &item : protocols.split(',')) {
            const auto value = item.trimmed();
            if (value == "evereye") labelled = true;
            else if (!value.isEmpty()) token = value;
        }
        if (!labelled || !equalToken(token, state->token)) {
            writeHttp(socket, 401, "text/plain", "unauthorized\n");
            return;
        }
        const auto job = controller("jobs.get", {{"id", id}});
        if (!job.value("ok").toBool()) {
            writeHttp(socket, 404, "text/plain", "not found\n");
            return;
        }
        complete = true;
        QObject::disconnect(socket, nullptr, this, nullptr);
        QByteArray response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + webSocketAccept(key) + "\r\n";
        response += "Sec-WebSocket-Protocol: evereye\r\n\r\n";
        socket->write(response);
        socket->flush();
        new WsTerminal(socket, id, leftover, parent());
        deleteLater();
    }

    bool authorized(const QByteArray &auth) const {
        const QByteArray prefix = "Bearer ";
        if (!auth.startsWith(prefix)) return false;
        return equalToken(auth.mid(prefix.size()), state->token);
    }

    void handle(const QByteArray &method, QString path, const QByteArray &auth, const QByteArray &body) {
        if (!path.startsWith("/api/")) { serveStatic(path); return; }
        if (!authorized(auth)) { writeJson(socket, 401, {{"ok", false}, {"error", "Unauthorized"}}); return; }
        if (path == "/api/status" && method == "GET") { writeJson(socket, 200, controller("status")); return; }
        if (path == "/api/jobs" && method == "GET") { writeJson(socket, 200, controller("jobs.list")); return; }
        if (path == "/api/jobs" && method == "POST") {
            QJsonParseError e; const auto d = QJsonDocument::fromJson(body, &e);
            if (e.error != QJsonParseError::NoError || !d.isObject()) { writeJson(socket, 400, {{"ok", false}, {"error", "JSON object required"}}); return; }
            auto args = d.object();
            if (args.contains("command") && !state->arbitrary) {
                writeJson(socket, 403, {{"ok", false}, {"error", "Arbitrary commands are disabled"}});
                return;
            }
            writeJson(socket, 200, controller("jobs.start", args));
            return;
        }
        if (path == "/api/tasks" && method == "GET") { writeJson(socket, 200, controller("tasks.list")); return; }
        if (path == "/api/session/end" && method == "POST") { writeJson(socket, 200, controller("end")); return; }
        if (path == "/api/session/extend" && method == "POST") {
            QJsonParseError e; const auto d = QJsonDocument::fromJson(body, &e);
            if (e.error != QJsonParseError::NoError || !d.isObject()) { writeJson(socket, 400, {{"ok", false}, {"error", "JSON object required"}}); return; }
            writeJson(socket, 200, controller("extend", d.object()));
            return;
        }
        if (path.startsWith("/api/jobs/")) {
            const auto rest = path.mid(QString("/api/jobs/").size());
            const auto slash = rest.indexOf('/');
            const auto id = slash < 0 ? rest : rest.left(slash);
            const auto extra = slash < 0 ? QString() : rest.mid(slash);
            if (id.isEmpty() || id.contains("..")) { writeJson(socket, 404, {{"ok", false}, {"error", "Not found"}}); return; }
            if (extra.isEmpty() && method == "GET") { writeJson(socket, 200, controller("jobs.get", {{"id", id}})); return; }
            if (extra == "/stop" && method == "POST") { writeJson(socket, 200, controller("jobs.stop", {{"id", id}})); return; }
            if (extra == "/input" && method == "POST") {
                QJsonParseError e; const auto d = QJsonDocument::fromJson(body, &e);
                if (e.error != QJsonParseError::NoError || !d.isObject()) { writeJson(socket, 400, {{"ok", false}, {"error", "JSON object required"}}); return; }
                writeJson(socket, 200, controller("jobs.input", {{"id", id}, {"data", d.object().value("data").toString()}}));
                return;
            }
            if (extra == "/stream" && method == "GET") { startStream(id); return; }
        }
        writeJson(socket, 404, {{"ok", false}, {"error", "Not found"}});
    }

    void serveStatic(QString path) {
        if (path == "/") path = "/index.html";
        if (path.contains("..")) { writeHttp(socket, 404, "text/plain", "not found\n"); return; }
        QFileInfo info(state->webRoot + path);
        // Next's trailing-slash export puts pages at /page/index.html.
        if (!info.isFile() && !path.endsWith('/') && !QFileInfo(path).suffix().size())
            info = QFileInfo(state->webRoot + path + "/index.html");
        if (path.endsWith('/')) info = QFileInfo(state->webRoot + path + "index.html");
        if (!info.isFile() || !info.canonicalFilePath().startsWith(QFileInfo(state->webRoot).canonicalFilePath())) {
            writeHttp(socket, 404, "text/plain", "not found\n"); return;
        }
        path = info.fileName();
        QFile f(info.filePath());
        if (!f.open(QIODevice::ReadOnly)) { writeHttp(socket, 404, "text/plain", "not found\n"); return; }
        QByteArray type = "text/plain";
        if (path.endsWith(".html")) type = "text/html; charset=utf-8";
        else if (path.endsWith(".js")) type = "application/javascript";
        else if (path.endsWith(".css")) type = "text/css";
        else if (path.endsWith(".svg")) type = "image/svg+xml";
        else if (path.endsWith(".webmanifest") || path.endsWith(".json")) type = "application/manifest+json";
        writeHttp(socket, 200, type, f.readAll());
    }

    void startStream(const QString &id) {
        streaming = true;
        streamId = id;
        const auto first = controller("jobs.log", {{"id", id}, {"offset", 0}, {"limit", 65536}});
        if (!first.value("ok").toBool()) { writeJson(socket, 404, first); return; }
        streamOffset = first.value("nextOffset").toInteger();
        // No Content-Length and no chunking: the stream is delimited by close,
        // so never advertise keep-alive here or browsers may not emit chunks.
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
        auto payload = jsonBytes(first);
        socket->write("data: " + payload + "\n\n");
        pump = new QTimer(this);
        pump->setInterval(400);
        QObject::connect(pump, &QTimer::timeout, this, [this] {
            const auto next = controller("jobs.log", {{"id", streamId}, {"offset", streamOffset}, {"limit", 65536}});
            if (!next.value("ok").toBool()) { socket->disconnectFromHost(); return; }
            const auto data = next.value("data").toString();
            if (!data.isEmpty()) {
                streamOffset = next.value("nextOffset").toInteger();
                socket->write("data: " + jsonBytes(next) + "\n\n");
            }
            if (next.value("eof").toBool()) {
                socket->write("event: end\ndata: {}\n\n");
                socket->disconnectFromHost();
            }
        });
        pump->start();
    }
};

bool loadOrCreateConfig(GatewayState &state, QString &error) {
    const auto dir = configDirectory();
    QDir().mkpath(dir);
    const auto path = dir + "/gateway.json";
    QJsonObject o;
    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return false; }
        QJsonParseError e; const auto d = QJsonDocument::fromJson(f.readAll(), &e);
        if (e.error != QJsonParseError::NoError || !d.isObject()) { error = "Invalid gateway.json"; return false; }
        o = d.object();
    }
    if (!o.contains("token") || o.value("token").toString().size() < 32) o.insert("token", QString::fromLatin1(randomToken()));
    if (state.bindGiven) o.insert("bind", state.bind);
    else if (!o.contains("bind")) o.insert("bind", state.bind);
    if (state.portGiven) o.insert("port", state.port);
    else if (!o.contains("port")) o.insert("port", state.port);
    if (!o.contains("enabled")) o.insert("enabled", true);
    if (!o.contains("allowArbitraryCommands")) o.insert("allowArbitraryCommands", false);
    if (!o.contains("selfSigned")) o.insert("selfSigned", !loopback(o.value("bind").toString(state.bind)));
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || !out.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) { error = "Cannot write gateway.json"; return false; }
    out.write(QJsonDocument(o).toJson());
    if (!out.commit()) { error = out.errorString(); return false; }
    if (!o.value("enabled").toBool(true)) { error = "Gateway is disabled in gateway.json"; return false; }
    state.token = o.value("token").toString().toUtf8();
    state.bind = o.value("bind").toString(state.bind);
    state.port = quint16(o.value("port").toInt(state.port));
    state.arbitrary = o.value("allowArbitraryCommands").toBool(false);
    state.notify = o.value("notify").toArray();
    state.webRoot = dataDirectory();
    const bool wantTls = o.value("selfSigned").toBool(false) || o.contains("tls");
    if (wantTls) {
        QString certPath = dir + "/gateway.crt";
        QString keyPath = dir + "/gateway.key";
        if (o.value("tls").isObject()) {
            const auto tls = o.value("tls").toObject();
            if (!tls.value("cert").toString().isEmpty()) certPath = tls.value("cert").toString();
            if (!tls.value("key").toString().isEmpty()) keyPath = tls.value("key").toString();
        }
        if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
            QProcess openssl;
            openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "825", "-nodes",
                                      "-keyout", keyPath, "-out", certPath, "-subj", "/CN=evereye"});
            if (!openssl.waitForFinished(15000) || openssl.exitCode() != 0) {
                error = "Cannot create TLS certificate (install openssl or provide tls.cert/tls.key)";
                return false;
            }
            QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner);
            QFile::setPermissions(certPath, QFile::ReadOwner | QFile::WriteOwner);
        }
        QFile certFile(certPath), keyFile(keyPath);
        if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) { error = "Cannot read TLS files"; return false; }
        state.cert = QSslCertificate(certFile.readAll());
        state.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
        if (state.cert.isNull() || state.key.isNull()) { error = "Invalid TLS certificate or key"; return false; }
        state.tls = true;
    }
    if (!loopback(state.bind) && !state.tls && qEnvironmentVariable("EVEREYE_INSECURE_ALLOW_PLAINTEXT") != "1") {
        error = "Non-loopback bind requires TLS. Set selfSigned in gateway.json, or EVEREYE_INSECURE_ALLOW_PLAINTEXT=1";
        return false;
    }
    if (state.tls && !QSslSocket::supportsSsl()) { error = "Qt was built without TLS"; return false; }
    return true;
}

void fireNotify(GatewayState *state, const QJsonObject &job) {
    if (!state->http) return;
    const auto status = job.value("status").toString();
    const QString event = status == "Failed" || status == "Killed" ? "jobFail" : "jobEnd";
    const auto body = jsonBytes({{"event", event}, {"job", job}});
    for (const auto &value : state->notify) {
        const auto hook = value.toObject();
        const auto url = hook.value("url").toString();
        if (url.isEmpty()) continue;
        const auto on = hook.value("on").toArray();
        bool match = on.isEmpty();
        for (const auto &item : on) if (item.toString() == event) match = true;
        if (!match) continue;
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        auto *reply = state->http->post(req, body);
        QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
}
}

int runGateway(const QStringList &args) {
    GatewayState state;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--bind" && i + 1 < args.size()) { state.bind = args[++i]; state.bindGiven = true; }
        else if (args[i] == "--port" && i + 1 < args.size()) { state.port = quint16(args[++i].toUInt()); state.portGiven = true; }
        else if (args[i] == "--help") {
            QTextStream(stdout) << "evereye gateway [--bind 127.0.0.1] [--port 8737]\n";
            return 0;
        }
    }
    QString error;
    if (!loadOrCreateConfig(state, error)) { QTextStream(stderr) << error << Qt::endl; return 1; }
    TlsServer server;
    server.tls = state.tls;
    server.cert = state.cert;
    server.key = state.key;
    if (!server.listen(QHostAddress(state.bind), state.port)) {
        QTextStream(stderr) << "Cannot listen on " << state.bind << ":" << state.port << ": " << server.errorString() << Qt::endl;
        return 1;
    }
    QNetworkAccessManager http;
    state.http = &http;
    QTimer poll;
    QSet<QString> known;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        const auto list = controller("jobs.list");
        for (const auto &value : list.value("jobs").toArray()) {
            const auto job = value.toObject();
            const auto id = job.value("id").toString();
            const auto status = job.value("status").toString();
            const bool running = status == "Running" || status == "Starting";
            if (!running && known.contains(id + ":live")) {
                known.remove(id + ":live");
                fireNotify(&state, job);
            }
            if (running) known.insert(id + ":live");
        }
    });
    poll.start(1000);
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (auto *socket = server.nextPendingConnection()) new Client(socket, &state, &server);
    });
    QTextStream(stderr) << "EverEye gateway on " << (state.tls ? "https://" : "http://") << state.bind << ":" << state.port
                        << "\nToken stored in " << configDirectory() << "/gateway.json\n";
    return QCoreApplication::exec();
}
}
