#pragma once

#include <QObject>
#include <QLoggingCategory>

#include <memory>

class QHostAddress;

Q_DECLARE_LOGGING_CATEGORY(ServerCat)

class Server final : public QObject
{
    Q_OBJECT
public:
    Server();
    ~Server();

    void listen(const QHostAddress& address, uint16_t port);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
