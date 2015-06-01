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

#pragma once

#include "entity_generated.h"
#include <QVariant>
#include <QByteArray>
#include <functional>
#include "clientapi.h" //for domain parts

#include "entity_generated.h"
#include "metadata_generated.h"
#include "entitybuffer.h"
#include "propertymapper.h"
#include "domain/event.h"
#include "log.h"

/**
 * Create a buffer from a domain object using the provided mappings
 */
template <class Builder, class Buffer>
flatbuffers::Offset<Buffer> createBufferPart(const Akonadi2::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, const WritePropertyMapper<Builder> &mapper)
{
    //First create a primitives such as strings using the mappings
    QList<std::function<void(Builder &)> > propertiesToAddToResource;
    for (const auto &property : domainObject.changedProperties()) {
        qWarning() << "copying property " << property;
        const auto value = domainObject.getProperty(property);
        if (mapper.hasMapping(property)) {
            mapper.setProperty(property, domainObject.getProperty(property), propertiesToAddToResource, fbb);
        } else {
            qWarning() << "no mapping for property available " << property;
        }
    }

    //Then create all porperties using the above generated builderCalls
    Builder builder(fbb);
    for (auto propertyBuilder : propertiesToAddToResource) {
        propertyBuilder(builder);
    }
    return builder.Finish();
}

/**
 * Create the buffer and finish the FlatBufferBuilder.
 * 
 * After this the buffer can be extracted from the FlatBufferBuilder object.
 */
template <typename Buffer, typename BufferBuilder>
static void createBufferPartBuffer(const Akonadi2::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, WritePropertyMapper<BufferBuilder> &mapper)
{
    auto pos = createBufferPart<BufferBuilder, Buffer>(domainObject, fbb, mapper);
    // Because we cannot template the following call
    // Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(fbb, pos);
    // FIXME: This means all buffers in here must have the AKFB identifier
    fbb.Finish(pos, "AKFB");
    flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
    if (!verifier.VerifyBuffer<Buffer>()) {
        Warning() << "Created invalid uffer";
    }
}

/**
 * A generic adaptor implementation that uses a property mapper to read/write values.
 * 
 * TODO: this is the read-only part. Create a write only equivalent
 */
template <class LocalBuffer, class ResourceBuffer>
class GenericBufferAdaptor : public Akonadi2::ApplicationDomain::BufferAdaptor
{
public:
    GenericBufferAdaptor()
        : BufferAdaptor()
    {

    }

    //TODO remove
    void setProperty(const QByteArray &key, const QVariant &value)
    {
    }

    virtual QVariant getProperty(const QByteArray &key) const
    {
        if (mResourceBuffer && mResourceMapper->hasMapping(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer && mLocalMapper->hasMapping(key)) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        qWarning() << "no mapping available for key " << key;
        return QVariant();
    }

    virtual QList<QByteArray> availableProperties() const
    {
        QList<QByteArray> props;
        props << mResourceMapper->availableProperties();
        props << mLocalMapper->availableProperties();
        return props;
    }

    LocalBuffer const *mLocalBuffer;
    ResourceBuffer const *mResourceBuffer;
    QSharedPointer<ReadPropertyMapper<LocalBuffer> > mLocalMapper;
    QSharedPointer<ReadPropertyMapper<ResourceBuffer> > mResourceMapper;
};

template<typename DomainType>
class DomainTypeAdaptorFactoryInterface
{
public:
    virtual ~DomainTypeAdaptorFactoryInterface() {};
    virtual QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity) = 0;
    virtual void createBuffer(const DomainType &domainType, flatbuffers::FlatBufferBuilder &fbb) = 0;
};

/**
 * The factory should define how to go from an entitybuffer (local + resource buffer), to a domain type adapter.
 * It defines how values are split accross local and resource buffer.
 * This is required by the facade the read the value, and by the pipeline preprocessors to access the domain values in a generic way.
 */
template<typename DomainType, typename ResourceBuffer, typename ResourceBuilder>
class DomainTypeAdaptorFactory : public DomainTypeAdaptorFactoryInterface<DomainType>
{
    typedef typename Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::Buffer LocalBuffer;
    typedef typename Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::BufferBuilder LocalBuilder;
public:
    DomainTypeAdaptorFactory() :
        mLocalMapper(Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::initializeReadPropertyMapper()),
        mResourceMapper(QSharedPointer<ReadPropertyMapper<ResourceBuffer> >::create()),
        mLocalWriteMapper(Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::initializeWritePropertyMapper()),
        mResourceWriteMapper(QSharedPointer<WritePropertyMapper<ResourceBuilder> >::create())
    {};
    virtual ~DomainTypeAdaptorFactory() {};

    /**
     * Creates an adaptor for the given domain and resource types.
     * 
     * This returns by default a GenericBufferAdaptor initialized with the corresponding property mappers.
     */
    virtual QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity) Q_DECL_OVERRIDE
    {
        const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<ResourceBuffer>(entity.resource());
        const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<LocalBuffer>(entity.local());
        // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());

        auto adaptor = QSharedPointer<GenericBufferAdaptor<LocalBuffer, ResourceBuffer> >::create();
        adaptor->mLocalBuffer = localBuffer;
        adaptor->mLocalMapper = mLocalMapper;
        adaptor->mResourceBuffer = resourceBuffer;
        adaptor->mResourceMapper = mResourceMapper;
        return adaptor;
    }

    virtual void createBuffer(const DomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE
    {
        flatbuffers::FlatBufferBuilder localFbb;
        if (mLocalWriteMapper) {
            createBufferPartBuffer<LocalBuffer, LocalBuilder>(domainObject, localFbb, *mLocalWriteMapper);
        }

        flatbuffers::FlatBufferBuilder resFbb;
        if (mResourceWriteMapper) {
            createBufferPartBuffer<ResourceBuffer, ResourceBuilder>(domainObject, resFbb, *mResourceWriteMapper);
        }

        Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, 0, 0, resFbb.GetBufferPointer(), resFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
    }


protected:
    QSharedPointer<ReadPropertyMapper<LocalBuffer> > mLocalMapper;
    QSharedPointer<ReadPropertyMapper<ResourceBuffer> > mResourceMapper;
    QSharedPointer<WritePropertyMapper<LocalBuilder> > mLocalWriteMapper;
    QSharedPointer<WritePropertyMapper<ResourceBuilder> > mResourceWriteMapper;
};


