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

#include "SyncManager.h"
#include "Application.h"
#include "MainWindow.h"
#include "Common.h"
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMenuBar>
#include <QDirIterator>
#include <QTimer>
#include <QStack>
#include <QtConcurrent/QtConcurrent>

/*
===================
SyncManager::SyncManager
===================
*/
SyncManager::SyncManager()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    m_paused = settings.value(QLatin1String("Paused"), false).toBool();
    m_notifications = QSystemTrayIcon::supportsMessages() && settings.value("Notifications", true).toBool();
    m_ignoreHiddenFiles = settings.value("IgnoreHiddenFiles", true).toBool();
    m_databaseLocation = static_cast<SyncManager::DatabaseLocation>(settings.value("DatabaseLocation", SyncManager::Decentralized).toInt());
    m_detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    m_syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (m_syncTimeMultiplier <= 0) m_syncTimeMultiplier = 1;
    m_movedFileMinSize = settings.value("MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt();

    m_caseSensitiveSystem = settings.value("caseSensitiveSystem", m_caseSensitiveSystem).toBool();
    m_versionFolder = settings.value("VersionFolder", "[Deletions]").toString();
    m_versionPattern = settings.value("VersionPattern", "yyyy_M_d_h_m_s_z").toString();
}

/*
===================
SyncManager::~SyncManager
===================
*/
SyncManager::~SyncManager()
{
}

/*
===================
SyncManager::addToQueue
===================
*/
void SyncManager::addToQueue(SyncProfile *profile)
{
    if (m_profiles.isEmpty() || (profile && m_queue.contains(profile)))
        return;

    // Adds the passed profile number to the sync queue
    if (profile)
    {
        if ((!profile->paused || m_syncingMode != Automatic) && !profile->toBeRemoved)
        {
            m_queue.enqueue(profile);
        }
    }
    // If a profile number is not passed, adds all remaining profiles to the sync queue
    else
    {
        for (auto &profile : profiles())
            if ((!profile.paused || m_syncingMode != Automatic) && !profile.toBeRemoved && !m_queue.contains(&profile))
                m_queue.enqueue(&profile);
    }
}

/*
===================
SyncManager::sync
===================
*/
void SyncManager::sync()
{
    if (m_busy || m_queue.isEmpty())
        return;

    m_busy = true;
    m_syncing = false;

    while (!m_queue.empty())
    {
        if (!syncProfile(*m_queue.head()))
            return;

        m_queue.head()->syncHidden = false;
        m_queue.dequeue();
    }

    // Removes profiles/folders completely if we remove them during syncing
    for (auto profileIt = m_profiles.begin(); profileIt != m_profiles.end();)
    {
        // Profiles
        if (profileIt->toBeRemoved)
        {
            profileIt = m_profiles.erase(static_cast<QList<SyncProfile>::const_iterator>(profileIt));
            continue;
        }

        // Folders
        for (auto folderIt = profileIt->folders.begin(); folderIt != profileIt->folders.end();)
        {
            if (folderIt->toBeRemoved)
                folderIt = profileIt->folders.erase(static_cast<QList<SyncFolder>::const_iterator>(folderIt));
            else
                folderIt++;
        }

        profileIt++;
    }

    m_busy = false;
}

/*
===================
SyncManager::updateStatus
===================
*/
void SyncManager::updateStatus()
{
    m_existingProfiles = 0;
    m_issue = true;
    m_warning = false;
    m_syncing = false;

    // Syncing status
    for (auto &profile : m_profiles)
    {
        profile.syncing = false;
        int existingFolders = 0;

        if (profile.toBeRemoved)
            continue;

        m_existingProfiles++;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((!m_queue.isEmpty() && m_queue.head() != &profile ) || folder.toBeRemoved)
                continue;

            if (folder.exists)
            {
                existingFolders++;

                if (existingFolders >= 2)
                    m_issue = false;
            }
            else
            {
                m_warning = true;
            }

            if (m_busy && folder.isActive())
            {
                if (!folder.foldersToRename.isEmpty() ||
                    !folder.filesToMove.isEmpty() ||
                    !folder.foldersToCreate.isEmpty() ||
                    !folder.filesToCopy.isEmpty() ||
                    !folder.foldersToRemove.isEmpty() ||
                    !folder.filesToRemove.isEmpty())
                {
                    m_syncing = true;
                    profile.syncing = true;
                    folder.syncing = true;
                }
            }
        }
    }

    // Number of files left to sync
    qsizetype size = 0;

    if (m_busy)
    {
        for (auto &folder : m_queue.head()->folders)
        {
            if (folder.isActive())
            {
                size += folder.foldersToRename.size();
                size += folder.filesToMove.size();
                size += folder.foldersToCreate.size();
                size += folder.filesToCopy.size();
                size += folder.foldersToRemove.size();
                size += folder.filesToRemove.size();
            }
        }

    }

    m_filesToSync = size;
}

/*
===================
SyncManager::updateTimer
===================
*/
void SyncManager::updateTimer(SyncProfile &profile)
{
    using namespace std;
    using namespace std::chrono;

    if (m_syncingMode != SyncManager::Automatic)
        return;

    QDateTime dateToSync(profile.lastSyncDate);
    dateToSync = dateToSync.addMSecs(profile.syncEvery);
    qint64 syncTime = 0;

    if (dateToSync >= QDateTime::currentDateTime())
        syncTime = QDateTime::currentDateTime().msecsTo(dateToSync);

    if (!profile.isActive())
        if (syncTime < SYNC_MIN_DELAY)
            syncTime = SYNC_MIN_DELAY;

    bool profileActive = profile.syncTimer.isActive();

    if ((!m_busy && profileActive) || (!profileActive || (duration<qint64, milli>(syncTime) < profile.syncTimer.remainingTime())))
    {
        quint64 interval = syncTime;
        quint64 max = numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

        // If exceeds the maximum value of an qint64
        if (interval > max)
            interval = max;

        profile.syncTimer.setInterval(duration_cast<duration<qint64, nano>>(duration<quint64, milli>(interval)));
        profile.syncTimer.start();
    }
}

/*
===================
SyncManager::updateNextSyncingTime
===================
*/
void SyncManager::updateNextSyncingTime()
{
    for (auto &profile : profiles())
    {
        quint64 time = profile.syncTime;

        // Multiplies sync time by 2
        for (int i = 0; i < m_syncTimeMultiplier - 1; i++)
        {
            time <<= 1;
            quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

            // If exceeds the maximum value of an qint64
            if (time > max)
            {
                time = max;
                break;
            }
        }

        if (time < SYNC_MIN_DELAY)
            time = SYNC_MIN_DELAY;

        profile.syncEvery = time;
    }
}

