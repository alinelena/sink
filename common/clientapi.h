/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#pragma once

#include "sinkcommon_export.h"
#include <QString>
#include <QSharedPointer>

#include <Async/Async>

#include "query.h"
#include "inspection.h"
#include "applicationdomaintype.h"

class QAbstractItemModel;

namespace Sink {
class ResourceAccess;
class Notification;

class SINKCOMMON_EXPORT Notifier {
public:
    Notifier(const QSharedPointer<ResourceAccess> &resourceAccess);
    Notifier(const QByteArray &resourceInstanceIdentifier);
    // Notifier(const QByteArrayList &resource);
    void registerHandler(std::function<void(const Notification &)>);

private:
    class Private;
    QSharedPointer<Private> d;
};


/**
 * Store interface used in the client API.
 */
namespace Store {

QString SINKCOMMON_EXPORT storageLocation();
QByteArray SINKCOMMON_EXPORT resourceName(const QByteArray &instanceIdentifier);

enum Roles {
    DomainObjectRole = Qt::UserRole + 1, //Must be the same as in ModelResult
    ChildrenFetchedRole,
    DomainObjectBaseRole
};

/**
    * Asynchronusly load a dataset with tree structure information
    */
template <class DomainType>
QSharedPointer<QAbstractItemModel> SINKCOMMON_EXPORT loadModel(Query query);

/**
 * Create a new entity.
 */
template <class DomainType>
KAsync::Job<void> SINKCOMMON_EXPORT create(const DomainType &domainObject);

/**
 * Modify an entity.
 * 
 * This includes moving etc. since these are also simple settings on a property.
 */
template <class DomainType>
KAsync::Job<void> SINKCOMMON_EXPORT modify(const DomainType &domainObject);

/**
 * Remove an entity.
 */
template <class DomainType>
KAsync::Job<void> SINKCOMMON_EXPORT remove(const DomainType &domainObject);

/**
 * Synchronize data to local cache.
 */
KAsync::Job<void> SINKCOMMON_EXPORT synchronize(const Sink::Query &query);

/**
 * Removes a resource from disk.
 */
void SINKCOMMON_EXPORT removeFromDisk(const QByteArray &resourceIdentifier);

/**
 * Removes all resource data from disk.
 * 
 * This will not touch the configuration. All commands that that arrived at the resource before this command will be dropped. All commands that arrived later will be executed.
 */
KAsync::Job<void> SINKCOMMON_EXPORT removeDataFromDisk(const QByteArray &resourceIdentifier);

template <class DomainType>
KAsync::Job<DomainType> SINKCOMMON_EXPORT fetchOne(const Sink::Query &query);

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr> > SINKCOMMON_EXPORT fetchAll(const Sink::Query &query);

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr> > SINKCOMMON_EXPORT fetch(const Sink::Query &query, int minimumAmount = 0);

};

namespace Resources {

template <class DomainType>
KAsync::Job<void> SINKCOMMON_EXPORT inspect(const Inspection &inspectionCommand);

/**
    * Shutdown resource.
    */
KAsync::Job<void> SINKCOMMON_EXPORT shutdown(const QByteArray &resourceIdentifier);

/**
    * Start resource.
    * 
    * The resource is ready for operation once this command completes.
    * This command is only necessary if a resource was shutdown previously,
    * otherwise the resource process will automatically start as necessary.
    */
KAsync::Job<void> SINKCOMMON_EXPORT start(const QByteArray &resourceIdentifier);

/**
    * Flushes any pending messages to disk
    */
KAsync::Job<void> SINKCOMMON_EXPORT flushMessageQueue(const QByteArrayList &resourceIdentifier);

/**
    * Flushes any pending messages that haven't been replayed to the source.
    */
KAsync::Job<void> SINKCOMMON_EXPORT flushReplayQueue(const QByteArrayList &resourceIdentifier);

}

}

