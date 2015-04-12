/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "resourceaccess.h"

#include "common/commands.h"
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"
#include "common/synchronize_generated.h"
#include "common/notification_generated.h"
#include "log.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>

namespace Akonadi2
{

class QueuedCommand
{
public:
    QueuedCommand(int commandId, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          callback(callback)
    {}

    QueuedCommand(int commandId, const QByteArray &b, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          buffer(b),
          callback(callback)
    {
    }

private:
    QueuedCommand(const QueuedCommand &other);
    QueuedCommand &operator=(const QueuedCommand &rhs);

public:
    const int commandId;
    QByteArray buffer;
    std::function<void(int, const QString &)> callback;
};

class ResourceAccess::Private
{
public:
    Private(const QByteArray &name, ResourceAccess *ra);
    QByteArray resourceName;
    QLocalSocket *socket;
    QTimer *tryOpenTimer;
    bool startingProcess;
    bool openingConnection;
    QByteArray partialMessageBuffer;
    flatbuffers::FlatBufferBuilder fbb;
    QVector<QSharedPointer<QueuedCommand>> commandQueue;
    QMap<uint, QSharedPointer<QueuedCommand>> pendingCommands;
    QMultiMap<uint, std::function<void(int error, const QString &errorMessage)> > resultHandler;
    uint messageId;
};

ResourceAccess::Private::Private(const QByteArray &name, ResourceAccess *q)
    : resourceName(name),
      socket(new QLocalSocket(q)),
      tryOpenTimer(new QTimer(q)),
      startingProcess(false),
      openingConnection(false),
      messageId(0)
{
}

ResourceAccess::ResourceAccess(const QByteArray &resourceName, QObject *parent)
    : QObject(parent),
      d(new Private(resourceName, this))
{
    d->tryOpenTimer->setInterval(50);
    d->tryOpenTimer->setSingleShot(true);
    connect(d->tryOpenTimer, &QTimer::timeout,
            this, &ResourceAccess::open);

    log("Starting access");
    connect(d->socket, &QLocalSocket::connected,
            this, &ResourceAccess::connected);
    connect(d->socket, &QLocalSocket::disconnected,
            this, &ResourceAccess::disconnected);
    connect(d->socket, SIGNAL(error(QLocalSocket::LocalSocketError)),
            this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
    connect(d->socket, &QIODevice::readyRead,
            this, &ResourceAccess::readResourceMessage);
}

ResourceAccess::~ResourceAccess()
{

}

QByteArray ResourceAccess::resourceName() const
{
    return d->resourceName;
}

bool ResourceAccess::isReady() const
{
    return d->socket->isValid();
}

void ResourceAccess::registerCallback(uint messageId, const std::function<void(int error, const QString &errorMessage)> &callback)
{
    d->resultHandler.insert(messageId, callback);
}

Async::Job<void> ResourceAccess::sendCommand(int commandId)
{
    return Async::start<void>([this, commandId](Async::Future<void> &f) {
        auto continuation = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            }
            f.setFinished();
        };
        d->commandQueue << QSharedPointer<QueuedCommand>::create(commandId, continuation);
        if (isReady()) {
            processCommandQueue();
        }
    });
}

Async::Job<void>  ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    //The flatbuffer is transient, but we want to store it until the job is executed
    QByteArray buffer(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize());
    return Async::start<void>([commandId, buffer, this](Async::Future<void> &f) {
        auto callback = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            } else {
                f.setFinished();
            }
        };

        d->commandQueue << QSharedPointer<QueuedCommand>::create(commandId, buffer, callback);
        if (isReady()) {
            processCommandQueue();
        }
    });
}

Async::Job<void> ResourceAccess::synchronizeResource(bool sourceSync, bool localSync)
{
    auto command = Akonadi2::CreateSynchronize(d->fbb, sourceSync, localSync);
    Akonadi2::FinishSynchronizeBuffer(d->fbb, command);
    return sendCommand(Commands::SynchronizeCommand, d->fbb);
    d->fbb.Clear();
}

void ResourceAccess::open()
{
    if (d->socket->isValid()) {
        log("Socket valid, so not opening again");
        return;
    }
    d->openingConnection = true;

    //TODO: if we try and try and the process does not pick up
    //      we should probably try to start the process again
    d->socket->setServerName(d->resourceName);
    log(QString("Opening %1").arg(d->socket->serverName()));
    //FIXME: race between starting the exec and opening the socket?
    d->socket->open();
}

void ResourceAccess::close()
{
    log(QString("Closing %1").arg(d->socket->fullServerName()));
    d->socket->close();
}

