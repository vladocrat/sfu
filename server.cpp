#include "server.h"

#include <QDataStream>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "network/protocol.h"


Q_LOGGING_CATEGORY(ServerCat, "Server")

using namespace Net;

struct Client
{
    QTcpSocket* tcp;
    QHostAddress udpAddress;
    uint16_t udpPort { 0 };
    uint32_t id { 0 };

    bool hasUdpEndpoint() const { return udpPort != 0; }
};

struct Room
{
    uint64_t id;
    std::vector<Client> members;

    void addMember(const Client& client) { members.push_back(client); }

    void removeMember(QTcpSocket* tcp)
    {
        members.erase(
            std::remove_if(members.begin(),
                           members.end(),
                           [tcp](const Client& c) { return c.tcp == tcp; }),
            members.end());
    }
};

struct Server::Impl
{
    QTcpServer tcpServer;
    QUdpSocket udpSocket;
    std::unordered_set<QTcpSocket*> pending;
    std::unordered_map<uint64_t, Room> rooms;
    uint32_t nextClientId { 1 };

    void removeClient(QTcpSocket* tcp)
    {
        pending.erase(tcp);

        for (auto& [roomId, room] : rooms) {
            room.removeMember(tcp);
        }
    }

    Client* findClientByAddress(const QHostAddress& address, uint16_t port)
    {
        for (auto& [roomId, room] : rooms) {
            for (auto& member : room.members) {
                if (member.udpAddress == address && member.udpPort == port) {
                    return &member;
                }
            }
        }
        return nullptr;
    }

    Client* findClientByTcpPeer(const QHostAddress& address)
    {
        for (auto& [roomId, room] : rooms) {
            for (auto& member : room.members) {
                qDebug() << member.tcp->peerAddress();
                if (member.tcp->peerAddress() == address && !member.hasUdpEndpoint()) {
                    return &member;
                }
            }
        }
        return nullptr;
    }

    Room* findRoomForUdpClient(const QHostAddress& address, uint16_t port)
    {
        for (auto& [roomId, room] : rooms) {
            for (const auto& member : room.members) {
                if (member.udpAddress == address && member.udpPort == port) {
                    return &room;
                }
            }
        }
        return nullptr;
    }

    void sendUdpPacket(const Client& client, const QByteArray& data)
    {
        if (!client.hasUdpEndpoint()) {
            return;
        }
        udpSocket.writeDatagram(data, client.udpAddress, client.udpPort);
    }

    Room* findRoomForClient(QTcpSocket* tcp)
    {
        for (auto& [roomId, room] : rooms) {
            for (const auto& member : room.members) {
                if (member.tcp == tcp) {
                    return &room;
                }
            }
        }
        return nullptr;
    }

    void sendPacket(QTcpSocket* tcp, const Packet& packet)
    {
        QByteArray body;
        QDataStream bodyStream(&body, QIODevice::WriteOnly);
        packet.toBytes(bodyStream);

        uint32_t size = body.size();
        tcp->write(reinterpret_cast<const char*>(&size), sizeof(uint32_t));
        tcp->write(body);
    }

    void sendAck(QTcpSocket* tcp, uint32_t sequenceId, Status status)
    {
        Packet ack;
        ack.header.version = Version::V1;
        ack.header.command = Command::ACK;
        ack.header.sequenceId = 0;
        ack.header.bodySize = 0;
        ack.body = AckBody { sequenceId, status };

        sendPacket(tcp, ack);
    }

    void handlePacket(QTcpSocket* tcp, const QByteArray& data)
    {
        qCDebug(ServerCat) << "recieved data: " << data.size();
        QDataStream stream(data);
        auto packet = Packet::fromBytes(stream);

        switch (packet.header.command) {
        case Command::CREATE_ROOM: {
            const auto& body = std::get<CreateRoomBody>(packet.body);
            qCDebug(ServerCat) << "Creating room" << body.roomId;

            rooms[body.roomId] = Room { body.roomId, { Client { tcp, {}, 0, nextClientId++ } } };
            pending.erase(tcp);

            sendAck(tcp, packet.header.sequenceId, Status::OK);
            break;
        }
        case Command::JOIN_ROOM: {
            const auto& body = std::get<JoinRoomBody>(packet.body);
            qCDebug(ServerCat) << body.name << "joining room" << body.roomId;

            auto it = rooms.find(body.roomId);
            if (it == rooms.end()) {
                qCWarning(ServerCat) << "Room" << body.roomId << "does not exist";
                sendAck(tcp, packet.header.sequenceId, Status::FAIL);
                return;
            }

            it->second.addMember(Client { tcp, {}, 0, nextClientId++ });
            pending.erase(tcp);

            sendAck(tcp, packet.header.sequenceId, Status::OK);
            break;
        }
        case Command::VOICE_MSG: {
            break;
        }
        case Command::ACK: {
            break;
        }
        }
    }
};

