#include "websocket.h"
#include <QCryptographicHash>
#include <QtEndian>

namespace insomnia {
namespace {
constexpr int MaxPayload = 1 << 20;
QByteArray unmask(const QByteArray &data, const char mask[4]) {
    QByteArray out(data.size(), '\0');
    for (int i = 0; i < data.size(); ++i) out[i] = char(data.at(i) ^ mask[i % 4]);
    return out;
}
}

QByteArray webSocketAccept(const QByteArray &clientKey) {
    static const QByteArray guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return QCryptographicHash::hash(clientKey + guid, QCryptographicHash::Sha1).toBase64();
}

QByteArray webSocketFrame(int opcode, const QByteArray &payload) {
    QByteArray frame;
    frame.append(char(0x80 | (opcode & 0x0f)));
    const auto size = payload.size();
    if (size < 126) {
        frame.append(char(size));
    } else if (size <= 0xffff) {
        frame.append(char(126));
        char length[2]; qToBigEndian<quint16>(quint16(size), reinterpret_cast<uchar *>(length));
        frame.append(length, 2);
    } else {
        frame.append(char(127));
        char length[8]; qToBigEndian<quint64>(quint64(size), reinterpret_cast<uchar *>(length));
        frame.append(length, 8);
    }
    frame.append(payload);
    return frame;
}

void WebSocketParser::append(const QByteArray &data) { buffer += data; }

bool WebSocketParser::next(QByteArray &payload, int &opcode, bool &fatal) {
    fatal = false;
    if (buffer.size() < 2) return false;
    const auto first = uchar(buffer.at(0));
    const auto second = uchar(buffer.at(1));
    const bool fin = first & 0x80;
    const int frameOpcode = first & 0x0f;
    if (!(second & 0x80)) { fatal = true; return false; }  // clients must mask
    quint64 length = second & 0x7f;
    int offset = 2;
    if (length == 126) {
        if (buffer.size() < 4) return false;
        length = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(buffer.constData() + 2));
        offset = 4;
    } else if (length == 127) {
        if (buffer.size() < 10) return false;
        length = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(buffer.constData() + 2));
        offset = 10;
    }
    if (length > MaxPayload) { fatal = true; return false; }
    if (buffer.size() < offset + 4 + int(length)) return false;
    const char *mask = buffer.constData() + offset;
    const auto data = unmask(buffer.mid(offset + 4, int(length)), mask);
    buffer.remove(0, offset + 4 + int(length));

    if (frameOpcode >= 8) {  // control frames are never fragmented
        if (!fin || length > 125) { fatal = true; return false; }
        payload = data;
        opcode = frameOpcode;
        return true;
    }
    if (frameOpcode == 0) {
        if (fragmentOpcode == 0) { fatal = true; return false; }
        fragments += data;
    } else {
        if (fragmentOpcode != 0) { fatal = true; return false; }
        fragmentOpcode = frameOpcode;
        fragments = data;
    }
    if (!fin) return false;
    payload = fragments;
    opcode = fragmentOpcode;
    fragments.clear();
    fragmentOpcode = 0;
    return true;
}
}
