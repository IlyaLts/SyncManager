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
bool SyncFile::isOlder(const SyncFile &otherFile) const
{
    // Has the file type or if both file types are different
    if (type != SyncFile::File && type == otherFile.type)
        return false;

    if (!exists || !otherFile.exists)
        return false;

    if (!updated && otherFile.updated)
        return true;

#ifdef Q_OS_LINUX
    if (updated && otherFile.updated && date < otherFile.date)
        return true;
#else
    if (updated == otherFile.updated && date < otherFile.date)
        return true;
#endif

    return false;
}