/*
===================
SyncManager::saveDatabaseLocally
===================
*/
void SyncManager::saveDatabaseLocally(const SyncProfile &profile) const
{
    for (auto &folder : profile.folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path)) + ".db");
        saveToDatabase(folder, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncManager::saveDatabaseDecentralised
===================
*/
void SyncManager::saveDatabaseDecentralised(const SyncProfile &profile) const
{
    for (auto &folder : profile.folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QDir().mkdir(folder.path + DATA_FOLDER_PATH);

        if (!QDir(folder.path + DATA_FOLDER_PATH).exists())
            continue;

        saveToDatabase(folder, folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);

#ifdef Q_OS_WIN
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH), true);
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
#endif
    }
}

/*
===================
SyncManager::loadDatabaseLocally
===================
*/
void SyncManager::loadDatabaseLocally(SyncProfile &profile)
{
    for (auto &folder : profile.folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path)) + ".db");
        loadFromDatabase(folder, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncManager::loadDatebaseDecentralised
===================
*/
void SyncManager::loadDatebaseDecentralised(SyncProfile &profile)
{
    for (auto &folder : profile.folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        loadFromDatabase(folder, folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
    }
}

/*
===================
SyncManager::removeDatabase
===================
*/
void SyncManager::removeDatabase(const SyncFolder &folder)
{
    if (databaseLocation() == Decentralized)
    {
        QByteArray path = QByteArray::number(hash64(folder.path));
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + path + ".db");
    }
    else
    {
        QDir(folder.path + DATA_FOLDER_PATH).removeRecursively();
    }
}

/*
===================
SyncManager::removeAllDatabases
===================
*/
void SyncManager::removeAllDatabases()
{
    QDirIterator it(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/", {"*.db"}, QDir::Files);

    while (it.hasNext())
    {
        it.next();
        QFile::remove(it.filePath());
    }

    for (auto &profile : m_profiles)
        for (auto &folder : profile.folders)
            QDir(folder.path + DATA_FOLDER_PATH).removeRecursively();
}

/*
===================
SyncManager::setSyncTimeMultiplier
===================
*/
void SyncManager::setSyncTimeMultiplier(int multiplier)
{
    m_syncTimeMultiplier = multiplier;
    updateNextSyncingTime();
}

/*
===================
SyncManager::isThereProfileWithHiddenSync
===================
*/
bool SyncManager::isThereProfileWithHiddenSync() const
{
    for (auto &profile : profiles())
        if (profile.syncHidden)
            return true;

    return false;
}

/*
===================
SyncManager::saveToDatabase
===================
*/
void SyncManager::saveToDatabase(const SyncFolder &folder, const QString &path) const
{
    SET_TIME(startTime);

    QFile data(path);
    if (!data.open(QIODevice::WriteOnly))
        return;

    QDataStream stream(&data);
    short version = DATABASE_VERSION;
    qsizetype size = folder.files.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&version), sizeof(version)) != sizeof(version))
        return;

    // File data
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto fileIt = folder.files.begin(); fileIt != folder.files.end(); fileIt++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(SyncFile::Type) +
                               sizeof(SyncFile::LockedFlag) + sizeof(qint8) + sizeof(Attributes);

        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<hash64_t *>(p) = fileIt.key().data;
        p += sizeof(hash64_t);
        memcpy(p, &fileIt->modifiedDate, sizeof(QDateTime));
        p += sizeof(QDateTime);
        *reinterpret_cast<qint64 *>(p) = fileIt->size;
        p += sizeof(qint64);
        *reinterpret_cast<SyncFile::Type *>(p) = fileIt->type;
        p += sizeof(SyncFile::Type);
        *reinterpret_cast<SyncFile::LockedFlag *>(p) = fileIt->lockedFlag;
        p += sizeof(SyncFile::LockedFlag);
        *reinterpret_cast<qint8 *>(p) = fileIt->flags;
        p += sizeof(qint8);
        *reinterpret_cast<Attributes *>(p) = fileIt->attributes;

        if (stream.writeRawData(&buf[0], bufSize) != bufSize)
            return;
    }

    // Folders to rename
    size = folder.foldersToRename.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : folder.foldersToRename)
    {
        stream << fileIt.toPath;
        stream << fileIt.fromFullPath;
        stream << fileIt.attributes;
    }

    // Files to move
    size = folder.filesToMove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : folder.filesToMove)
    {
        stream << fileIt.toPath;
        stream << fileIt.fromFullPath;
        stream << fileIt.attributes;
    }

    // Folders to create
    size = folder.foldersToCreate.size();
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : folder.foldersToCreate)
    {
        stream << fileIt.path;
        stream << fileIt.attributes;
    }

    // Files to copy
    size = folder.filesToCopy.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : folder.filesToCopy)
    {
        stream << fileIt.toPath;
        stream << fileIt.fromFullPath;
        stream << fileIt.modifiedDate;
    }

    // Folders to remove
    size = folder.foldersToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : folder.foldersToRemove)
        stream << path;

    // Files to remove
    size = folder.filesToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : folder.filesToRemove)
        stream << path;

    TIMESTAMP(startTime, "Saved %s to database", qUtf8Printable(folder.path));
}

