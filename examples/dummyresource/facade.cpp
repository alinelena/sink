/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "facade.h"

#include <QDebug>
#include <functional>

#include "common/resourceaccess.h"
#include "common/commands.h"
#include "dummycalendar_generated.h"
#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "domainadaptor.h"
#include <common/entitybuffer.h>
#include <common/index.h>
#include <common/log.h>

using namespace DummyCalendar;
using namespace flatbuffers;


DummyResourceFacade::DummyResourceFacade()
    : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>("org.kde.dummy", QSharedPointer<DummyEventAdaptorFactory>::create())
{
}

DummyResourceFacade::~DummyResourceFacade()
{
}

static std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)> prepareQuery(const Akonadi2::Query &query)
{
    //Compose some functions to make query matching fast.
    //This way we can process the query once, and convert all values into something that can be compared quickly
    std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)> preparedQuery;
    if (!query.ids.isEmpty()) {
        //Match by id
        //TODO: for id's a direct lookup would be way faster

        //We convert the id's to std::string so we don't have to convert each key during the scan. (This runs only once, and the query will be run for every key)
        //Probably a premature optimization, but perhaps a useful technique to be investigated.
        QVector<std::string> ids;
        for (const auto &id : query.ids) {
            ids << id.toStdString();
        }
        preparedQuery = [ids](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
            if (ids.contains(key)) {
                return true;
            }
            return false;
        };
    } else if (!query.propertyFilter.isEmpty()) {
        if (query.propertyFilter.contains("uid")) {
            const QByteArray uid = query.propertyFilter.value("uid").toByteArray();
            preparedQuery = [uid](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
                if (local && local->uid() && (QByteArray::fromRawData(local->uid()->c_str(), local->uid()->size()) == uid)) {
                    return true;
                }
                return false;
            };
        }
    } else {
        //Match everything
        preparedQuery = [](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
            return true;
        };
    }
    return preparedQuery;
}

void DummyResourceFacade::readValue(QSharedPointer<Akonadi2::Storage> storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback, std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)> preparedQuery)
{
    storage->scan(key, [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {

        //Skip internals
        if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
            return true;
        }

        //Extract buffers
        Akonadi2::EntityBuffer buffer(dataValue, dataSize);

        const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(buffer.entity().resource());
        const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::ApplicationDomain::Buffer::Event>(buffer.entity().local());
        const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(buffer.entity().metadata());

        if ((!resourceBuffer && !localBuffer) || !metadataBuffer) {
            qWarning() << "invalid buffer " << QByteArray::fromRawData(static_cast<char*>(keyValue), keySize);
            return true;
        }

        //We probably only want to create all buffers after the scan
        //TODO use adapter for query and scan?
        if (preparedQuery && preparedQuery(std::string(static_cast<char*>(keyValue), keySize), resourceBuffer, localBuffer)) {
            qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            //This only works for a 1:1 mapping of resource to domain types.
            //Not i.e. for tags that are stored as flags in each entity of an imap store.
            auto adaptor = mDomainTypeAdaptorFactory->createAdaptor(buffer.entity());
            //TODO only copy requested properties
            auto memoryAdaptor = QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create(*adaptor);
            // here we could copy additional properties that don't have a 1:1 mapping, such as separately stored tags.
            auto event = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("org.kde.dummy", QByteArray::fromRawData(static_cast<char*>(keyValue), keySize), revision, memoryAdaptor);
            resultCallback(event);
        }
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });
}

Async::Job<qint64> DummyResourceFacade::load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider, qint64 oldRevision, qint64 newRevision)
{
    return Async::start<qint64>([=]() {
        //Now that the sync is complete we can execute the query
        const auto preparedQuery = prepareQuery(query);

        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");
        storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        const qint64 revision = storage->maxRevision();

        //Index lookups
        QVector<QByteArray> keys;
        if (query.propertyFilter.contains("uid")) {
            static Index uidIndex(Akonadi2::Store::storageLocation(), "org.kde.dummy.index.uid", Akonadi2::Storage::ReadOnly);
            uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
                keys << value;
            },
            [](const Index::Error &error) {
                Warning() << "Error in index: " <<  error.message;
            });
        }

        // TODO only emit changes and don't replace everything
        resultProvider->clear();
        // rerun query
        auto resultCallback = std::bind(&Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr>::add, resultProvider, std::placeholders::_1);

        if (keys.isEmpty()) {
            Log() << "Executing a full scan";
            readValue(storage, QByteArray(), resultCallback, preparedQuery);
        } else {
            for (const auto &key : keys) {
                readValue(storage, key, resultCallback, preparedQuery);
            }
        }
        storage->abortTransaction();
        return revision;
    });
}
