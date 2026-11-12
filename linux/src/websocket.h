#pragma once
#include <QByteArray>

namespace insomnia {
// RFC 6455 handshake + frame helpers. The gateway implements the small
// WebSocket surface it needs rather than pulling in another Qt module.
QByteArray webSocketAccept(const QByteArray &clientKey);
QByteArray webSocketFrame(int opcode, const QByteArray &payload);

class WebSocketParser {
public:
    void append(const QByteArray &data);
    // Returns true when a complete frame was decoded. `fatal` marks a protocol
    // violation that must close the connection.
    bool next(QByteArray &payload, int &opcode, bool &fatal);
private:
    QByteArray buffer;
    QByteArray fragments;
    int fragmentOpcode = 0;
};
}
