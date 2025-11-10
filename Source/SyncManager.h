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
#define TEMP_EXTENSION          "sm_temp"
#define DATABASE_VERSION        4
#define SYNC_MIN_DELAY          1000
#define NOTIFICATION_COOLDOWN   300000
#define CPU_UPDATE_TIME         50

class CpuUsage;

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
    void purgeRemovedProfiles();
    void throttleCpu();

    inline const QQueue<SyncProfile *> &queue() const { return m_queue; }
    inline const std::list<SyncProfile> &profiles() const { return m_profiles; }
    inline std::list<SyncProfile> &profiles() { return m_profiles; }

    inline void shouldQuit() { m_shouldQuit = true; }
    void setSyncTimeMultiplier(SyncProfile &profile, int multiplier);
    inline void setMaxDiskTransferRate(quint64 rate) { m_maxDiskTransferRate = rate; }
    inline void setMaxCpuUsage(float percentage) { m_maxCpuUsage = percentage; }
    inline void setPaused(bool paused) { m_paused = paused; }
    inline void enableNotifications(bool enable) { m_notifications = enable; }    

    inline quint64 maxDiskTransferRate() const { return m_maxDiskTransferRate; }
    inline float maxCpuUsage() const { return m_maxCpuUsage; }
    inline int filesToSync() const { return m_filesToSync; }
    inline int existingProfiles() const { return m_existingProfiles; }
    inline bool quitting() const { return m_shouldQuit; }
    inline bool issue() const { return m_issue; }
    inline bool warning() const { return m_warning; }
    inline bool busy() const { return m_busy; }
    inline bool paused() const { return m_paused; }
    inline bool syncing() const { return m_syncing; }
    bool hasManualSyncProfile() const;
    bool inPausedState() const;
    inline bool notificationsEnabled() const { return m_notifications; }

    static quint64 maxInterval();

public Q_SLOTS:

    void updateCpuUsage(float appPercentage, float systemPercentage);


Q_SIGNALS:

    void message(const QString &title, const QString &message);
    void profileSynced(SyncProfile *profile);

private:

    bool syncProfile(SyncProfile &profile);
    int scanFiles(SyncFolder &folder);
    void synchronizeFileAttributes(SyncProfile &profile);
    void checkForRenamedFolders(SyncProfile &profile);
    void checkForMovedFiles(SyncProfile &profile);
    void checkForAddedFiles(SyncProfile &profile);
    void checkForRemovedFiles(SyncProfile &profile);
    void checkForChanges(SyncProfile &profile);
    bool removeFile(SyncFolder &folder, const QString &path, const QString &fullPath, SyncFile::Type type);
    bool copyFile(quint64 &deviceRead, const QString &fileName, const QString &newName);
    void renameFolders(SyncFolder &folder);
    void moveFiles(SyncFolder &folder);
    void createParentFolders(SyncFolder &folder, QByteArray path);
    void removeFolders(SyncFolder &folder);
    void removeFiles(SyncFolder &folder);
    void createFolders(SyncFolder &folder);
    void copyFiles(SyncFolder &folder);
    void removeUniqueFiles(SyncFolder &folder);
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

    quint64 m_maxDiskTransferRate = 0;
    float m_maxCpuUsage = 100.0;
    float m_processUsage = 0.0;
    float m_systemUsage = 0.0;
    QTimer m_diskUsageResetTimer;
    QMap<QString, QTimer *> m_notificationList;
    QSet<hash64_t> m_usedDevices;
    QMap<hash64_t, quint64> m_deviceRead;
    QMutex m_usedDevicesMutex;
    CpuUsage *m_cpuUsage;
};

#endif // SYNCMANAGER_H
