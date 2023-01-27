#include <QGuiApplication>

#include "Server.h"

int main(int argc, char **argv)
{
    QGuiApplication application{argc, argv};

    KRdp::Server server;
    server.start();

    return application.exec();
}
