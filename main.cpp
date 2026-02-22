#include <QCoreApplication>
#include <QCommandLineParser>
#include <QHostAddress>

#include "server.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("light_whisper_server");

    QCommandLineParser parser;
    parser.setApplicationDescription("Light Whisper voice server");
    parser.addHelpOption();

    QCommandLineOption ipOption({ "i", "ip" }, "IP address to listen on", "address", "0.0.0.0");
    QCommandLineOption portOption({ "p", "port" }, "Port to listen on", "port", "8083");
    parser.addOption(ipOption);
    parser.addOption(portOption);
    parser.process(app);

    const auto ip = parser.value(ipOption);
    const auto port = parser.value(portOption).toUShort();

    QHostAddress address;
    if (!address.setAddress(ip)) {
        qCritical() << "Invalid IP address:" << ip;
        return 1;
    }

    Server server;
    server.listen(address, port);

    return app.exec();
}
