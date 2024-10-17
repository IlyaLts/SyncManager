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

#include "SyncFolder.h"
#include "Common.h"

/*
===================
SyncFolder::clearUnnecessaryData
===================
*/
void SyncFolder::clearUnnecessaryData()
{
    for (QHash<Hash, SyncFile>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        // If a file doesn't have a path for some reason, then that means that the file doesn't exist at all.
        // So, it is better to remove it from the database to prevent further synchronization issues.
        if (fileIt->path.isEmpty())
        {
            fileIt = files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
        }
        // Otherwise, clears the path manually as we don't need it at the end of a synchronization session.
        else
        {
            fileIt->path.clear();
            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::optimize
===================
*/
void SyncFolder::optimize()
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
bool SyncFolder::isTopFolderUpdated(const SyncFile &file) const
{
    return files.value(hash64(QByteArray(file.path).remove(file.path.indexOf('/'), file.path.size()))).updated();
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
