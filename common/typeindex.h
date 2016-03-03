/*
    Copyright (c) 2015 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/
#pragma once

#include "resultset.h"
#include "bufferadaptor.h"
#include "storage.h"
#include "query.h"
#include <QByteArray>

class TypeIndex
{
public:
    TypeIndex(const QByteArray &type);

    template <typename T>
    void addProperty(const QByteArray &property);
    template <typename T, typename S>
    void addPropertyWithSorting(const QByteArray &property, const QByteArray &sortProperty);

    void add(const QByteArray &identifier, const Sink::ApplicationDomain::BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction);
    void remove(const QByteArray &identifier, const Sink::ApplicationDomain::BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction);

    ResultSet query(const Sink::Query &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::Transaction &transaction);

private:
    QByteArray indexName(const QByteArray &property, const QByteArray &sortProperty = QByteArray()) const;
    QByteArray mType;
    QByteArrayList mProperties;
    QMap<QByteArray, QByteArray> mSortedProperties;
    QHash<QByteArray, std::function<void(const QByteArray &identifier, const QVariant &value, Sink::Storage::Transaction &transaction)>> mIndexer;
    QHash<QByteArray, std::function<void(const QByteArray &identifier, const QVariant &value, const QVariant &sortValue, Sink::Storage::Transaction &transaction)>> mSortIndexer;
};
