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

#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include <QList>
#include <QSet>
#include <QMap>
#include <QQueue>
#include <QTimer>
#include <QDateTime>

#include "SyncFile.h"
#include "SyncFolder.h"
#include "SyncProfile.h"

#define DATA_FOLDER_PATH        ".SyncManager"
#define DATABASE_FILENAME       "db"
#define DATABASE_VERSION        2
#define SYNC_MIN_DELAY          1000
#define NOTIFICATION_COOLDOWN   300000
#define MOVED_FILES_MIN_SIZE    0

/*
===========================================================

    SyncManager

===========================================================
*/
class SyncManager : public QObject
{
    Q_OBJECT

public:

    enum SyncingMode
    {
        Automatic,
        Manual
    };

    enum DeletionMode
    {
        MoveToTrash,
        Versioning,
        DeletePermanently
    };

    enum DatabaseLocation
    {
        Locally,
        Decentralized
    };

    SyncManager();
    ~SyncManager();

    void addToQueue(SyncProfile *profile);
    void sync();

    void updateTimer(SyncProfile &profile);
    void updateStatus();
    void updateNextSyncingTime();

    void saveFileDataLocally(const SyncProfile &profile) const;
    void saveFileDataDecentralised(const SyncProfile &profile) const;
    void loadFileDataLocally(SyncProfile &profile);
    void loadFileDataDecentralised(SyncProfile &profile);
    void removeFileData();
    void removeFileData(const SyncFolder &folder);

    inline void setSyncingMode(SyncingMode mode) { m_syncingMode = mode; }
    inline void setDeletionMode(DeletionMode mode) { m_deletionMode = mode; }
    inline SyncingMode syncingMode() const { return m_syncingMode; }
    inline DeletionMode deletionMode() const { return m_deletionMode; }
    inline const QQueue<SyncProfile *> &queue() const { return m_queue; }
    inline const QList<SyncProfile> &profiles() const { return m_profiles; }
    inline QList<SyncProfile> &profiles() { return m_profiles; }

    inline void shouldQuit() { m_shouldQuit = true; }
    inline void setSyncTimeMultiplier(int multiplier) { m_syncTimeMultiplier = multiplier; }
    inline void setPaused(bool paused) { m_paused = paused; }
    inline void enableNotifications(bool enable) { m_notifications = enable; }
    inline void enableDatabaseSaving(bool enable) { m_saveDatabase = enable; }
    inline void enableIgnoreHiddenFiles(bool enable) { m_ignoreHiddenFiles = enable; }
    inline void setDatabaseLocation(DatabaseLocation location) { m_databaseLocation = location; }
    inline void enableDetectMovedFiles(bool enable) { m_detectMovedFiles = enable; }

    inline int filesToSync() const { return m_filesToSync; }
    inline int syncTimeMultiplier() const { return m_syncTimeMultiplier; }
    inline int existingProfiles() const { return m_existingProfiles; }
    inline int movedFileMinSize() const { return m_movedFileMinSize; }
    inline bool isCaseSensitiveSystem() const { return m_caseSensitiveSystem; }
    inline bool isQuitting() const { return m_shouldQuit; }
    inline bool isThereIssue() const { return m_issue; }
    inline bool isThereWarning() const { return m_warning; }
    inline bool isBusy() const { return m_busy; }
    inline bool isPaused() const { return m_paused; }
    inline bool isSyncing() const { return m_syncing; }
    bool isThereHiddenProfile() const;
    inline bool notificationsEnabled() const { return m_notifications; }
    inline bool saveDatabaseEnabled() const { return m_saveDatabase; }
    inline DatabaseLocation databaseLocation() const { return m_databaseLocation; }
    inline bool ignoreHiddenFilesEnabled() const { return m_ignoreHiddenFiles; }
    inline bool detectMovedFilesEnabled() const { return m_detectMovedFiles; }
    inline const QString &versionFolder() const { return m_versionFolder; }
    inline const QString &versionPattern() const { return m_versionPattern; }

Q_SIGNALS:

    void warning(const QString &title, const QString &message);
    void profileSynced(SyncProfile *profile);

private:

    void saveToFileData(const SyncFolder &folder, const QString &path) const;
    void loadFromFileData(SyncFolder &folder, const QString &path);
    bool syncProfile(SyncProfile &profile);
    int getListOfFiles(SyncProfile &profile, SyncFolder &folder);
    void synchronizeFileAttributes(SyncProfile &profile);
    void checkForRenamedFolders(SyncProfile &profile);
    void checkForMovedFiles(SyncProfile &profile);
    void checkForAddedFiles(SyncProfile &profile);
    void checkForRemovedFiles(SyncProfile &profile);
    void checkForChanges(SyncProfile &profile);
    bool removeFile(SyncProfile &profile, SyncFolder &folder, const QString &path, const QString &fullPath, const QString &versioningPath, SyncFile::Type type);
    void renameFolders(SyncProfile &profile, SyncFolder &folder);
    void moveFiles(SyncProfile &profile, SyncFolder &folder);
    void createParentFolders(SyncProfile &profile, SyncFolder &folder, QByteArray path);
    void removeFolders(SyncProfile &profile, SyncFolder &folder, const QString &versioningPath);
    void removeFiles(SyncProfile &profile, SyncFolder &folder, const QString &versioningPath);
    void createFolders(SyncProfile &profile, SyncFolder &folder, const QString &versioningPath);
    void copyFiles(SyncProfile &profile, SyncFolder &folder, const QString &versioningPath);
    void syncFiles(SyncProfile &profile);

    SyncingMode m_syncingMode;
    DeletionMode m_deletionMode;

    QQueue<SyncProfile *> m_queue;
    QList<SyncProfile> m_profiles;

    int m_filesToSync = 0;
    int m_syncTimeMultiplier = 1;
    int m_existingProfiles = 0;
    int m_movedFileMinSize = 0;
    bool m_databaseChanged = false;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    bool m_caseSensitiveSystem = false;
#else
    bool m_caseSensitiveSystem = true;
#endif

    bool m_shouldQuit = false;
    bool m_issue = true;
    bool m_warning = false;
    bool m_busy = false;
    bool m_paused = false;
    bool m_syncing = false;
    bool m_notifications = true;
    bool m_saveDatabase = false;
    DatabaseLocation m_databaseLocation = Decentralized;
    bool m_ignoreHiddenFiles = false;
    bool m_detectMovedFiles = false;

    QMap<QString, QTimer *> m_notificationList;
    QSet<hash64_t> m_usedDevices;
    QMutex usedDevicesMutex;

    QString m_versionFolder;
    QString m_versionPattern;
};

#endif // SYNCMANAGER_H