/*
===================
SyncManager::loadFromDatabase
===================
*/
void SyncManager::loadFromDatabase(SyncFolder &folder, const QString &path)
{
    SET_TIME(startTime);

    QFile data(path);
    if (!data.open(QIODevice::ReadOnly))
        return;

    QDataStream stream(&data);
    short version;
    qsizetype numOfFiles;

    if (stream.readRawData(reinterpret_cast<char *>(&version), sizeof(version)) != sizeof(version))
        return;

    if (version != DATABASE_VERSION)
        return;

    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.files.reserve(numOfFiles);

    // File data
    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(SyncFile::Type) +
                               sizeof(SyncFile::LockedFlag) + sizeof(qint8) + sizeof(Attributes);

        char buf[bufSize];

        if (stream.readRawData(&buf[0], bufSize) != bufSize)
            return;

        char *p = buf;
        hash64_t hash;
        QDateTime modifiedDate;
        qint64 size;
        SyncFile::Type type;
        SyncFile::LockedFlag lockedFlag;
        qint8 flags;
        Attributes attributes;

        hash = *reinterpret_cast<hash64_t *>(p);
        p += sizeof(hash64_t);
        modifiedDate = *reinterpret_cast<QDateTime *>(p);
        p += sizeof(QDateTime);
        size = *reinterpret_cast<qint64 *>(p);
        p += sizeof(qint64);
        type = *reinterpret_cast<SyncFile::Type *>(p);
        p += sizeof(SyncFile::Type);
        lockedFlag = *reinterpret_cast<SyncFile::LockedFlag *>(p);
        p += sizeof(SyncFile::LockedFlag);
        flags = *reinterpret_cast<qint8 *>(p);
        p += sizeof(qint8);
        attributes = *reinterpret_cast<Attributes *>(p);

        const auto it = folder.files.insert(hash, SyncFile(type, modifiedDate));
        it->size = size;
        it->lockedFlag = lockedFlag;
        it->flags = flags;
        it->attributes = attributes;
    }

    // Folders to rename
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.foldersToRename.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromFullPath;
        Attributes attributes;

        stream >> toPath;
        stream >> fromFullPath;
        stream >> attributes;

        const auto it = folder.foldersToRename.insert(hash64(toPath), {toPath, fromFullPath, attributes});
        it->toPath.squeeze();
        it->fromFullPath.squeeze();
    }

    // Files to move
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.filesToMove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromFullPath;
        Attributes attributes;

        stream >> toPath;
        stream >> fromFullPath;
        stream >> attributes;

        const auto it = folder.filesToMove.insert(hash64(toPath), {toPath, fromFullPath, attributes});
        it->toPath.squeeze();
        it->fromFullPath.squeeze();
    }

    // Folders to create
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.foldersToCreate.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        Attributes attributes;

        stream >> path;
        stream >> attributes;

        const auto it = folder.foldersToCreate.insert(hash64(path), {path, attributes});
        it->path.squeeze();
    }

    // Files to copy
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.filesToCopy.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromFullPath;
        QDateTime modifiedDate;

        stream >> toPath;
        stream >> fromFullPath;
        stream >> modifiedDate;

        const auto it = folder.filesToCopy.insert(hash64(toPath), {toPath, fromFullPath, modifiedDate});
        it->toPath.squeeze();
        it->fromFullPath.squeeze();
    }

    // Folders to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.foldersToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        const auto it = folder.foldersToRemove.insert(hash64(path), path);
        it->squeeze();
    }

    // Files to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    folder.filesToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        const auto it = folder.filesToRemove.insert(hash64(path), path);
        it->squeeze();
    }

    folder.optimizeMemoryUsage();

    TIMESTAMP(startTime, "Loaded %s from database", qUtf8Printable(folder.path));
}

/*
===================
SyncManager::syncProfile
===================
*/
bool SyncManager::syncProfile(SyncProfile &profile)
{
#ifdef DEBUG
    std::chrono::high_resolution_clock::time_point syncTime;
    debugSetTime(syncTime);
#endif

    if (m_paused)
        return true;

    QElapsedTimer timer;
    timer.start();

    for (auto &folder : profile.folders)
        folder.exists = QFileInfo::exists(folder.path);

    if (!profile.isActive())
    {
        emit profileSynced(&profile);
        return true;
    }

#ifdef DEBUG
    qDebug("=======================================");
    qDebug("Started syncing %s", qUtf8Printable(profile.name));
    qDebug("=======================================");
#endif

    if (m_databaseLocation == Decentralized)
        loadDatebaseDecentralised(profile);
    else
        loadDatabaseLocally(profile);

    // Gets lists of all files in folders
    SET_TIME(startTime);

    int result = 0;
    QList<QPair<Hash, QFuture<int>>> futureList;

    for (auto &folder : profile.folders)
    {
        hash64_t requiredDevice = hash64(QStorageInfo(folder.path).device());
        QPair<Hash, QFuture<int>> pair(requiredDevice, QFuture(QtConcurrent::run([&](){ return getListOfFiles(profile, folder); })));
        pair.second.suspend();
        futureList.append(pair);
    }

    while (!futureList.isEmpty())
    {
        for (auto futureIt = futureList.begin(); futureIt != futureList.end();)
        {
            usedDevicesMutex.lock();

            if (!m_usedDevices.contains(futureIt->first.data))
            {
                m_usedDevices.insert(futureIt->first.data);
                futureIt->second.resume();
            }

            if (futureIt->second.isFinished())
            {
                result += futureIt->second.result();
                m_usedDevices.remove(futureIt->first.data);
                futureIt = futureList.erase(static_cast<QList<QPair<Hash, QFuture<int>>>::const_iterator>(futureIt));
            }
            else
            {
                futureIt++;
            }

            usedDevicesMutex.unlock();
        }

        if (m_shouldQuit)
            return false;
    }

    TIMESTAMP(startTime, "Found %d files in %s.", result, qUtf8Printable(profile.name));

    checkForChanges(profile);

    bool countAverage = profile.syncTime ? true : false;
    profile.syncTime += timer.elapsed();

    if (countAverage)
        profile.syncTime /= 2;

    int numOfFoldersToRename = 0;
    int numOfFilesToMove = 0;
    int numOfFoldersToCreate = 0;
    int numOffilesToCopy = 0;
    int numOfFoldersToRemove = 0;
    int numOfFilesToRemove = 0;

    for (auto &folder : profile.folders)
    {
        numOfFoldersToRename += folder.foldersToRename.size();
        numOfFilesToMove += folder.filesToMove.size();
        numOfFoldersToCreate += folder.foldersToCreate.size();
        numOffilesToCopy += folder.filesToCopy.size();
        numOfFoldersToRemove += folder.foldersToRemove.size();
        numOfFilesToRemove += folder.filesToRemove.size();
    }

    if (numOfFoldersToRename || numOfFilesToMove || numOfFoldersToCreate || numOffilesToCopy || numOfFoldersToRemove || numOfFilesToRemove)
    {
        m_databaseChanged = true;

#ifdef DEBUG
        qDebug("---------------------------------------");
        if (numOfFoldersToRename)   qDebug("Folders to rename: %d", numOfFoldersToRename);
        if (numOfFilesToMove)       qDebug("Files to move: %d", numOfFilesToMove);
        if (numOfFoldersToCreate)   qDebug("Folders to create: %d", numOfFoldersToCreate);
        if (numOffilesToCopy)       qDebug("Files to copy: %d", numOffilesToCopy);
        if (numOfFoldersToRemove)   qDebug("Folders to remove: %d", numOfFoldersToRemove);
        if (numOfFilesToRemove)     qDebug("Files to remove: %d", numOfFilesToRemove);
        qDebug("---------------------------------------");
#endif
    }

    updateStatus();

    if (m_shouldQuit)
        return false;

    syncFiles(profile);

    for (auto &folder : profile.folders)
        folder.removeInvalidFileData(profile);

    if (profile.resetLocks())
        m_databaseChanged = true;

    if (m_databaseChanged)
    {
        if (m_databaseLocation == Decentralized)
            saveDatabaseDecentralised(profile);
        else
            saveDatabaseLocally(profile);
    }

    for (auto &folder : profile.folders)
        folder.clearData();

    m_databaseChanged = false;

    for (auto &folder : profile.folders)
        folder.optimizeMemoryUsage();

    profile.clearFilePaths();

    // Last sync date update
    profile.lastSyncDate = QDateTime::currentDateTime();

    for (auto &folder : profile.folders)
        if (folder.isActive())
            folder.lastSyncDate = QDateTime::currentDateTime();

    updateStatus();
    updateNextSyncingTime();
    emit profileSynced(&profile);

    TIMESTAMP(syncTime, "Syncing is complete.");
    return true;
}

