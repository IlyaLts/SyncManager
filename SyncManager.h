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

    SyncManager();
    ~SyncManager();

    void addToQueue(int profileNumber = -1);
    void sync();

    void updateTimer();
    void updateStatus();
    void updateNextSyncingTime();

    void saveFileDataInternally() const;
    void saveFileDataLocally() const;
    void loadFileDataInternally();
    void loadFileDataLocally();
    void removeFileData();
    void removeFileData(const SyncFolder &folder);

    inline void setSyncingMode(SyncingMode mode) { m_syncingMode = mode; }
    inline void setDeletionMode(DeletionMode mode) { m_deletionMode = mode; }
    inline SyncingMode syncingMode() const { return m_syncingMode; }
    inline DeletionMode deletionMode() const { return m_deletionMode; }
    inline const QQueue<int> &queue() const { return m_queue; }
    inline const QList<SyncProfile> &profiles() const { return m_profiles; }
    inline QList<SyncProfile> &profiles() { return m_profiles; }
    inline QTimer &syncTimer() { return m_syncTimer; }

    inline void shouldQuit() { m_shouldQuit = true; }
    inline void setSyncTimeMultiplier(int multiplier) { m_syncTimeMultiplier = multiplier; }
    inline void setPaused(bool paused) { m_paused = paused; }
    inline void setSyncHidden(bool hidden) { m_syncHidden = hidden; }
    inline void enableNotifications(bool enable) { m_notifications = enable; }
    inline void enableRememberFiles(bool enable) { m_rememberFiles = enable; }
    inline void enableSaveDataLocally(bool enable) { m_saveDataLocally = enable; }
    inline void enableDetectMovedFiles(bool enable) { m_detectMovedFiles = enable; }

    inline int filesToSync() const { return m_filesToSync; }
    inline int syncTimeMultiplier() const { return m_syncTimeMultiplier; }
    inline int syncEvery() const { return m_syncEvery; }
    inline int existingProfiles() const { return m_existingProfiles; }
    inline int movedFileMinSize() const { return m_movedFileMinSize; }
    inline bool isCaseSensitiveSystem() const { return m_caseSensitiveSystem; }
    inline bool isQuitting() const { return m_shouldQuit; }
    inline bool isThereIssue() const { return m_issue; }
    inline bool isThereWarning() const { return m_warning; }
    inline bool isBusy() const { return m_busy; }
    inline bool isPaused() const { return m_paused; }
    inline bool isSyncing() const { return m_syncing; }
    inline bool isSyncHidden() const { return m_syncHidden; }
    inline bool notificationsEnabled() const { return m_notifications; }
    inline bool rememberFilesEnabled() const { return m_rememberFiles; }
    inline bool saveDataLocallyEnabled() const { return m_saveDataLocally; }
    inline bool detectMovedFilesEnabled() const { return m_detectMovedFiles; }
    inline const QString &versionFolder() const { return m_versionFolder; }
    inline const QString &versionPattern() const { return m_versionPattern; }

Q_SIGNALS:

    void warning(const QString &title, const QString &message);
    void profileSynced(SyncProfile *profile);

private:

    void saveToFileData(const SyncFolder &folder, QDataStream &stream) const;
    void loadFromFileData(SyncFolder &folder, QDataStream &stream, bool dry = false);
    bool syncProfile(SyncProfile &profile);
    int getListOfFiles(SyncFolder &folder, const QList<QByteArray> &excludeList);
    void synchronizeFileAttributes(SyncProfile &profile);
    void checkForRenamedFolders(SyncProfile &profile);
    void checkForMovedFiles(SyncProfile &profile);
    void checkForAddedFiles(SyncProfile &profile);
    void checkForRemovedFiles(SyncProfile &profile);
    void checkForChanges(SyncProfile &profile);
    bool removeFile(SyncFolder &folder, const QString &path, const QString &fullPath, const QString &versioningPath, SyncFile::Type type);
    void renameFolders(SyncFolder &folder);
    void moveFiles(SyncFolder &folder);
    void createParentFolders(SyncFolder &folder, QByteArray path);
    void removeFolders(SyncFolder &folder, const QString &versioningPath);
    void removeFiles(SyncFolder &folder, const QString &versioningPath);
    void createFolders(SyncFolder &folder, const QString &versioningPath);
    void copyFiles(SyncFolder &folder, const QString &versioningPath);
    void syncFiles(SyncProfile &profile);

    SyncingMode m_syncingMode;
    DeletionMode m_deletionMode;

    QQueue<int> m_queue;
    QList<SyncProfile> m_profiles;
    QTimer m_syncTimer;

    int m_filesToSync = 0;
    int m_syncTimeMultiplier = 1;
    int m_syncEvery = 0;
    int m_existingProfiles = 0;
    int m_movedFileMinSize = 0;

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
    bool m_syncHidden = false;
    bool m_notifications = true;
    bool m_rememberFiles = false;
    bool m_saveDataLocally = false;
    bool m_detectMovedFiles = false;

    QMap<QString, QTimer *> m_notificationList;
    QSet<hash64_t> m_usedDevices;

    QString m_versionFolder;
    QString m_versionPattern;
};

#endif // SYNCMANAGER_H
