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

#include "SyncFile.h"
#include "Common.h"
#include <QByteArray>
#include <QHash>
#include <QSet>

class SyncProfile;

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

using Files = QHash<Hash, SyncFile>;
using FolderRenameList = QHash<Hash, FolderToRenameInfo>;
using FileMoveList = QHash<Hash, FileToMoveInfo>;
using FolderCreateList = QHash<Hash, FolderToCreateInfo>;
using FileCopyList = QHash<Hash, FileToCopyInfo>;
using FolderRemoveList = QHash<Hash, QByteArray>;
using FileRemoveList = QHash<Hash, QByteArray>;
using FolderUpdateList = QSet<QByteArray>;

/*
===========================================================

    SyncFolder

===========================================================
*/
class SyncFolder
{
public:

    enum Type
    {
        TWO_WAY,
        ONE_WAY,
        ONE_WAY_UPDATE
    };

    explicit SyncFolder(SyncProfile *profile) { m_profile = profile; }

    inline bool operator ==(const SyncFolder &other) { return path == other.path; }

    void loadSettings();
    void saveSettings() const;
    void removeSettings() const;

    void createParentFolders(QByteArray path);
    bool removeFile(const QString &path, SyncFile::Type type);
    void cleanup();
    void clearData();
    void optimizeMemoryUsage();
    void updateVersioningPath();
    void checkCaseSensitive();
    void saveToDatabase(const QString &path) const;
    void loadFromDatabase(const QString &path);
    void removeDatabase() const;
    void removeNonExistentFiles();
    bool isActive() const;
    bool hasUnsyncedFiles() const;

    inline bool bidirectional() const { return type == TWO_WAY; }
    inline bool mirroring() const { return type == ONE_WAY; }
    inline bool contributing() const { return type == ONE_WAY_UPDATE; }
    inline bool caseSensitive() const { return m_caseSensitive; }

    inline SyncProfile &profile() const { return *m_profile; }

    Type type = TWO_WAY;
    QByteArray path;
    QString versioningPath;
    Files files;
    FolderRenameList foldersToRename;
    FileMoveList filesToMove;
    FolderCreateList foldersToCreate;
    FileCopyList filesToCopy;
    FolderRemoveList foldersToRemove;
    FileRemoveList filesToRemove;
    FolderUpdateList foldersToUpdate;

    QString unsyncedList;
    QDateTime lastSyncDate;
    bool partiallySynchronized = false;
    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;

private:

    bool m_caseSensitive = false;

    SyncProfile *m_profile;
};

#endif // SYNCFOLDER_H