/*
===================
SyncManager::getListOfFiles
===================
*/
int SyncManager::getListOfFiles(SyncProfile &profile, SyncFolder &folder)
{
    int totalNumOfFiles = 0;
    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    for (auto &file : folder.files)
        file.flags = 0;

    while (dir.hasNext())
    {
        if (m_shouldQuit || !folder.isActive())
            return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());

        // Skips hidden files
        if (ignoreHiddenFilesEnabled() && fileInfo.isHidden())
            continue;

        // Skips database files
        if (databaseLocation() == Decentralized)
        {
            if (fileInfo.isHidden())
            {
                if (fileInfo.fileName().compare(DATA_FOLDER_PATH, Qt::CaseInsensitive) == 0)
                    continue;

                if (fileInfo.fileName().compare(DATABASE_FILENAME, Qt::CaseInsensitive) == 0)
                    continue;
            }
        }

        QByteArray absoluteFilePath = fileInfo.filePath().toUtf8();
        QByteArray filePath(absoluteFilePath);
        filePath.remove(0, folder.path.size());

        bool shouldExclude = false;

        // Excludes unwanted files and folder from scanning
        for (auto &exclude : profile.excludeList)
        {
            if (m_caseSensitiveSystem ? qstrncmp(exclude, filePath, exclude.length()) == 0 : qstrnicmp(exclude, filePath, exclude.length()) == 0)
            {
                shouldExclude = true;
                break;
            }
        }

        if (shouldExclude)
            continue;

        SyncFile::Type type = fileInfo.isDir() ? SyncFile::Folder : SyncFile::File;
        hash64_t fileHash = hash64(filePath);

        profile.addFilePath(fileHash, filePath);

        // If a file is already in our database
        if (folder.files.contains(fileHash))
        {
            SyncFile &file = folder.files[fileHash];
            QDateTime modifiedDate(fileInfo.lastModified());

            // Quits if a hash collision is detected
            if (file.processed())
            {
#ifndef DEBUG
                QMessageBox::critical(nullptr, QString("Hash collision detected!"), QString("%s vs %s").arg(qUtf8Printable(filePath), qUtf8Printable(profile.filePath(fileHash))));
#else
                qCritical("Hash collision detected: %s vs %s", qUtf8Printable(filePath), qUtf8Printable(profile.filePath(fileHash)));
#endif

                m_shouldQuit = true;
                qApp->quit();
                return -1;
            }

            if (file.modifiedDate != modifiedDate)
            {
                m_databaseChanged = true;
                file.setUpdated(true);
            }

            if (file.size != fileInfo.size())
                m_databaseChanged = true;

            if (file.type != type)
                m_databaseChanged = true;

            if (file.attributes != getFileAttributes(absoluteFilePath))
            {
                m_databaseChanged = true;
                file.setAttributesUpdated(true);
            }

            // Marks all parent folders as updated if the current folder was updated
            if (file.updated())
            {
                QByteArray folderPath(fileInfo.filePath().toUtf8());

                while (folderPath.remove(folderPath.lastIndexOf("/"), folderPath.length()).length() > folder.path.length())
                {
                    hash64_t hash = hash64(QByteArray(folderPath).remove(0, folder.path.size()));

                    if (folder.files.value(hash).updated())
                        break;

                    folder.files[hash].setUpdated(true);
                }
            }

            file.modifiedDate = modifiedDate;
            file.size = fileInfo.size();
            file.type = type;
            file.attributes = getFileAttributes(absoluteFilePath);
            file.setExists(true);
            file.setProcessed(true);
        }
        else
        {
            SyncFile *file = folder.files.insert(fileHash, SyncFile(type, fileInfo.lastModified())).operator->();
            file->size = fileInfo.size();
            file->attributes = getFileAttributes(absoluteFilePath);
            file->setNewlyAdded(true);
            file->setProcessed(true);

            m_databaseChanged = true;
        }

        totalNumOfFiles++;
    }

    folder.optimizeMemoryUsage();
    usedDevicesMutex.lock();
    m_usedDevices.remove(hash64(QStorageInfo(folder.path).device()));
    usedDevicesMutex.unlock();
    return totalNumOfFiles;
}

/*
===================
SyncManager::synchronizeFileAttributes
===================
*/
void SyncManager::synchronizeFileAttributes(SyncProfile &profile)
{
    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists)
            continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists)
                continue;

            if (!folderIt->isActive())
                break;

            for (QHash<Hash, SyncFile>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (!otherFolderIt->isActive())
                    break;

                if (!otherFileIt.value().exists())
                    continue;

                const SyncFile &file = folderIt->files.value(otherFileIt.key());
                const SyncFile &otherFile = otherFileIt.value();

                if (file.lockedFlag != SyncFile::Unlocked || otherFile.lockedFlag != SyncFile::Unlocked)
                    continue;

                if (file.toBeRemoved() || otherFile.toBeRemoved())
                    continue;

                if (!file.exists() || !otherFile.exists())
                    continue;

                if (file.hasOlderAttributes(otherFile))
                {
                    QByteArray from(otherFolderIt->path);
                    from.append(profile.filePath(otherFileIt.key()));

                    QByteArray to(folderIt->path);
                    to.append(profile.filePath(otherFileIt.key()));

                    if (setFileAttribute(to, getFileAttributes(from)))
                    {
                        SyncFile &folder = folderIt->files[otherFileIt.key()];
                        folder.attributes = otherFile.attributes;
                        m_databaseChanged = true;
                    }
                }
            }
        }
    }

    TIMESTAMP(startTime, "Synchronized file attributes.");
}

