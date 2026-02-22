#pragma once

#include <QString>

#include <QtGlobal>
#include <cstdint>
#include <vector>
#include <variant>

class QByteArray;
class QDataStream;

namespace Net
{

enum class Command : uint16_t
{
    CREATE_ROOM,
    JOIN_ROOM,
    VOICE_MSG,
    ACK
};

enum class Status : uint8_t
{
    OK,
    FAIL
};

enum class Version : uint8_t
{
    V1
};

struct Header
{
    Version version;
    Command command;
    uint32_t sequenceId;
    uint32_t bodySize;

    static Header fromBytes(QDataStream& stream);
    void toBytes(QDataStream& stream) const;
};

struct CreateRoomBody
{
    quint64 roomId;
};

struct JoinRoomBody
{
    quint64 roomId;
    QString name;
};

struct VoiceMessageBody
{
    quint64 timestamp;
    std::vector<unsigned char> samples;
};

struct AckBody
{
    uint32_t originalSequenceId;
    Status status;
};

using Body = std::variant<CreateRoomBody, JoinRoomBody, VoiceMessageBody, AckBody>;

struct Packet
{
    Header header;
    Body body;

    static Packet fromBytes(QDataStream& stream);
    void toBytes(QDataStream& stream) const;
};

}
