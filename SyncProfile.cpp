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

#include <QMutex>
#include "SyncProfile.h"
#include "SyncFolder.h"

/*
===================
SyncProfile::operator =
===================
*/
void SyncProfile::operator =(const SyncProfile &other)
{
    folders = other.folders;
    excludeList = other.excludeList;

    syncing = other.syncing;
    paused = other.paused;
    toBeRemoved = other.toBeRemoved;
    syncTime = other.syncTime;
    lastSyncDate = other.lastSyncDate;
    name = other.name;
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    if (!filePaths.contains(Hash(hash)))
    {
        mutex.lock();
        auto it = filePaths.insert(Hash(hash), path);
        it->squeeze();
        mutex.unlock();
    }
}

/*
===================
SyncProfile::isActive
===================
*/
bool SyncProfile::isActive() const
{
    int activeFolders = 0;

    for (auto &folder : folders)
        if (folder.isActive())
            activeFolders++;

    return !paused && !toBeRemoved && activeFolders >= 2;
}
