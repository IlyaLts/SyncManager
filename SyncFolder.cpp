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

#include "SyncFolder.h"
#include "SyncProfile.h"
#include "Common.h"
#include <QMutex>

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
SyncFolder::updateVersioningPath
===================
*/
void SyncFolder::updateVersioningPath(const QString &folder, const QString &pattern)
{
    versioningPath.assign(path);
    versioningPath.remove(versioningPath.lastIndexOf("/", 1), versioningPath.size());
    versioningPath.append("_");
    versioningPath.append(folder);
    versioningPath.append("/");
    versioningPath.append(QDateTime::currentDateTime().toString(pattern));
    versioningPath.append("/");
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
