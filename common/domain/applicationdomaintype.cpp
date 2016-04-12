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
#include "applicationdomaintype.h"
#include "log.h"
#include "../bufferadaptor.h"

namespace Sink {
namespace ApplicationDomain {

ApplicationDomainType::ApplicationDomainType()
    :mAdaptor(new MemoryBufferAdaptor())
{

}

ApplicationDomainType::ApplicationDomainType(const QByteArray &resourceInstanceIdentifier)
    :mAdaptor(new MemoryBufferAdaptor()),
    mResourceInstanceIdentifier(resourceInstanceIdentifier)
{

}

ApplicationDomainType::ApplicationDomainType(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : mAdaptor(adaptor),
    mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mIdentifier(identifier),
    mRevision(revision)
{
}

ApplicationDomainType::ApplicationDomainType(const ApplicationDomainType &other)
{
    *this = other;
}

ApplicationDomainType& ApplicationDomainType::operator=(const ApplicationDomainType &other)
{
    mAdaptor = other.mAdaptor;
    mChangeSet = other.mChangeSet;
    mResourceInstanceIdentifier = other.mResourceInstanceIdentifier;
    mIdentifier = other.mIdentifier;
    mRevision = other.mRevision;
    return *this;
}

ApplicationDomainType::~ApplicationDomainType()
{
}

bool ApplicationDomainType::hasProperty(const QByteArray &key) const
{
    Q_ASSERT(mAdaptor);
    return mAdaptor->availableProperties().contains(key);
}

QVariant ApplicationDomainType::getProperty(const QByteArray &key) const
{
    Q_ASSERT(mAdaptor);
    if (!mAdaptor->availableProperties().contains(key)) {
        Warning() << "No such property available " << key;
    }
    return mAdaptor->getProperty(key);
}

void ApplicationDomainType::setProperty(const QByteArray &key, const QVariant &value)
{
    Q_ASSERT(mAdaptor);
    mChangeSet.insert(key);
    mAdaptor->setProperty(key, value);
}

void ApplicationDomainType::setChangedProperties(const QSet<QByteArray> &changeset)
{
    mChangeSet = changeset;
}

QByteArrayList ApplicationDomainType::changedProperties() const
{
    return mChangeSet.toList();
}

QByteArrayList ApplicationDomainType::availableProperties() const
{
    Q_ASSERT(mAdaptor);
    return mAdaptor->availableProperties();
}

qint64 ApplicationDomainType::revision() const
{
    return mRevision;
}

QByteArray ApplicationDomainType::resourceInstanceIdentifier() const
{
    return mResourceInstanceIdentifier;
}

QByteArray ApplicationDomainType::identifier() const
{
    return mIdentifier;
}

Entity::~Entity()
{

}

Event::~Event()
{

}

Todo::~Todo()
{

}

Mail::~Mail()
{

}

Folder::~Folder()
{

}

SinkResource::SinkResource(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

SinkResource::SinkResource(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

SinkResource::SinkResource()
    : ApplicationDomainType()
{

}

SinkResource::~SinkResource()
{

}

SinkAccount::SinkAccount(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

SinkAccount::SinkAccount(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

SinkAccount::SinkAccount()
    : ApplicationDomainType()
{

}

SinkAccount::~SinkAccount()
{

}

Identity::Identity(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

Identity::Identity(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

Identity::Identity()
    : ApplicationDomainType()
{

}

Identity::~Identity()
{

}

template<>
QByteArray getTypeName<Event>()
{
    return "event";
}

template<>
QByteArray getTypeName<Todo>()
{
    return "todo";
}

template<>
QByteArray getTypeName<SinkResource>()
{
    return "sinkresource";
}

template<>
QByteArray getTypeName<SinkAccount>()
{
    return "sinkaccount";
}

template<>
QByteArray getTypeName<Identity>()
{
    return "identity";
}

template<>
QByteArray getTypeName<Mail>()
{
    return "mail";
}

template<>
QByteArray getTypeName<Folder>()
{
    return "folder";
}

}
}

