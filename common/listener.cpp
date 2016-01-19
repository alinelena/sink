/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "listener.h"

#include "common/commands.h"
#include "common/resource.h"
#include "common/log.h"
#include "common/definitions.h"

// commands
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"
#include "common/synchronize_generated.h"
#include "common/notification_generated.h"
#include "common/revisionreplayed_generated.h"
#include "common/inspection_generated.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QTime>
#include <QDataStream>

Listener::Listener(const QByteArray &resourceInstanceIdentifier, QObject *parent)
    : QObject(parent),
      m_server(new QLocalServer(this)),
      m_resourceName(Akonadi2::resourceName(resourceInstanceIdentifier)),
      m_resourceInstanceIdentifier(resourceInstanceIdentifier),
      m_resource(0),
      m_clientBufferProcessesTimer(new QTimer(this)),
      m_messageId(0)
{
    connect(m_server, &QLocalServer::newConnection,
             this, &Listener::acceptConnection);
    Trace() << "Trying to open " << m_resourceInstanceIdentifier;

    if (!m_server->listen(QString::fromLatin1(m_resourceInstanceIdentifier))) {
        m_server->removeServer(m_resourceInstanceIdentifier);
        if (!m_server->listen(QString::fromLatin1(m_resourceInstanceIdentifier))) {
            Warning() << "Utter failure to start server";
            exit(-1);
        }
    }

    if (m_server->isListening()) {
        Log() << QString("Listening on %1").arg(m_server->serverName());
    }

    m_checkConnectionsTimer = new QTimer;
    m_checkConnectionsTimer->setSingleShot(true);
    m_checkConnectionsTimer->setInterval(1000);
    connect(m_checkConnectionsTimer, &QTimer::timeout, [this]() {
        if (m_connections.isEmpty()) {
            Log() << QString("No connections, shutting down.");
            quit();
        }
    });

    //TODO: experiment with different timeouts
    //      or even just drop down to invoking the method queued? => invoke queued unless we need throttling
    m_clientBufferProcessesTimer->setInterval(0);
    m_clientBufferProcessesTimer->setSingleShot(true);
    connect(m_clientBufferProcessesTimer, &QTimer::timeout,
            this, &Listener::processClientBuffers);
}

Listener::~Listener()
{
}

void Listener::closeAllConnections()
{
    for (Client &client: m_connections) {
        if (client.socket) {
            disconnect(client.socket, 0, this, 0);
            client.socket->close();
            delete client.socket;
            client.socket = 0;
        }
    }

    m_connections.clear();
}

void Listener::acceptConnection()
{
    Log() << "Accepting connection";
    QLocalSocket *socket = m_server->nextPendingConnection();

    if (!socket) {
        Warning() << "Accepted connection but didn't get a socket for it";
        return;
    }

    m_connections << Client("Unknown Client", socket);
    connect(socket, &QIODevice::readyRead,
            this, &Listener::onDataAvailable);
    connect(socket, &QLocalSocket::disconnected,
            this, &Listener::clientDropped);
    m_checkConnectionsTimer->stop();

    //If this is the first client, set the lower limit for revision cleanup
    if (m_connections.size() == 1) {
        loadResource()->setLowerBoundRevision(0);
    }

    if (socket->bytesAvailable()) {
        readFromSocket(socket);
    }
}

void Listener::clientDropped()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    bool dropped = false;
    QMutableVectorIterator<Client> it(m_connections);
    while (it.hasNext()) {
        const Client &client = it.next();
        if (client.socket == socket) {
            dropped = true;
            Log() << QString("Dropped connection: %1").arg(client.name) << socket;
            it.remove();
            break;
        }
    }
    if (!dropped) {
        Warning() << "Failed to find connection for disconnected socket: " << socket;
    }

    checkConnections();
}

void Listener::checkConnections()
{
    //If this was the last client, disengage the lower limit for revision cleanup
    if (m_connections.isEmpty()) {
        loadResource()->setLowerBoundRevision(std::numeric_limits<qint64>::max());
    }
    m_checkConnectionsTimer->start();
}

void Listener::onDataAvailable()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    readFromSocket(socket);
}

void Listener::readFromSocket(QLocalSocket *socket)
{
    Trace() << "Reading from socket...";
    for (Client &client: m_connections) {
        if (client.socket == socket) {
            client.commandBuffer += socket->readAll();
            if (!m_clientBufferProcessesTimer->isActive()) {
                m_clientBufferProcessesTimer->start();
            }
            break;
        }
    }
}

