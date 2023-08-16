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
#define SYNCMANAGER_VERSION     "1.5"
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

    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
};

struct SyncProfile
{
    explicit SyncProfile(bool paused) : paused(paused){}

    QList<SyncFolder> folders;

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
    } syncingMode;

    enum DeletionMode
    {
        MoveToTrash,
        Versioning,
        DeletePermanently
    } deletionMode;

    void addToQueue(int profileNumber = -1);
    void sync();
    int getListOfFiles(SyncFolder &folder);
    void checkForChanges(SyncProfile &profile);

    void updateTimer();
    void updateStatus();
    void updateNextSyncingTime();
    void saveData() const;
    void restoreData();

Q_SIGNALS:

    void warning(QString title, QString message);
    void profileSynced(SyncProfile *profile);

public:

    QQueue<int> queue;
    QList<SyncProfile> profiles;
    QMap<QString, QTimer *> notificationList;
    QSet<hash64_t> usedDevices;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    bool caseSensitiveSystem = false;
#else
    bool caseSensitiveSystem = true;
#endif

    bool isThereIssue = true;
    bool isThereWarning = false;
    bool shouldQuit = false;
    bool busy = false;
    bool paused = false;
    bool syncing = false;
    bool syncHidden = false;
    bool notifications = true;
    bool rememberFiles = false;
    bool detectMovedFiles = false;

    int numOfFilesToSync = 0;
    int syncTimeMultiplier = 1;
    int syncEvery = 0;
    int existingProfiles = 0;

    QString versionFolder;
    QString versionPattern;

    QTimer syncTimer;
};

#endif // SYNCMANAGER_H
