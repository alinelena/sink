/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#pragma once

#include "datasetdefinition.h"

#include "state.h"
#include "common/storage.h"

#include <QHash>
#include <QVariant>

namespace HAWD
{

class Dataset
{
public:
    class Row
    {
        public:
            Row(const Row &other);
            Row &operator=(const Row &rhs);
            void setValue(const QString &column, const QVariant &value);
            QVariant value(const QString &column);
            void annotate(const QString &note);
            qint64 key() const;
            QByteArray toBinary() const;
            QString toString() const;

        private:
            Row();
            Row(const Dataset &dataset, qint64 key = 0);
            void fromBinary(QByteArray &binary);

            qint64 m_key;
            QHash<QString, DataDefinition> m_columns;
            QHash<QString, QVariant> m_data;
            QString m_annotation;
            const Dataset *m_dataset;
            friend class Dataset;
    };

    Dataset(const DatasetDefinition &definition);
    Dataset(const QString &name, const State &state);
    ~Dataset();

    bool isValid();
    const DatasetDefinition &definition() const;

    qint64 insertRow(const Row &row);
    void removeRow(const Row &row);
    Row row(qint64 key = 0);
    Row lastRow();
    //TODO: row cursor

private:
    DatasetDefinition m_definition;
    Storage m_storage;
};

} // namespace HAWD