/*
===================
SyncManager::checkForRenamedFolders

Detects only changes in the case of folder names
===================
*/
void SyncManager::checkForRenamedFolders(SyncProfile &profile)
{
    if (m_caseSensitiveSystem)
        return;

    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->isActive())
            continue;

        for (QHash<Hash, SyncFile>::iterator renamedFolderIt = folderIt->files.begin(); renamedFolderIt != folderIt->files.end(); ++renamedFolderIt)
        {
            // Only a newly added folder can indicate that the case of folder name was changed
            if (renamedFolderIt->type != SyncFile::Folder || !renamedFolderIt->newlyAdded() || !renamedFolderIt->exists())
                continue;

            // Skips if the folder is already scheduled to be moved, especially when there are three or more sync folders
            if (renamedFolderIt->lockedFlag == SyncFile::Locked)
                continue;

            bool abort = false;

            // Aborts if the folder doesn't exist in any other sync folder
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                QString otherFolderPath(otherFolderIt->path);
                otherFolderPath.append(profile.filePath(renamedFolderIt.key()));
                QFileInfo otherFolderInfo(otherFolderPath);

                if (!otherFolderInfo.exists())
                {
                    abort = true;
                    break;
                }
            }

            if (abort)
                continue;

            // Adds folders from other sync folders for renaming
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                QByteArray otherFolderPath(otherFolderIt->path);
                otherFolderPath.append(profile.filePath(renamedFolderIt.key()));
                QFileInfo otherFileInfo(otherFolderPath);
                QByteArray otherCurrentFolderName;
                QByteArray otherCurrentFolderPath;

                QByteArray folderName(profile.filePath(renamedFolderIt.key()));
                folderName.remove(0, folderName.lastIndexOf("/") + 1);

                QFileInfo otherFolder = getCurrentFileInfo(otherFileInfo.absolutePath(), otherFileInfo.fileName(), QDir::Dirs);

                if (otherFolder.exists())
                {
                    otherCurrentFolderName = otherFolder.fileName().toUtf8();
                    otherCurrentFolderPath = otherFolder.filePath().toUtf8();
                    otherCurrentFolderPath.remove(0, otherFolderIt->path.size());
                }
                else
                {
                    QString str(otherFileInfo.absolutePath());
                    str.append("/");
                    str.append(otherFileInfo.fileName());
                    qDebug("getCurrentFileInfo failed with %s", qUtf8Printable(str));
                    continue;
                }

                // Both folder names should differ in case
                if (otherCurrentFolderName.compare(folderName, Qt::CaseSensitive) == 0)
                    continue;

                hash64_t otherFolderHash = hash64(otherCurrentFolderPath);

                // Skips if the folder in another sync folder is already in the renaming list
                if (otherFolderIt->foldersToRename.contains(otherFolderHash))
                    continue;

                QString folderPath(folderIt->path);
                folderPath.append(profile.filePath(renamedFolderIt.key()));

                // Finally, adds the folder from another sync folder to the renaming list
                otherFolderIt->foldersToRename.insert(otherFolderHash, {profile.filePath(renamedFolderIt.key()), otherFolder.filePath().toUtf8(), getFileAttributes(folderPath)});

                renamedFolderIt->lockedFlag = SyncFile::Locked;
                otherFolderIt->files[otherFolderHash].lockedFlag = SyncFile::Locked;

                // Marks all subdirectories of the renamed folder in our sync folder as locked
                QDirIterator dirIterator(folderPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();

                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].lockedFlag = SyncFile::LockedInternal;
                }

                // Marks all subdirectories of the folder that doesn't exist anymore in our sync folder as to be removed using the path from another sync folder
                QString oldPath(folderIt->path);
                oldPath.append(otherCurrentFolderPath);
                QDirIterator oldDirIterator(oldPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (oldDirIterator.hasNext())
                {
                    oldDirIterator.next();

                    QByteArray path(oldDirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].setToBeRemoved(true);
                }

                // Marks all subdirectories of the current folder in another sync folder as to be locked
                QByteArray otherPathToRename(otherFolderIt->path);
                otherPathToRename.append(otherCurrentFolderPath);
                QDirIterator otherDirIterator(otherPathToRename, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (otherDirIterator.hasNext())
                {
                    otherDirIterator.next();

                    QByteArray path(otherDirIterator.filePath().toUtf8());
                    path.remove(0, otherFolderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].lockedFlag = SyncFile::LockedInternal;
                }
            }
        }
    }

    TIMESTAMP(startTime, "Checked for changed case of folders.");
}

/*
===================
SyncManager::checkForMovedFiles

Detects moved & renamed files
===================
*/
void SyncManager::checkForMovedFiles(SyncProfile &profile)
{
    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->isActive())
            continue;

        QHash<Hash, SyncFile *> missingFiles;
        QHash<Hash, SyncFile *> newFiles;

        // Finds files that don't exist in our sync folder anymore
        for (QHash<Hash, SyncFile>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
            if (fileIt.value().type == SyncFile::File && !fileIt.value().exists() && fileIt->size >= m_movedFileMinSize)
                missingFiles.insert(fileIt.key(), &fileIt.value());

        removeSimilarFiles(missingFiles);

        if (missingFiles.isEmpty())
            continue;

        // Finds files that are new in our sync folder
        for (QHash<Hash, SyncFile>::iterator newFileIt = folderIt->files.begin(); newFileIt != folderIt->files.end(); ++newFileIt)
            if (newFileIt->type == SyncFile::File && newFileIt->newlyAdded() && newFileIt->exists() && newFileIt->size >= m_movedFileMinSize)
                newFiles.insert(newFileIt.key(), &newFileIt.value());

        removeSimilarFiles(newFiles);

        for (QHash<Hash, SyncFile *>::iterator newFileIt = newFiles.begin(); newFileIt != newFiles.end(); ++newFileIt)
        {
            bool abort = false;
            SyncFile *movedFile = nullptr;
            hash64_t movedFileHash;

            // Searches for a match between missed file and a newly added file
            for (QHash<Hash, SyncFile *>::iterator missingFileIt = missingFiles.begin(); missingFileIt != missingFiles.end(); ++missingFileIt)
            {
                if (missingFileIt.value()->size != newFileIt.value()->size)
                    continue;

#if defined(Q_OS_WIN) || defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                if (missingFileIt.value()->modifiedDate != newFileIt.value()->modifiedDate)
                    continue;
#endif

                movedFile = &folderIt->files[missingFileIt.key()];
                movedFileHash = missingFileIt.key().data;
                break;
            }

            if (!movedFile)
                continue;

            // Additional checks
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                const SyncFile &fileToBeMoved = otherFolderIt->files.value(movedFileHash);

                // Aborts if a file that needs to be moved doesn't exists in any sync folder
                if (!fileToBeMoved.exists())
                {
                    abort = true;
                    break;
                }

                // Other sync folders should not contain a newly added file
                if (otherFolderIt->files.contains(newFileIt.key()))
                {
                    abort = true;
                    break;
                }

                QString destPath(otherFolderIt->path);
                destPath.append(profile.filePath(newFileIt.key()));

                // Aborts if other sync folders have a file at the destination location
                // Also, the both paths should differ, as in the case of changing case of parent folder name, the file still exists in the destination path
                if (QFileInfo::exists(destPath))
                {
                    if (m_caseSensitiveSystem || profile.filePath(newFileIt.key()).compare(profile.filePath(movedFileHash), Qt::CaseInsensitive) != 0)
                    {
                        abort = true;
                        break;
                    }
                }

                // Aborts if files that need to be moved have different sizes or dates between different sync folders
#if !defined(Q_OS_WIN) && !defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                if (fileToBeMoved.size != folderIt->files.value(movedFileHash).size)
#else
                if (fileToBeMoved.size != folderIt->files.value(movedFileHash).size || fileToBeMoved.modifiedDate != movedFile->modifiedDate)
#endif
                {
                    abort = true;
                    break;
                }
            }

            if (abort)
                continue;

            // Adds a moved/renamed file for moving
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                const SyncFile &fileToMove = otherFolderIt->files.value(movedFileHash);

                newFileIt.value()->lockedFlag = SyncFile::Locked;
                movedFile->setToBeRemoved(true);

                QByteArray pathToMove(otherFolderIt->path);
                pathToMove.append(profile.filePath(movedFileHash));

                QByteArray pathToNewFile(folderIt->path);
                pathToNewFile.append(profile.filePath(newFileIt.key()));

                otherFolderIt->files[movedFileHash].lockedFlag = SyncFile::Locked;
                QByteArray path = profile.filePath(newFileIt.key());
                otherFolderIt->filesToMove.insert(movedFileHash, {path, pathToMove, getFileAttributes(pathToNewFile)});

#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                setFileModificationDate(pathToMove, QFileInfo(pathToNewFile).lastModified());
#endif
            }
        }
    }

    TIMESTAMP(startTime, "Checked for moved/renamed files.");
}