void Listener::processClientBuffers()
{
    //TODO: we should not process all clients, but iterate async over them and process
    //      one command from each in turn to ensure all clients get fair handling of
    //      commands?
    bool again = false;
    for (Client &client: m_connections) {
        if (!client.socket || !client.socket->isValid() || client.commandBuffer.isEmpty()) {
            continue;
        }

        if (processClientBuffer(client)) {
            again = true;
        }
    }

    if (again) {
        m_clientBufferProcessesTimer->start();
    }
}

void Listener::processCommand(int commandId, uint messageId, const QByteArray &commandBuffer, Client &client, const std::function<void(bool)> &callback)
{
    bool success = true;
    switch (commandId) {
        case Akonadi2::Commands::HandshakeCommand: {
            flatbuffers::Verifier verifier((const uint8_t *)commandBuffer.constData(), commandBuffer.size());
            if (Akonadi2::Commands::VerifyHandshakeBuffer(verifier)) {
                auto buffer = Akonadi2::Commands::GetHandshake(commandBuffer.constData());
                client.name = buffer->name()->c_str();
            } else {
                Warning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::SynchronizeCommand: {
            flatbuffers::Verifier verifier((const uint8_t *)commandBuffer.constData(), commandBuffer.size());
            if (Akonadi2::Commands::VerifySynchronizeBuffer(verifier)) {
                auto buffer = Akonadi2::Commands::GetSynchronize(commandBuffer.constData());
                Log() << QString("\tSynchronize request (id %1) from %2").arg(messageId).arg(client.name);
                auto timer = QSharedPointer<QTime>::create();
                timer->start();
                auto job = KAsync::null<void>();
                if (buffer->sourceSync()) {
                    job = loadResource()->synchronizeWithSource();
                }
                if (buffer->localSync()) {
                    job = job.then<void>(loadResource()->processAllMessages());
                }
                job.then<void>([callback, timer]() {
                    Trace() << "Sync took " << timer->elapsed();
                    callback(true);
                }).exec();
                return;
            } else {
                Warning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::InspectionCommand:
        case Akonadi2::Commands::FetchEntityCommand:
        case Akonadi2::Commands::DeleteEntityCommand:
        case Akonadi2::Commands::ModifyEntityCommand:
        case Akonadi2::Commands::CreateEntityCommand:
            Log() << "\tCommand id  " << messageId << " of type \"" << Akonadi2::Commands::name(commandId) << "\" from " << client.name;
            loadResource()->processCommand(commandId, commandBuffer);
            break;
        case Akonadi2::Commands::ShutdownCommand:
            Log() << QString("\tReceived shutdown command from %1").arg(client.name);
            //Immediately reject new connections
            m_server->close();
            QTimer::singleShot(0, this, &Listener::quit);
            break;
        case Akonadi2::Commands::PingCommand:
            Log() << QString("\tReceived ping command from %1").arg(client.name);
            break;
        case Akonadi2::Commands::RevisionReplayedCommand: {
            Log() << QString("\tReceived revision replayed command from %1").arg(client.name);
            flatbuffers::Verifier verifier((const uint8_t *)commandBuffer.constData(), commandBuffer.size());
            if (Akonadi2::Commands::VerifyRevisionReplayedBuffer(verifier)) {
                auto buffer = Akonadi2::Commands::GetRevisionReplayed(commandBuffer.constData());
                client.currentRevision = buffer->revision();
            } else {
                Warning() << "received invalid command";
            }
            loadResource()->setLowerBoundRevision(lowerBoundRevision());
        }
            break;
        default:
            if (commandId > Akonadi2::Commands::CustomCommand) {
                Log() << QString("\tReceived custom command from %1: ").arg(client.name) << commandId;
                loadResource()->processCommand(commandId, commandBuffer);
            } else {
                success = false;
                ErrorMsg() << QString("\tReceived invalid command from %1: ").arg(client.name) << commandId;
            }
            break;
    }
    callback(success);
}

qint64 Listener::lowerBoundRevision()
{
    qint64 lowerBound = 0;
    for (const Client &c : m_connections) {
        if (c.currentRevision > 0) {
            if (lowerBound == 0) {
                lowerBound = c.currentRevision;
            } else {
                lowerBound = qMin(c.currentRevision, lowerBound);
            }
        }
    }
    return lowerBound;
}

void Listener::quit()
{
    //Broadcast shutdown notifications to open clients, so they don't try to restart the resource
    auto command = Akonadi2::Commands::CreateNotification(m_fbb, Akonadi2::Commands::NotificationType::NotificationType_Shutdown);
    Akonadi2::Commands::FinishNotificationBuffer(m_fbb, command);
    for (Client &client : m_connections) {
        if (client.socket && client.socket->isOpen()) {
            Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::NotificationCommand, m_fbb);
        }
    }
    m_fbb.Clear();

    //Connections will be cleaned up later
    emit noClients();
}

bool Listener::processClientBuffer(Client &client)
{
    static const int headerSize = Akonadi2::Commands::headerSize();
    if (client.commandBuffer.size() < headerSize) {
        return false;
    }

    const uint messageId = *(uint*)client.commandBuffer.constData();
    const int commandId = *(int*)(client.commandBuffer.constData() + sizeof(uint));
    const uint size = *(uint*)(client.commandBuffer.constData() + sizeof(int) + sizeof(uint));
    Trace() << "Received message. Id:" << messageId << " CommandId: " << commandId << " Size: " << size;

    //TODO: reject messages above a certain size?

    const bool commandComplete = size <= uint(client.commandBuffer.size() - headerSize);
    if (commandComplete) {
        client.commandBuffer.remove(0, headerSize);

        auto socket = QPointer<QLocalSocket>(client.socket);
        auto clientName = client.name;
        const QByteArray commandBuffer = client.commandBuffer.left(size);
        client.commandBuffer.remove(0, size);
        processCommand(commandId, messageId, commandBuffer, client, [this, messageId, commandId, socket, clientName](bool success) {
            Log() << QString("\tCompleted command messageid %1 of type \"%2\" from %3").arg(messageId).arg(QString(Akonadi2::Commands::name(commandId))).arg(clientName);
            if (socket) {
                sendCommandCompleted(socket.data(), messageId, success);
            } else {
                Log() << QString("Socket became invalid before we could send a response. client: %1").arg(clientName);
            }
        });

        return client.commandBuffer.size() >= headerSize;
    }

    return false;
}

void Listener::sendCommandCompleted(QLocalSocket *socket, uint messageId, bool success)
{
    if (!socket || !socket->isValid()) {
        return;
    }

    auto command = Akonadi2::Commands::CreateCommandCompletion(m_fbb, messageId, success);
    Akonadi2::Commands::FinishCommandCompletionBuffer(m_fbb, command);
    Akonadi2::Commands::write(socket, ++m_messageId, Akonadi2::Commands::CommandCompletionCommand, m_fbb);
    m_fbb.Clear();
}

void Listener::refreshRevision(qint64 revision)
{
    updateClientsWithRevision(revision);
}

void Listener::updateClientsWithRevision(qint64 revision)
{
    auto command = Akonadi2::Commands::CreateRevisionUpdate(m_fbb, revision);
    Akonadi2::Commands::FinishRevisionUpdateBuffer(m_fbb, command);

    for (const Client &client: m_connections) {
        if (!client.socket || !client.socket->isValid()) {
            continue;
        }

        Trace() << "Sending revision update for " << client.name << revision;
        Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::RevisionUpdateCommand, m_fbb);
    }
    m_fbb.Clear();
}

void Listener::notify(const Akonadi2::ResourceNotification &notification)
{
    auto messageString = m_fbb.CreateString(notification.message.toUtf8().constData(), notification.message.toUtf8().size());
    auto idString = m_fbb.CreateString(notification.id.constData(), notification.id.size());
    Akonadi2::Commands::NotificationBuilder builder(m_fbb);
    builder.add_type(static_cast<Akonadi2::Commands::NotificationType>(notification.type));
    builder.add_code(notification.code);
    builder.add_identifier(idString);
    builder.add_message(messageString);
    auto command = builder.Finish();
    Akonadi2::Commands::FinishNotificationBuffer(m_fbb, command);
    for (Client &client : m_connections) {
        if (client.socket && client.socket->isOpen()) {
            Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::NotificationCommand, m_fbb);
        }
    }
    m_fbb.Clear();
}

Akonadi2::Resource *Listener::loadResource()
{
    if (!m_resource) {
        if (Akonadi2::ResourceFactory *resourceFactory = Akonadi2::ResourceFactory::load(m_resourceName)) {
            m_resource = resourceFactory->createResource(m_resourceInstanceIdentifier);
            Trace() << QString("Resource factory: %1").arg((qlonglong)resourceFactory);
            Trace() << QString("\tResource: %1").arg((qlonglong)m_resource);
            connect(m_resource, &Akonadi2::Resource::revisionUpdated,
                this, &Listener::refreshRevision);
            connect(m_resource, &Akonadi2::Resource::notify,
                this, &Listener::notify);
        } else {
            ErrorMsg() << "Failed to load resource " << m_resourceName;
            m_resource = new Akonadi2::Resource;
        }
    }
    return m_resource;
}

