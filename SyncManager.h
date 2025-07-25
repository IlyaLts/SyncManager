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

#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include "SyncFile.h"
#include "SyncFolder.h"
#include "SyncProfile.h"
#include <QList>
#include <QSet>
#include <QMap>
#include <QQueue>
#include <QTimer>
#include <QDateTime>

#define DATA_FOLDER_PATH        ".SyncManager"
#define DATABASE_FILENAME       "db"
#define DATABASE_VERSION        3
#define SYNC_MIN_DELAY          1000
#define NOTIFICATION_COOLDOWN   300000

/*
===========================================================

    SyncManager

===========================================================
*/
class SyncManager : public QObject
{
    Q_OBJECT

public:

    SyncManager();

    void addToQueue(SyncProfile *profile);
    void sync();

    void updateTimer(SyncProfile &profile);
    void updateStatus();
    void updateNextSyncingTime(SyncProfile &profile);
    void removeAllDatabases();

    inline const QQueue<SyncProfile *> &queue() const { return m_queue; }
    inline const std::list<SyncProfile> &profiles() const { return m_profiles; }
    inline std::list<SyncProfile> &profiles() { return m_profiles; }

    inline void shouldQuit() { m_shouldQuit = true; }
    void setSyncTimeMultiplier(SyncProfile &profile, int multiplier);
    inline void setPaused(bool paused) { m_paused = paused; }
    inline void enableNotifications(bool enable) { m_notifications = enable; }

    inline int filesToSync() const { return m_filesToSync; }
    inline int existingProfiles() const { return m_existingProfiles; }
    inline bool isQuitting() const { return m_shouldQuit; }
    inline bool isThereIssue() const { return m_issue; }
    inline bool isThereWarning() const { return m_warning; }
    inline bool isBusy() const { return m_busy; }
    inline bool isPaused() const { return m_paused; }
    inline bool isSyncing() const { return m_syncing; }
    bool isThereProfileWithHiddenSync() const;
    bool isInAutomaticPausedState() const;
    inline bool notificationsEnabled() const { return m_notifications; }

Q_SIGNALS:

    void warning(const QString &title, const QString &message);
    void profileSynced(SyncProfile *profile);

private:

    bool syncProfile(SyncProfile &profile);
    int scanFiles(SyncProfile &profile, SyncFolder &folder);
    void synchronizeFileAttributes(SyncProfile &profile);
    void checkForRenamedFolders(SyncProfile &profile);
    void checkForMovedFiles(SyncProfile &profile);
    void checkForAddedFiles(SyncProfile &profile);
    void checkForRemovedFiles(SyncProfile &profile);
    void checkForChanges(SyncProfile &profile);
    bool removeFile(SyncProfile &profile, SyncFolder &folder, const QString &path, const QString &fullPath, SyncFile::Type type);
    void renameFolders(SyncProfile &profile, SyncFolder &folder);
    void moveFiles(SyncProfile &profile, SyncFolder &folder);
    void createParentFolders(SyncProfile &profile, SyncFolder &folder, QByteArray path);
    void removeFolders(SyncProfile &profile, SyncFolder &folder);
    void removeFiles(SyncProfile &profile, SyncFolder &folder);
    void createFolders(SyncProfile &profile, SyncFolder &folder);
    void copyFiles(SyncProfile &profile, SyncFolder &folder);
    void removeUniqueFiles(SyncProfile &profile, SyncFolder &folder);
    void syncFiles(SyncProfile &profile);

    QQueue<SyncProfile *> m_queue;
    std::list<SyncProfile> m_profiles;

    int m_filesToSync = 0;
    int m_existingProfiles = 0;
    bool m_databaseChanged = false;
    bool m_shouldQuit = false;
    bool m_issue = true;
    bool m_warning = false;
    bool m_busy = false;
    bool m_paused = false;
    bool m_syncing = false;
    bool m_notifications = true;

    QMap<QString, QTimer *> m_notificationList;
    QSet<hash64_t> m_usedDevices;
    QMutex usedDevicesMutex;
};

#endif // SYNCMANAGER_H
