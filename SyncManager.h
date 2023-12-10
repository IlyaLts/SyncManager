/*
===============================================================================
    Copyright (C) 2022-2023 Ilya Lyakhovets

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
#include <QHash>
#include <QQueue>
#include <QTimer>
#include <QDateTime>

#define DATA_FILENAME           "Data.dat"
#define SYNC_MIN_DELAY          1000
#define NOTIFICATION_DELAY      300000

// In a couple times slower than QDirIterator
//#define USE_STD_FILESYSTEM

using hash64_t = quint64;

struct File
{
    enum Type : qint8
    {
        unknown,
        file,
        folder
    };

    File(){}
    File(QByteArray path, Type type, QDateTime time, bool updated = false, bool exists = true, bool onRestore = false) : path(path),
                                                                                                                         date(time),
                                                                                                                         type(type),
                                                                                                                         updated(updated),
                                                                                                                         exists(exists),
                                                                                                                         onRestore(onRestore){}

    QByteArray path;
    QDateTime date;
    qint64 size = 0;
    Type type = unknown;
    bool updated = false;
    bool exists = false;
    bool onRestore = false;
    bool newlyAdded = false;
    bool moved = false;
    bool movedSource = false;
};

struct SyncFolder
{
    explicit SyncFolder(bool paused) : paused(paused){}

    QByteArray path;
    QHash<hash64_t, File> files;
    QHash<hash64_t, QPair<QByteArray, QByteArray>> foldersToRename;
    QHash<hash64_t, QPair<QByteArray, QByteArray>> filesToMove;
    QHash<hash64_t, QByteArray> foldersToAdd;
    QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>> filesToAdd;
    QHash<hash64_t, QByteArray> foldersToRemove;
    QHash<hash64_t, QByteArray> filesToRemove;
    QHash<hash64_t, qint64> sizeList;

    QDateTime lastSyncDate;
    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
};

struct SyncProfile
{
    explicit SyncProfile(bool paused) : paused(paused){}

    QList<SyncFolder> folders;
    QList<QByteArray> excludeList;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
    quint64 syncTime = 0;
    QDateTime lastSyncDate;
    QString name;
};

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

    void saveData() const;
    void restoreData();

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
    inline void enableDetectMovedFiles(bool enable) { m_detectMovedFiles = enable; }

    inline int filesToSync() const { return m_filesToSync; }
    inline int syncTimeMultiplier() const { return m_syncTimeMultiplier; }
    inline int syncEvery() const { return m_syncEvery; }
    inline int existingProfiles() const { return m_existingProfiles; }
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
    inline bool detectMovedFilesEnabled() const { return m_detectMovedFiles; }
    inline const QString &versionFolder() const { return m_versionFolder; }
    inline const QString &versionPattern() const { return m_versionPattern; }

Q_SIGNALS:

    void warning(const QString &title, const QString &message);
    void profileSynced(SyncProfile *profile);

private:

    int getListOfFiles(SyncFolder &folder, const QList<QByteArray> &excludeList);
    void checkForChanges(SyncProfile &profile);
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
    bool m_detectMovedFiles = false;

    QMap<QString, QTimer *> m_notificationList;
    QSet<hash64_t> m_usedDevices;

    QString m_versionFolder;
    QString m_versionPattern;
};

#endif // SYNCMANAGER_H
