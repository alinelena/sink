/*
 *   Copyright (C) 2016 Aaron Seigo <aseigo@kde.org>
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

#include "utils.h"

namespace Utils {

QStringList filteredCompletions(const QStringList &possibleCompletions, const QString &commandFragment, Qt::CaseSensitivity cs)
{
    if (commandFragment.isEmpty()) {
        return possibleCompletions;
    }

    QStringList filtered;
    for (auto item : possibleCompletions) {
        if (item.startsWith(commandFragment, cs)) {
            filtered << item;
        }
    }

    return filtered;
}

} // namespace Utils