/*
===================
SyncManager::checkForAddedFiles

Checks for added/modified files and folders
===================
*/
void SyncManager::checkForAddedFiles(SyncProfile &profile)
{
    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists)
                continue;

            if (!folderIt->isActive())
                break;

            for (QHash<Hash, SyncFile>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (!otherFolderIt->isActive())
                    break;

                if (!otherFileIt.value().exists())
                    continue;

                const SyncFile &file = folderIt->files.value(otherFileIt.key());
                const SyncFile &otherFile = otherFileIt.value();

                if (file.lockedFlag != SyncFile::Unlocked || file.toBeRemoved() || otherFile.lockedFlag != SyncFile::Unlocked || otherFile.toBeRemoved())
                    continue;

                bool alreadyAdded = folderIt->filesToCopy.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToCopy.value(otherFileIt.key()).modifiedDate < otherFile.modifiedDate;

                // Removes a file path from the "to remove" list if a file was updated
                if (otherFile.type == SyncFile::File)
                {
                    if (otherFile.updated())
                        otherFolderIt->filesToRemove.remove(otherFileIt.key());

                    if (file.updated() || (otherFile.exists() && folderIt->filesToRemove.contains(otherFileIt.key()) && !otherFolderIt->filesToRemove.contains(otherFileIt.key())))
                        folderIt->filesToRemove.remove(otherFileIt.key());
                }
                else if (otherFile.type == SyncFile::Folder)
                {
                    if (otherFile.updated())
                        otherFolderIt->foldersToRemove.remove(otherFileIt.key());

                    if (file.updated() || (otherFile.exists() && folderIt->foldersToRemove.contains(otherFileIt.key()) && !otherFolderIt->foldersToRemove.contains(otherFileIt.key())))
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                }

                // Checks for the newest version of a file in case if we have three folders or more
                if (alreadyAdded && !hasNewer)
                    continue;

                if ((!folderIt->files.contains(otherFileIt.key()) || file.isOlder(otherFile) ||
                    // Or if other folders has a new version of a file and our file was removed
                     (!file.exists() && (otherFile.updated() || otherFolderIt->isTopFolderUpdated(profile, otherFileIt.key().data)))))
                {
                    // Aborts if a file is supposed to be removed
                    if (otherFile.type == SyncFile::File)
                    {
                        if (otherFolderIt->filesToRemove.contains(otherFileIt.key()))
                            continue;
                    }
                    else if (otherFile.type == SyncFile::Folder)
                    {
                        if (otherFolderIt->foldersToRemove.contains(otherFileIt.key()))
                            continue;
                    }

                    if (otherFile.type == SyncFile::Folder)
                    {
                        QByteArray path = profile.filePath(otherFileIt.key());

                        auto it = folderIt->foldersToCreate.insert(otherFileIt.key(), {path, otherFile.attributes});
                        it->path.squeeze();

                        folderIt->foldersToRemove.remove(otherFileIt.key());
                    }
                    else
                    {
                        QByteArray to(profile.filePath(otherFileIt.key()));
                        QByteArray from(otherFolderIt->path);
                        from.append(profile.filePath(otherFileIt.key()));

                        auto it = folderIt->filesToCopy.insert(otherFileIt.key(), {to, from, otherFile.modifiedDate});
                        it->toPath.squeeze();
                        it->fromFullPath.squeeze();
                        folderIt->filesToRemove.remove(otherFileIt.key());
                    }
                }
            }
        }
    }

    TIMESTAMP(startTime, "Checked for added/modified files and folders.");
}

/*
===================
SyncManager::checkForRemovedFiles
===================
*/
void SyncManager::checkForRemovedFiles(SyncProfile &profile)
{
    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (!folderIt->isActive())
                break;

            // A file was moved and should be removed from a file datebase
            if (fileIt->toBeRemoved())
            {
                fileIt = folderIt->files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
                continue;
            }

            if (fileIt->exists() || fileIt->lockedFlag != SyncFile::Unlocked)
            {
                ++fileIt;
                continue;
            }

            if (fileIt->type == SyncFile::File)
            {
                if (folderIt->filesToMove.contains(fileIt.key()) ||
                    folderIt->filesToCopy.contains(fileIt.key()) ||
                    folderIt->filesToRemove.contains(fileIt.key()))
                {
                    ++fileIt;
                    continue;
                }
            }
            else if (fileIt->type == SyncFile::Folder)
            {
                if (folderIt->foldersToRename.contains(fileIt.key()) ||
                    folderIt->foldersToCreate.contains(fileIt.key()) ||
                    folderIt->foldersToRemove.contains(fileIt.key()))
                {
                    ++fileIt;
                    continue;
                }
            }

            // Aborts if a removed file still exists, but with a different case
            if (!m_caseSensitiveSystem)
            {
                bool abort = false;

                // As a removed file doesn't have a file path anymore, we need to construct a file path using other files in other folders
                for (auto anotherFolderIt = profile.folders.begin(); anotherFolderIt != profile.folders.end(); ++anotherFolderIt)
                {
                    if (folderIt == anotherFolderIt || !anotherFolderIt->isActive())
                        continue;

                    if (profile.hasFilePath(fileIt.key()))
                    {
                        QString path(folderIt->path);
                        path.append(profile.filePath(fileIt.key()));

                        if (QFileInfo::exists(path))
                        {
                            abort = true;
                            break;
                        }
                    }
                }

                if (abort)
                {
                    ++fileIt;
                    continue;
                }
            }

            // Adds files from other folders for removal
            for (auto anotherFolderIt = profile.folders.begin(); anotherFolderIt != profile.folders.end(); ++anotherFolderIt)
            {
                if (folderIt == anotherFolderIt || !anotherFolderIt->isActive())
                    continue;

                const SyncFile &fileToRemove = anotherFolderIt->files.value(fileIt.key());

                if (fileToRemove.exists())
                {
                    QByteArray path = profile.filePath(fileIt.key());

                    if (fileIt.value().type == SyncFile::Folder)
                        anotherFolderIt->foldersToRemove.insert(fileIt.key(), path)->squeeze();
                    else
                        anotherFolderIt->filesToRemove.insert(fileIt.key(), path)->squeeze();
                }
                else
                {
                    anotherFolderIt->files.remove(fileIt.key());
                }
            }

            fileIt = folderIt->files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
        }
    }

    TIMESTAMP(startTime, "Checked for removed files.");
}

