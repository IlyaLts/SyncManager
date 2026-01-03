/*
===============================================================================
    Copyright (C) 2022-2025 Ilya Lyakhovets

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
===============================================================================
*/

#include "SyncFile.h"
#include "Common.h"

/*
===================
SyncFile::isOlder
===================
*/
bool SyncFile::isOlder(const SyncFile &other) const
{
    // Must have the file type, or both file types must be different
    if (type != SyncFile::File && type == other.type)
        return false;

    if (!exists() || !other.exists())
        return false;

    if (!updated() && other.updated())
        return true;

#if defined(Q_OS_WIN) || defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
    // Checking the updated flags on both files allows detection of a newly added file with an older modification date
    if (updated() == other.updated() && modifiedDate < other.modifiedDate)
        return true;
#else
    // Linux and MacOS don't preserve the original modification date by default, so the updated flags on both files should be mandatory
    if (updated() && other.updated() && modifiedDate < other.modifiedDate)
        return true;
#endif

    return false;
}

/*
===================
SyncFile::hasOlderAttributes
===================
*/
bool SyncFile::hasOlderAttributes(const SyncFile &other) const
{
    if (type != other.type)
        return false;

    if (!attributesUpdated() && other.attributesUpdated())
        return true;

    if (!attributesUpdated() && !other.attributesUpdated())
        if (attributes != other.attributes)
            return true;

    return false;
}

/*
===================
SyncFile::hasSameSizeAndDate
===================
*/
bool SyncFile::hasSameSizeAndDate(const SyncFile &other) const
{
    if (size != other.size)
        return false;

#if defined(Q_OS_WIN) || defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
    if (modifiedDate != other.modifiedDate)
        return false;
#endif

    return true;
}
