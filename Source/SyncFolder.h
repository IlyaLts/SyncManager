/*
===============================================================================
    Copyright (C) 2022-2026 Ilya Lyakhovets

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
#include <QByteArray>
#include <QHash>
#include <QSet>

class SyncProfile;

struct ConflictedFileToRenameInfo
{
    QByteArray path;
};

struct FolderToRenameInfo
{
    QByteArray toPath;
    QByteArray fromPath;
    attributes_t attributes;
};

struct FileToMoveInfo
{
    QByteArray toPath;
    QByteArray fromPath;
    attributes_t attributes;
};

struct FolderToCreateInfo
{
    QByteArray path;
    attributes_t attributes;
};

struct FileToCopyInfo
{
    QByteArray toPath;
    QByteArray fromFullPath;
    QDateTime modifiedDate;
};

using Files = QHash<SyncHash, SyncFile>;
using ConflictedFileRenameList = QHash<SyncHash, ConflictedFileToRenameInfo>;
using FolderRenameList = QHash<SyncHash, FolderToRenameInfo>;
using FileMoveList = QHash<SyncHash, FileToMoveInfo>;
using FolderCreateList = QHash<SyncHash, FolderToCreateInfo>;
using FileCopyList = QHash<SyncHash, FileToCopyInfo>;
using FolderRemoveList = QHash<SyncHash, QByteArray>;
using FileRemoveList = QHash<SyncHash, QByteArray>;
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

    inline bool operator ==(const SyncFolder &other) const { return m_path == other.m_path; }

    void loadSettings();
    void saveSettings() const;
    void removeSettings() const;

    void createParentFolders(const QByteArray &path);
    bool removeFile(const QString &path, SyncFile::Type type);
    void cleanup();
    void clearData();
    void optimizeMemoryUsage();
    void updateVersioningPath();
    void checkCaseSensitive();
    void loadDatabasesLocally();
    void loadDatabasesDecentralised();
    void saveDatabasesLocally();
    void saveDatabasesDecentralised();
    void removeDatabase() const;
    void removeNonexistentFiles();
    bool active() const;
    bool hasUnsyncedFiles() const;
    bool partiallySynchronized() const;
    void updateUnsyncedList();
    void checkForFolderContentPrecedence();
    void checkForCorruptedFiles();
    void checkForConflictedFiles();
    void removeNonexistentFileData();
    void remove();

    int scanFiles();

    void renameConflictedFiles();
    void renameFolders();
    void moveFiles();
    void removeFolders();
    void removeFiles();
    void createFolders();
    void copyFiles();
    void updateFolderModifiedDates();

    void setType(Type type);
    inline void setLastSyncDate(const QDateTime &date) { m_lastSyncDate = date; }
    inline void setSyncing(bool syncing) { m_syncing = syncing; }
    void setPaused(bool paused);
    inline void checkExistence(){ m_exists = QFileInfo::exists(m_path); }
    inline void setDatabaseDirty() { m_databaseChanged = true; }

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
    inline bool hasCorruptedFiles() const { return m_hasCorruptedFiles; };
    inline bool hasConflictedFiles() const { return m_hasConflictedFiles; };
    inline bool databaseChanged() const { return m_databaseChanged; }

    void addConflictedFileToRename(SyncHash hash, const QByteArray &path);
    void addFolderToRename(SyncHash hash, const QByteArray &toPath, const QByteArray &fromPath, attributes_t attributes);
    void addFileToMove(SyncHash hash, const QByteArray &toPath, const QByteArray &fromPath, attributes_t attributes);
    void addFolderToCreate(SyncHash hash, const QByteArray &path, attributes_t attributes);
    void addFileToCopy(SyncHash hash, const QByteArray &toPath, const QByteArray &fromFullPath, const QDateTime &modifiedDate);
    void addFolderToRemove(SyncHash hash, const QByteArray &path);
    void addFileToRemove(SyncHash hash, const QByteArray &path);

    QDateTime filesToCopyModifiedDate(SyncHash hash) const { return m_filesToCopy.value(hash).modifiedDate; }

    inline bool hasFolderToRename(SyncHash hash) { return m_foldersToRename.contains(hash); }
    inline bool hasFileToMove(SyncHash hash) { return m_filesToMove.contains(hash); }
    inline bool hasFolderToCreate(SyncHash hash) { return m_foldersToCreate.contains(hash); }
    inline bool hasFileToCopy(SyncHash hash) { return m_filesToCopy.contains(hash); }
    inline bool hasFolderToRemove(SyncHash hash) { return m_foldersToRemove.contains(hash); }
    inline bool hasFileToRemove(SyncHash hash) { return m_filesToRemove.contains(hash); }

    inline void removeFileToMove(SyncHash hash) { m_filesToMove.remove(hash); }
    inline void removeFolderToRemove(SyncHash hash) { m_foldersToRemove.remove(hash); }
    inline void removeFileToRemove(SyncHash hash) { m_filesToRemove.remove(hash); }

    inline qsizetype conflictedFilesToRenameSize() const { return m_conflictedFilesToRename.size(); };
    inline qsizetype foldersToRenameSize() const { return m_foldersToRename.size(); };
    inline qsizetype filesToMoveSize() const { return m_filesToMove.size(); };
    inline qsizetype foldersToCreateSize() const { return m_foldersToCreate.size(); };
    inline qsizetype filesToCopySize() const { return m_filesToCopy.size(); };
    inline qsizetype foldersToRemoveSize() const { return m_foldersToRemove.size(); };
    inline qsizetype filesToRemoveSize() const { return m_filesToRemove.size(); };

    inline Files &files() { return m_files; };
    inline const Files &files() const { return m_files; };
    inline const FolderRenameList &foldersToRename() const { return m_foldersToRename; };
    inline const FileMoveList &filesToMove() const { return m_filesToMove; };

    inline SyncProfile &profile() const { return *m_profile; }

private:

    void loadDatabase(const QString &path);
    void saveDatabase(const QString &path) const;

    Files m_files;
    ConflictedFileRenameList m_conflictedFilesToRename;
    FolderRenameList m_foldersToRename;
    FileMoveList m_filesToMove;
    FolderCreateList m_foldersToCreate;
    FileCopyList m_filesToCopy;
    FolderRemoveList m_foldersToRemove;
    FileRemoveList m_filesToRemove;
    FolderUpdateList m_foldersToUpdate;

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
    bool m_hasCorruptedFiles = false;
    bool m_hasConflictedFiles = false;
    bool m_databaseChanged = false;

    hash64_t m_deviceHash;
    SyncProfile *m_profile;
};

#endif // SYNCFOLDER_H
