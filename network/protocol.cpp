#include "protocol.h"

#include <QByteArray>
#include <QDataStream>

namespace
{

using namespace Net;

Body bodyFromBytes(Command command, QDataStream& stream)
{
    switch (command) {
    case Command::CREATE_ROOM: {
        CreateRoomBody b;
        stream >> b.roomId;
        return b;
    }
    case Command::JOIN_ROOM: {
        JoinRoomBody b;
        stream >> b.roomId >> b.name;
        return b;
    }
    case Command::VOICE_MSG: {
        VoiceMessageBody b;
        uint32_t size {};
        stream >> b.timestamp >> size;

        b.samples.resize(size);
        stream.readRawData(reinterpret_cast<char*>(b.samples.data()),
                           static_cast<int>(size));
        return b;
    }
    case Command::ACK: {
        AckBody b;
        uint8_t status {};
        stream >> b.originalSequenceId >> status;
        b.status = static_cast<Status>(status);
        return b;
    }
    }

    Q_UNREACHABLE();
}

void bodyToBytes(const Body& body, QDataStream& stream)
{
    std::visit([&stream](const auto& b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, CreateRoomBody>) {
            stream << b.roomId;
        } else if constexpr (std::is_same_v<T, JoinRoomBody>) {
            stream << b.roomId << b.name;
        } else if constexpr (std::is_same_v<T, VoiceMessageBody>) {
            stream << b.timestamp << static_cast<uint32_t>(b.samples.size());
            stream.writeRawData(reinterpret_cast<const char*>(b.samples.data()),
                                static_cast<int>(b.samples.size()));
        } else if constexpr (std::is_same_v<T, AckBody>) {
            stream << b.originalSequenceId << static_cast<uint8_t>(b.status);
        }
    }, body);
}

}

namespace Net
{

Header Header::fromBytes(QDataStream& stream)
{
    Header h;

    uint8_t version {};
    uint16_t command {};
    stream >> version >> command >> h.sequenceId;

    h.version = static_cast<Version>(version);
    h.command = static_cast<Command>(command);

    return h;
}

void Header::toBytes(QDataStream& stream) const
{
    stream << static_cast<uint8_t>(version) << static_cast<uint16_t>(command) << sequenceId;
}

Packet Packet::fromBytes(QDataStream& stream)
{
    Packet p;

    p.header = Header::fromBytes(stream);
    p.body = bodyFromBytes(p.header.command, stream);

    return p;
}

void Packet::toBytes(QDataStream& stream) const
{
    header.toBytes(stream);
    bodyToBytes(body, stream);
}

}