void ResourceAccess::sendCommand(const QSharedPointer<QueuedCommand> &command)
{
    Q_ASSERT(isReady());
    //TODO: we should have a timeout for commands
    d->messageId++;
    const auto messageId = d->messageId;
    log(QString("Sending command %1 with messageId %2").arg(command->commandId).arg(d->messageId));
    if (command->callback) {
        registerCallback(d->messageId, [this, messageId, command](int number, QString foo) {
            d->pendingCommands.remove(messageId);
            command->callback(number, foo);
        });
    }
    //Keep track of the command until we're sure it arrived
    d->pendingCommands.insert(d->messageId, command);
    Commands::write(d->socket, d->messageId, command->commandId, command->buffer.constData(), command->buffer.size());
}

void ResourceAccess::processCommandQueue()
{
    //TODO: serialize instead of blast them all through the socket?
    log(QString("We have %1 queued commands").arg(d->commandQueue.size()));
    for (auto command: d->commandQueue) {
        sendCommand(command);
    }
    d->commandQueue.clear();
}

void ResourceAccess::connected()
{
    d->startingProcess = false;
    d->openingConnection = false;

    if (!isReady()) {
        return;
    }

    log(QString("Connected: %1").arg(d->socket->fullServerName()));

    {
        auto name = d->fbb.CreateString(QString::number(QCoreApplication::applicationPid()).toLatin1());
        auto command = Akonadi2::CreateHandshake(d->fbb, name);
        Akonadi2::FinishHandshakeBuffer(d->fbb, command);
        Commands::write(d->socket, ++d->messageId, Commands::HandshakeCommand, d->fbb);
        d->fbb.Clear();
    }

    processCommandQueue();

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    d->socket->close();
    log(QString("Disconnected from %1").arg(d->socket->fullServerName()));
    emit ready(false);
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    if (d->startingProcess) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
        return;
    }

    if (error == QLocalSocket::PeerClosedError) {
        Log() << "The resource closed the connection.";
    } else {
        Warning() << QString("Connection error: %1 : %2").arg(error).arg(d->socket->errorString());
    }

    //TODO We could first try to reconnect and resend the message if necessary.
    for(auto handler : d->resultHandler.values()) {
        handler(1, "The resource closed unexpectedly");
    }
    d->resultHandler.clear();

    //We're trying to connect but failed, start the resource and retry.
    //Don't automatically restart on later disconnects.
    if (d->openingConnection) {
        startResourceAndConnect();
    }
}

void ResourceAccess::startResourceAndConnect()
{
    d->startingProcess = true;
    Log() << "Attempting to start resource " + d->resourceName;
    QStringList args;
    args << d->resourceName;
    if (QProcess::startDetached("akonadi2_synchronizer", args, QDir::homePath())) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
    } else {
        qWarning() << "Failed to start resource";
    }
}

void ResourceAccess::readResourceMessage()
{
    if (!d->socket->isValid()) {
        return;
    }

    d->partialMessageBuffer += d->socket->readAll();

    // should be scheduled rather than processed all at once
    while (processMessageBuffer()) {}
}

bool ResourceAccess::processMessageBuffer()
{
    static const int headerSize = Commands::headerSize();
    if (d->partialMessageBuffer.size() < headerSize) {
        qWarning() << "command too small";
        return false;
    }

    //const uint messageId = *(int*)(d->partialMessageBuffer.constData());
    const int commandId = *(int*)(d->partialMessageBuffer.constData() + sizeof(uint));
    const uint size = *(int*)(d->partialMessageBuffer.constData() + sizeof(int) + sizeof(uint));

    if (size > (uint)(d->partialMessageBuffer.size() - headerSize)) {
        qWarning() << "command too small";
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Revision updated to: %1").arg(buffer->revision()));
            emit revisionChanged(buffer->revision());

            break;
        }
        case Commands::CommandCompletion: {
            auto buffer = GetCommandCompletion(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Command with messageId %1 completed %2").arg(buffer->id()).arg(buffer->success() ? "sucessfully" : "unsuccessfully"));
            //TODO: if a queued command, get it out of the queue ... pass on completion ot the relevant objects .. etc

            //The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
            QMetaObject::invokeMethod(this, "callCallbacks", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(int, buffer->id()));
            break;
        }
        case Commands::NotificationCommand: {
            auto buffer = GetNotification(d->partialMessageBuffer.constData() + headerSize);
            switch (buffer->type()) {
                case Akonadi2::NotificationType::NotificationType_Shutdown:
                    Log() << "Received shutdown notification.";
                    close();
                    break;
                default:
                    Warning() << "Received unknown notification: " << buffer->type();
                    break;
            }
            break;
        }
        default:
            break;
    }

    d->partialMessageBuffer.remove(0, headerSize + size);
    return d->partialMessageBuffer.size() >= headerSize;
}

void ResourceAccess::callCallbacks(int id)
{
    for(auto handler : d->resultHandler.values(id)) {
        handler(0, QString());
    }
    d->resultHandler.remove(id);
}

void ResourceAccess::log(const QString &message)
{
    Log() << d->resourceName + ": " + message;
}

}