/*
===================
SyncManager::checkForChanges
===================
*/
void SyncManager::checkForChanges(SyncProfile &profile)
{
    if (!profile.isActive())
        return;

    checkForRenamedFolders(profile);

    if (m_detectMovedFiles)
        checkForMovedFiles(profile);

    checkForAddedFiles(profile);
    checkForRemovedFiles(profile);

    // Fixes double syncing of updated files on restart, as these states are no longer needed in the file database after syncing
    for (auto &folder : profile.folders)
    {
        for (auto &file : folder.files)
        {
            file.setUpdated(false);
            file.setNewlyAdded(false);
        }
    }
}

/*
===================
SyncManager::removeFile
===================
*/
bool SyncManager::removeFile(SyncProfile &profile, SyncFolder &folder, const QString &path, const QString &fullPath, SyncFile::Type type)
{
    if (m_deletionMode == MoveToTrash)
    {
        // Used to make sure that moveToTrash function really moved a file/folder
        // to the trash as it can return true even though it failed to do so
        QString pathInTrash;

        return QFile::moveToTrash(fullPath, &pathInTrash) && !pathInTrash.isEmpty();
    }
    else if (m_deletionMode == Versioning)
    {
        QString newLocation(folder.versioningPath);
        newLocation.append(path);

        createParentFolders(profile, folder, QDir::cleanPath(newLocation).toUtf8());
        return QFile::rename(fullPath, newLocation);
    }
    else
    {
        if (type == SyncFile::Folder)
            return QDir(fullPath).removeRecursively() || !QFileInfo::exists(fullPath);
        else
            return QFile::remove(fullPath) || !QFileInfo::exists(fullPath);
    }
}

/*
===================
SyncManager::createParentFolders

Creates all necessary parent directories for a given file path
===================
*/
void SyncManager::createParentFolders(SyncProfile &profile, SyncFolder &folder, QByteArray path)
{
    QStack<QByteArray> foldersToCreate;

    while ((path = QFileInfo(path).path().toUtf8()).length() > folder.path.length())
    {
        if (QDir(path).exists())
            break;

        foldersToCreate.append(path);
    }

    while (!foldersToCreate.isEmpty())
    {
        if (QDir().mkdir(foldersToCreate.top()))
        {
            QByteArray relativePath(foldersToCreate.top());
            relativePath.remove(0, folder.path.size());
            hash64_t hash = hash64(relativePath);

            folder.files.insert(hash, SyncFile(SyncFile::Folder, QFileInfo(foldersToCreate.top()).lastModified()));
            folder.foldersToCreate.remove(hash);
            folder.foldersToUpdate.insert(foldersToCreate.top());
            profile.addFilePath(hash, relativePath);
        }

        foldersToCreate.pop();
    }
}

