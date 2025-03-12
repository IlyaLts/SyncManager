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

#ifndef SYNCFOLDER_H
#define SYNCFOLDER_H

#include "SyncProfile.h"
#include "SyncFile.h"
#include "Common.h"
#include <QByteArray>
#include <QHash>
#include <QSet>

struct FolderToRenameInfo
{
    QByteArray toPath;
    QByteArray fromPath;
    Attributes attributes;
};

struct FileToMoveInfo
{
    QByteArray toPath;
    QByteArray fromPath;
    Attributes attributes;
};

struct FolderToCreateInfo
{
    QByteArray path;
    Attributes attributes;
};

struct FileToCopyInfo
{
    QByteArray toPath;
    QByteArray fromFullPath;
    QDateTime modifiedDate;
};

/*
===========================================================

    SyncFolder

===========================================================
*/
class SyncFolder
{
public:

    void clearData();
    void optimizeMemoryUsage();
    void updateVersioningPath(const QString &folder, const QString &pattern);
    void saveToDatabase(const QString &path) const;
    void loadFromDatabase(const QString &path);
    void removeDatabase() const;
    bool isActive() const;

    QByteArray path;
    QString versioningPath;
    QHash<Hash, SyncFile> files;
    QHash<Hash, FolderToRenameInfo> foldersToRename;
    QHash<Hash, FileToMoveInfo> filesToMove;
    QHash<Hash, FolderToCreateInfo> foldersToCreate;
    QHash<Hash, FileToCopyInfo> filesToCopy;
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