Server::Server()
    : d(std::make_unique<Impl>())
{
    ///! UDP
    QObject::connect(&d->udpSocket, &QUdpSocket::readyRead, this, [this]() {

        while (d->udpSocket.hasPendingDatagrams()) {
            const auto datagram = d->udpSocket.receiveDatagram();
            const auto senderAddress = datagram.senderAddress();
            const auto senderPort = static_cast<uint16_t>(datagram.senderPort());

            // Learn endpoint on first datagram
            auto* client = d->findClientByAddress(senderAddress, senderPort);
            if (!client) {
                client = d->findClientByTcpPeer(senderAddress);
                if (client) {
                    client->udpAddress = senderAddress;
                    client->udpPort = senderPort;
                    qCDebug(ServerCat) << "Learned UDP endpoint" << senderAddress << ":" << senderPort;
                } else {
                    qCWarning(ServerCat) << "Unknown UDP sender" << senderAddress << ":" << senderPort;
                    continue;
                }
            }

            auto* room = d->findRoomForUdpClient(senderAddress, senderPort);

            if (!room) {
                continue;
            }

            // Parse packet, stamp with sender's ID, re-serialize
            QDataStream inStream(datagram.data());
            auto packet = Packet::fromBytes(inStream);

            if (packet.header.command != Command::VOICE_MSG) {
                continue;
            }

            auto& msg = std::get<VoiceMessageBody>(packet.body);
            msg.senderId = client->id;

            QByteArray outData;
            QDataStream outStream(&outData, QIODevice::WriteOnly);
            packet.toBytes(outStream);

            for (const auto& member : room->members) {
                if (member.udpAddress == senderAddress && member.udpPort == senderPort) {
                    continue;
                }

                d->sendUdpPacket(member, outData);
            }
        }
    });

    ///! TCP
    QObject::connect(&d->tcpServer, &QTcpServer::newConnection, this, [this]() {
        while (auto* tcp = d->tcpServer.nextPendingConnection()) {
            qCDebug(ServerCat) << "New connection from" << tcp->peerAddress();

            d->pending.insert(tcp);

            QObject::connect(tcp, &QTcpSocket::readyRead, this, [this, tcp]() {
                if (tcp->bytesAvailable() < static_cast<qsizetype>(sizeof(uint32_t))) {
                    return;
                }

                uint32_t packetSize { 0 };
                tcp->peek(reinterpret_cast<char*>(&packetSize), sizeof(uint32_t));

                if (tcp->bytesAvailable() < static_cast<qsizetype>(sizeof(uint32_t) + packetSize)) {
                    return;
                }

                tcp->skip(sizeof(uint32_t));
                const auto data = tcp->read(packetSize);

                d->handlePacket(tcp, data);
            });

            QObject::connect(tcp, &QTcpSocket::disconnected, this, [this, tcp]() {
                qCDebug(ServerCat) << "Client disconnected:" << tcp->peerAddress();

                d->removeClient(tcp);
                tcp->deleteLater();
            });
        }
    });
}

Server::~Server() = default;

void Server::listen(const QHostAddress& address, uint16_t port)
{
    if (!d->tcpServer.listen(address, port)) {
        qCWarning(ServerCat) << "Failed to listen:" << d->tcpServer.errorString();
        return;
    }

    if (!d->udpSocket.bind(address, port)) {
        qCWarning(ServerCat) << "Failed to bind UDP:" << d->udpSocket.errorString();
        return;
    }

    qCDebug(ServerCat) << "Listening on" << address << ":" << port;

    d->rooms[0] = Room { 0, {} };
    qCDebug(ServerCat) << "Created default room 0";
}

