#include <QGuiApplication>

#include "Server.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};

    KRdp::Server server;

    server.setAddress(QHostAddress::Any);
    server.setPort(3389);
    server.setUserName(QStringLiteral("test"));
    server.setPassword(QStringLiteral("test"));
    server.setTlsCertificate(QStringLiteral("server.crt"));
    server.setTlsCertificateKey(QStringLiteral("server.key"));

    server.start();

    return application.exec();
}
