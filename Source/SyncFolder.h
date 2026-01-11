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

    explicit SyncFolder(SyncProfile *profile, const QByteArray &path);

    inline bool operator ==(const SyncFolder &other) { return m_path == other.m_path; }

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
    void updateUnsyncedList();
    void remove();

    void setType(Type type);
    inline void setLastSyncDate(const QDateTime &date) { m_lastSyncDate = date; }
    inline void setSyncing(bool syncing) { m_syncing = syncing; }
    void setPaused(bool paused);
    inline void checkExistence(){ m_exists = QFileInfo::exists(m_path); }

    inline bool bidirectional() const { return m_type == TWO_WAY; }
    inline bool mirroring() const { return m_type == ONE_WAY; }
    inline bool contributing() const { return m_type == ONE_WAY_UPDATE; }
    inline Type type() const { return m_type; }
    inline const QByteArray &path() const { return m_path; }
    inline const QDateTime &lastSyncDate() const { return m_lastSyncDate; }
    inline const QString &unsyncedList() const { return m_unsyncedList; }
    inline bool exists() const { return m_exists; }
    inline bool syncing() const { return m_syncing; }
    inline bool paused() const { return m_paused; }
    inline bool toBeRemoved() const { return m_toBeRemoved; }
    inline bool caseSensitive() const { return m_caseSensitive; }

    inline SyncProfile &profile() const { return *m_profile; }

    Files files;
    FolderRenameList foldersToRename;
    FileMoveList filesToMove;
    FolderCreateList foldersToCreate;
    FileCopyList filesToCopy;
    FolderRemoveList foldersToRemove;
    FileRemoveList filesToRemove;
    FolderUpdateList foldersToUpdate;

private:

    Type m_type = TWO_WAY;
    QByteArray m_path;
    QDateTime m_lastSyncDate;
    QString m_unsyncedList;
    QString m_versioningPath;
    bool m_exists = true;
    bool m_syncing = false;
    bool m_paused = false;
    bool m_toBeRemoved = false;
    bool m_caseSensitive = false;

    SyncProfile *m_profile;
};

#endif // SYNCFOLDER_H
