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

#ifndef SYNCFOLDER_H
#define SYNCFOLDER_H

#include <QByteArray>
#include <QHash>
#include <QSet>
#include "SyncFile.h"
#include "Common.h"

/*
===========================================================

    SyncFolder

===========================================================
*/
class SyncFolder
{
public:

    explicit SyncFolder(bool paused) : paused(paused){}

    void clearData();
    void clearFilePaths();
    void optimizeMemoryUsage();
    bool isTopFolderUpdated(const SyncFile &file) const;
    bool isActive() const;

    QByteArray path;
    QHash<Hash, SyncFile> files;
    QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>> foldersToRename;
    QHash<Hash, QPair<QByteArray,  QPair<QByteArray, Attributes>>> filesToMove;
    QHash<Hash, QPair<QByteArray, Attributes>> foldersToCreate;
    QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>> filesToCopy;
    QHash<Hash, QByteArray> foldersToRemove;
    QHash<Hash, QByteArray> filesToRemove;
    QSet<QByteArray> foldersToUpdate;

    QDateTime lastSyncDate;
    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
};

#endif // SYNCFOLDER_H
