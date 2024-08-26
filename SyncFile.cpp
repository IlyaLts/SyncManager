/*
===============================================================================
    Copyright (C) 2022-2024 Ilya Lyakhovets

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

/*
===================
SyncFile::isOlder
===================
*/
bool SyncFile::isOlder(const SyncFile &other) const
{
    // Has the file type or if both file types are different
    if (type != SyncFile::File && type == other.type)
        return false;

    if (!exists() || !other.exists())
        return false;

    if (!updated() && other.updated())
        return true;

#ifdef Q_OS_LINUX
    // Linux doesn't preserve the original modification date, so the updated flags on both files should be mandatory
    if (updated && other.updated && date < other.date)
        return true;
#else
    // Checking the updated flags on both files allows detection of a newly added file with an older modification date
    if (updated() == other.updated() && date < other.date)
        return true;
#endif

    return false;
}
