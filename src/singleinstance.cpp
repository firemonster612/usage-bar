#include "singleinstance.h"

#include <QLocalServer>
#include <QLocalSocket>

#include <utility>

SingleInstance::SingleInstance(QString name, QObject *parent)
    : QObject(parent), name_(std::move(name)), server_(new QLocalServer(this))
{
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server_->nextPendingConnection()) {
            const auto readCommand = [this, socket] {
                if (socket->bytesAvailable() == 0)
                    return;
                if (socket->readAll().trimmed() == "show")
                    emit showRequested();
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, readCommand);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QMetaObject::invokeMethod(socket, readCommand, Qt::QueuedConnection);
        }
    });
}

bool SingleInstance::notifyExisting(bool requestShow)
{
    QLocalSocket socket;
    socket.connectToServer(name_, QIODevice::WriteOnly);
    if (!socket.waitForConnected(500))
        return false;
    socket.write(requestShow ? "show" : "background");
    socket.flush();
    socket.waitForBytesWritten(500);
    return true;
}

bool SingleInstance::start(bool requestShow)
{
    if (notifyExisting(requestShow))
        return false;
    if (server_->listen(name_))
        return true;
    if (server_->serverError() != QAbstractSocket::AddressInUseError)
        return false;
    if (notifyExisting(requestShow))
        return false;
    QLocalServer::removeServer(name_);
    return server_->listen(name_);
}