/*
===================
SyncManager::renameFolders
===================
*/
void SyncManager::renameFolders(SyncProfile &profile, SyncFolder &folder)
{
    for (auto folderIt = folder.foldersToRename.begin(); folderIt != folder.foldersToRename.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        // Removes from the "folders to rename" list if the source file doesn't exist
        if (!QFileInfo::exists(folderIt.value().fromFullPath))
        {
            folderIt = folder.foldersToRename.erase(static_cast<QHash<Hash, FolderToRenameInfo>::const_iterator>(folderIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(folderIt.value().toPath);
        hash64_t fileHash = hash64(folderIt.value().toPath);

        if (QDir().rename(folderIt.value().fromFullPath, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(folderIt.value().fromFullPath).path();

            setFileAttribute(filePath, folderIt.value().attributes);

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            folder.files.remove(hash64(QByteArray(folderIt.value().fromFullPath).remove(0, folder.path.size())));
            folder.files.insert(fileHash, SyncFile(SyncFile::Folder, QFileInfo(filePath).lastModified()));
            profile.addFilePath(fileHash, folderIt.value().toPath);
            folderIt = folder.foldersToRename.erase(static_cast<QHash<Hash, FolderToRenameInfo>::const_iterator>(folderIt));
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncManager::moveFiles
===================
*/
void SyncManager::moveFiles(SyncProfile &profile, SyncFolder &folder)
{
    for (auto fileIt = folder.filesToMove.begin(); fileIt != folder.filesToMove.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        // Removes from the "files to move" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().fromFullPath))
        {
            fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().toPath);
        hash64_t fileHash = hash64(fileIt.value().toPath);

        // Removes from the "files to move" list if a file already exists at the destination location
        if (QFileInfo::exists(filePath))
        {
            // For case-insensitive systems, both paths should not be the same
            if (m_caseSensitiveSystem || fileIt.value().fromFullPath.compare(filePath.toUtf8(), Qt::CaseInsensitive) != 0)
            {
                fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
                continue;
            }
        }

        createParentFolders(profile, folder, QDir::cleanPath(filePath).toUtf8());

        if (QFile::rename(fileIt.value().fromFullPath, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(fileIt.value().fromFullPath).path();

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            setFileAttribute(filePath, fileIt.value().attributes);

            hash64_t oldHash = hash64(QByteArray(fileIt.value().fromFullPath).remove(0, folder.path.size()));

            folder.files.remove(oldHash);

            auto it = folder.files.insert(fileHash, SyncFile(SyncFile::File, QFileInfo(filePath).lastModified()));
            it->size = QFileInfo(filePath).size();
            profile.addFilePath(fileHash, fileIt.value().toPath);
            fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncManager::removeFolders
===================
*/
void SyncManager::removeFolders(SyncProfile &profile, SyncFolder &folder)
{
    // Sorts the folders for removal from the top to the bottom.
    // This ensures that the trash folder has the exact same folder structure as in the original destination.
    QVector<QString> sortedFoldersToRemove;
    sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
    for (const auto &str : std::as_const(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
    std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

    for (auto folderIt = sortedFoldersToRemove.begin(); folderIt != sortedFoldersToRemove.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        QString folderPath(folder.path);
        folderPath.append(*folderIt);
        hash64_t fileHash = hash64(folderIt->toUtf8());

        if (removeFile(profile, folder, *folderIt, folderPath, SyncFile::Folder) || !QDir().exists(folderPath))
        {
            folder.files.remove(fileHash);
            folder.foldersToRemove.remove(fileHash);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QString>::const_iterator>(folderIt));

            QString parentPath = QFileInfo(folderPath).path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncManager::removeFiles
===================
*/
void SyncManager::removeFiles(SyncProfile &profile, SyncFolder &folder)
{
    for (auto fileIt = folder.filesToRemove.begin(); fileIt != folder.filesToRemove.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        QString filePath(folder.path);
        filePath.append(*fileIt);
        hash64_t fileHash = hash64(*fileIt);

        if (removeFile(profile, folder, *fileIt, filePath, SyncFile::File) || !QFile().exists(filePath))
        {
            folder.files.remove(fileHash);
            fileIt = folder.filesToRemove.erase(static_cast<QHash<Hash, QByteArray>::const_iterator>(fileIt));

            QString parentPath = QFileInfo(filePath).path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncManager::createFolders
===================
*/
void SyncManager::createFolders(SyncProfile &profile, SyncFolder &folder)
{
    for (auto folderIt = folder.foldersToCreate.begin(); folderIt != folder.foldersToCreate.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        QString folderPath(folder.path);
        folderPath.append(folderIt->path);
        hash64_t fileHash = hash64(folderIt->path);
        QFileInfo fileInfo(folderPath);

        createParentFolders(profile, folder, QDir::cleanPath(folderPath).toUtf8());

        // Removes a file with the same filename first if exists
        if (fileInfo.exists() && fileInfo.isFile())
            removeFile(profile, folder, folderIt->path, folderPath, SyncFile::File);

        if (QDir().mkdir(folderPath) || fileInfo.exists())
        {
            auto newFolderIt = folder.files.insert(fileHash, SyncFile(SyncFile::Folder, fileInfo.lastModified()));
            newFolderIt->attributes = folderIt->attributes;
            profile.addFilePath(fileHash, folderIt->path);
            folderIt = folder.foldersToCreate.erase(static_cast<QHash<Hash, FolderToCreateInfo>::const_iterator>(folderIt));
            setFileAttribute(folderPath, newFolderIt->attributes);

            QString parentPath = QFileInfo(folderPath).path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncManager::copyFiles
===================
*/
void SyncManager::copyFiles(SyncProfile &profile, SyncFolder &folder)
{
    QString rootPath = QStorageInfo(folder.path).rootPath();
    bool shouldNotify = m_notificationList.contains(rootPath) ? !m_notificationList.value(rootPath)->isActive() : true;

    for (auto fileIt = folder.filesToCopy.begin(); fileIt != folder.filesToCopy.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        // Removes from the "files to copy" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().fromFullPath) || fileIt.value().toPath.isEmpty() || fileIt.value().fromFullPath.isEmpty())
        {
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().toPath);
        hash64_t fileHash = hash64(fileIt.value().toPath);
        const SyncFile &file = folder.files.value(fileHash);
        QFileInfo destination(filePath);

        // Destination file is a newly added file
        if (file.type == SyncFile::Unknown && destination.exists())
        {
            QFileInfo origin(fileIt.value().fromFullPath);

            // Aborts the copy operation if the origin file is older than the destination file
            if (destination.lastModified() > origin.lastModified())
            {
                fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
                continue;
            }

            // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
            // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
            if (!m_caseSensitiveSystem)
            {
                QByteArray fileName = fileIt.value().fromFullPath;
                fileName.remove(0, fileName.lastIndexOf("/") + 1);

                QByteArray originFilename = getCurrentFileInfo(origin.absolutePath(), origin.fileName(), QDir::Files).fileName().toUtf8();

                if (!originFilename.isEmpty())
                {
                    // Aborts the copy operation if the origin path and the path on a disk have different cases
                    if (originFilename.compare(fileName, Qt::CaseSensitive) != 0)
                    {
                        fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
                        continue;
                    }
                }
            }
        }

        createParentFolders(profile, folder, QDir::cleanPath(filePath).toUtf8());

        // Removes a file with the same filename first if exists
        if (destination.exists())
            removeFile(profile, folder, fileIt.value().toPath, filePath, file.type);

        if (QFile::copy(fileIt.value().fromFullPath, filePath))
        {
            QFileInfo fileInfo(filePath);

#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
            setFileModificationDate(filePath, fileIt.value().second.second);
#endif

            // Do not touch QFileInfo(filePath).lastModified()), as we want to get the latest modified date
            auto it = folder.files.insert(fileHash, SyncFile(SyncFile::File, fileInfo.lastModified()));
            it->size = fileInfo.size();
            it->attributes = getFileAttributes(filePath);
            profile.addFilePath(fileHash, fileIt.value().toPath);
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));

            QString parentPath = destination.path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            // Not enough disk space notification
            if (m_notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(fileIt.value().fromFullPath).size())
            {
                if (!m_notificationList.contains(rootPath))
                    m_notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                shouldNotify = false;
                m_notificationList.value(rootPath)->start(NOTIFICATION_COOLDOWN);
                emit warning(QString(tr("Not enough disk space on %1 (%2)")).arg(QStorageInfo(folder.path).displayName(), rootPath), "");
            }

            ++fileIt;
        }
    }
}

/*
===================
SyncManager::syncFiles
===================
*/
void SyncManager::syncFiles(SyncProfile &profile)
{
    synchronizeFileAttributes(profile);

    for (auto &folder : profile.folders)
    {
        if (!folder.isActive())
            continue;

        if (m_deletionMode == Versioning)
            folder.updateVersioningPath(m_versionFolder, m_versionPattern);

        renameFolders(profile, folder);
        moveFiles(profile, folder);
        removeFolders(profile, folder);
        removeFiles(profile, folder);
        createFolders(profile, folder);
        copyFiles(profile, folder);

        folder.versioningPath.clear();

        // Updates the modified date of the parent folders as adding/removing files and folders change their modified date
        for (auto folderIt = folder.foldersToUpdate.begin(); folderIt != folder.foldersToUpdate.end();)
        {
            hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, folder.path.size()));

            if (folder.files.contains(folderHash))
                folder.files[folderHash].modifiedDate = QFileInfo(*folderIt).lastModified();

            folderIt = folder.foldersToUpdate.erase(static_cast<QSet<QByteArray>::const_iterator>(folderIt));
        }
    }

    for (auto &folder : profile.folders)
        for (auto &file : folder.files)
            file.setAttributesUpdated(false);
}
