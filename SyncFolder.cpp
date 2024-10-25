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
#include "SyncFolder.h"
#include "SyncProfile.h"
#include "Common.h"

/*
===================
SyncFolder::clearData
===================
*/
void SyncFolder::clearData()
{
    files.clear();
    foldersToRename.clear();
    filesToMove.clear();
    foldersToCreate.clear();
    filesToCopy.clear();
    foldersToRemove.clear();
    filesToRemove.clear();
    foldersToUpdate.clear();
}

/*
===================
SyncFolder::removeInvalidFileData

If a file doesn't have a path for some reason, then that means that the file doesn't exist at all.
So, it is better to remove it from the database to prevent further synchronization issues.
===================
*/
void SyncFolder::removeInvalidFileData(SyncProfile &profile)
{
    for (QHash<Hash, SyncFile>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        if (!profile.hasFilePath(fileIt.key()))
            fileIt = files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
SyncFolder::optimizeMemoryUsage
===================
*/
void SyncFolder::optimizeMemoryUsage()
{
    files.squeeze();
    filesToMove.squeeze();
    foldersToCreate.squeeze();
    filesToCopy.squeeze();
    foldersToRemove.squeeze();
    filesToRemove.squeeze();
}

/*
===================
SyncFolder::isTopFolderUpdated
===================
*/
bool SyncFolder::isTopFolderUpdated(SyncProfile &profile, hash64_t hash) const
{
    QByteArray path = profile.getFilePath(hash);
    return files.value(hash64(QByteArray(path).remove(path.indexOf('/'), path.size()))).updated();
}

/*
===================
SyncFolder::isActive
===================
*/
bool SyncFolder::isActive() const
{
    return !paused && !toBeRemoved && exists;
}
