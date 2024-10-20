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

#include "SyncManager.h"
#include "MainWindow.h"

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
#include "Common.h"

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
    m_saveDatabase = settings.value("SaveDatabase", true).toBool();
    m_ignoreHiddenFiles = settings.value("IgnoreHiddenFiles", true).toBool();
    m_loadingPolicy = static_cast<SyncManager::LoadingPolicy>(settings.value("LoadingPolicy", SyncManager::LoadAsNeeded).toInt());
    m_databaseLocation = static_cast<SyncManager::DatabaseLocation>(settings.value("DatabaseLocation", SyncManager::Decentralized).toInt());
    m_detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    m_syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (m_syncTimeMultiplier <= 0) m_syncTimeMultiplier = 1;
    m_movedFileMinSize = settings.value("MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt();

    m_caseSensitiveSystem = settings.value("caseSensitiveSystem", m_caseSensitiveSystem).toBool();
    m_versionFolder = settings.value("VersionFolder", "[Deletions]").toString();
    m_versionPattern = settings.value("VersionPattern", "yyyy_M_d_h_m_s_z").toString();

    m_syncTimer.setSingleShot(true);

    if (m_syncingMode == SyncManager::Automatic)
        m_syncTimer.start(0);
}

/*
===================
SyncManager::~SyncManager
===================
*/
SyncManager::~SyncManager()
{
    if (m_saveDatabase && !m_loadingPolicy)
    {
        removeFileData();

        if (m_databaseLocation)
        {
            for (auto &profile : m_profiles)
            {
                if (profile.toBeRemoved)
                    continue;

                saveFileDataDecentralised(profile);
            }
        }
        else
        {
            saveFileDataLocally();
        }
    }
}

/*
===================
SyncManager::addToQueue
===================
*/
void SyncManager::addToQueue(int profileNumber)
{
    if (m_profiles.isEmpty() || m_queue.contains(profileNumber))
        return;

    // Adds the passed profile number to the sync queue
    if (profileNumber >= 0 && profileNumber < m_profiles.size())
    {
        if ((!m_profiles[profileNumber].paused || m_syncingMode != Automatic) && !m_profiles[profileNumber].toBeRemoved)
        {
            m_queue.enqueue(profileNumber);
        }
    }
    // If a profile number is not passed, adds all remaining profiles to the sync queue
    else
    {
        for (int i = 0; i < m_profiles.size(); i++)
        {
            if ((!m_profiles[i].paused || m_syncingMode != Automatic) && !m_profiles[i].toBeRemoved && !m_queue.contains(i))
            {
                m_queue.enqueue(i);
            }
        }
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
        if (!syncProfile(m_profiles[m_queue.head()]))
            return;

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
    m_syncHidden = false;
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
    for (int i = -1; auto &profile : m_profiles)
    {
        i++;
        profile.syncing = false;
        int existingFolders = 0;

        if (profile.toBeRemoved)
            continue;

        m_existingProfiles++;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((!m_queue.isEmpty() && m_queue.head() != i ) || folder.toBeRemoved)
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
        for (auto &folder : m_profiles[m_queue.head()].folders)
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
void SyncManager::updateTimer()
{
    if (m_syncingMode == SyncManager::Automatic)
    {
        if ((!m_busy && m_syncTimer.isActive()) || (!m_syncTimer.isActive() || m_syncEvery < m_syncTimer.remainingTime()))
        {
            m_syncTimer.start(m_syncEvery);
        }
    }
}

/*
===================
SyncManager::updateNextSyncingTime
===================
*/
void SyncManager::updateNextSyncingTime()
{
    int time = 0;

    // Counts the total syncing time of profiles with at least two active folders
    for (const auto &profile : m_profiles)
        if (profile.isActive())
            time += profile.syncTime;

    // Multiplies sync time by 2
    for (int i = 0; i < m_syncTimeMultiplier - 1; i++)
    {
        time <<= 1;

        // If exceeds the maximum value of an int
        if (time < 0)
        {
            time = std::numeric_limits<int>::max();
            break;
        }
    }

    if (time < SYNC_MIN_DELAY)
        time = SYNC_MIN_DELAY;

    m_syncEvery = time;
}

/*
===================
SyncManager::saveFileDataLocally
===================
*/
void SyncManager::saveFileDataLocally() const
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATABASE_FILENAME);
    if (!data.open(QIODevice::WriteOnly))
        return;

    QDataStream stream(&data);

    stream << m_profiles.size();

    QStringList profileNames;

    for (auto &profile : m_profiles)
        profileNames.append(profile.name);

    for (int i = 0; auto &profile : m_profiles)
    {
        if (profile.toBeRemoved)
            continue;

        stream << profileNames[i];
        stream << profile.folders.size();

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved)
                continue;

            // File data
            stream << folder.path;
            saveToFileData(folder, stream);
        }

        i++;
    }
}

/*
===================
SyncManager::saveFileDataDecentralised
===================
*/
void SyncManager::saveFileDataDecentralised(const SyncProfile &profile) const
{
    for (auto &folder : profile.folders)
    {
        if (folder.toBeRemoved)
            continue;

        QDir().mkdir(folder.path + DATA_FOLDER_PATH);

        if (!QDir(folder.path + DATA_FOLDER_PATH).exists())
            continue;

        QFile data(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
        if (!data.open(QIODevice::WriteOnly))
            continue;

#ifdef Q_OS_WIN
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH), true);
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
#endif

        QDataStream stream(&data);
        saveToFileData(folder, stream);
    }
}

/*
===================
SyncManager::loadFileDataLocally
===================
*/
void SyncManager::loadFileDataLocally()
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATABASE_FILENAME);
    if (!data.open(QIODevice::ReadOnly))
        return;

    QDataStream stream(&data);
    QStringList profileNames;
    qsizetype profilesSize;

    stream >> profilesSize;

    for (auto &profile : m_profiles)
        profileNames.append(profile.name);

    for (qsizetype i = 0; i < profilesSize; i++)
    {
        QString profileName;
        qsizetype foldersSize;

        stream >> profileName;
        stream >> foldersSize;

        int profileIndex = profileNames.indexOf(profileName);

        for (qsizetype j = 0; j < foldersSize; j++)
        {
            QByteArray folderPath;
            stream >> folderPath;

            QStringList folderPaths;
            bool folderExists = profileIndex >= 0;

            if (folderExists)
            {
                for (auto &folder : m_profiles[profileIndex].folders)
                    folderPaths.append(folder.path);
            }

            int folderIndex = folderPaths.indexOf(folderPath);
            folderExists = folderExists && folderIndex >= 0;

            loadFromFileData(m_profiles[profileIndex].folders[folderIndex], stream, !folderExists);
        }
    }
}

/*
===================
SyncManager::loadFileDataDecentralised
===================
*/
void SyncManager::loadFileDataDecentralised(SyncProfile &profile)
{
    for (auto &folder : profile.folders)
    {
        QFile data(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
        if (!data.open(QIODevice::ReadOnly))
            continue;

        QDataStream stream(&data);
        loadFromFileData(folder, stream, false);
    }
}

/*
===================
SyncManager::removeFileData
===================
*/
void SyncManager::removeFileData()
{
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATABASE_FILENAME);

    for (auto &profile : m_profiles)
        for (auto &folder : profile.folders)
            removeFileData(folder);
}

/*
===================
SyncManager::removeFileData
===================
*/
void SyncManager::removeFileData(const SyncFolder &folder)
{
    QDir(folder.path + DATA_FOLDER_PATH).removeRecursively();
}

/*
===================
SyncManager::setLoadingPolicy
===================
*/
void SyncManager::setLoadingPolicy(SyncManager::LoadingPolicy policy)
{
    if (!m_saveDatabase || (m_loadingPolicy == policy && m_loadingPolicy == LoadAsNeeded))
        return;

    m_loadingPolicy = policy;

    if (policy == SyncManager::AlwaysLoaded)
    {
        for (auto &profile : m_profiles)
            for (auto &folder : profile.folders)
                folder.clearData();

        if (databaseLocation() == SyncManager::Decentralized)
        {
            for (auto &profile : m_profiles)
            {
                if (profile.toBeRemoved)
                    continue;

                loadFileDataDecentralised(profile);
            }
        }
        else
        {
            loadFileDataLocally();
        }
    }
    else if (policy == SyncManager::LoadAsNeeded)
    {
        if (databaseLocation() == SyncManager::Decentralized)
        {
            for (auto &profile : m_profiles)
            {
                if (profile.toBeRemoved)
                    continue;

                saveFileDataDecentralised(profile);
            }
        }
        else
        {
            saveFileDataLocally();
        }

        for (auto &profile : profiles())
            for (auto &folder : profile.folders)
                folder.clearData();
    }
}

/*
===================
SyncManager::saveToFileData
===================
*/
void SyncManager::saveToFileData(const SyncFolder &folder, QDataStream &stream) const
{
    // File data
    stream << folder.files.size();

    for (auto fileIt = folder.files.begin(); fileIt != folder.files.end(); fileIt++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(SyncFile::Type) + sizeof(qint8) + sizeof(Attributes);
        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<hash64_t *>(p) = fileIt.key().data;
        p += sizeof(hash64_t);
        qstrncpy(p, reinterpret_cast<char *>(const_cast<QDateTime *>(&fileIt->date)), sizeof(QDateTime));
        p += sizeof(QDateTime);
        *reinterpret_cast<qint64 *>(p) = fileIt->size;
        p += sizeof(qint64);
        *reinterpret_cast<SyncFile::Type *>(p) = fileIt->type;
        p += sizeof(SyncFile::Type);
        *reinterpret_cast<qint8 *>(p) = fileIt->flags;
        p += sizeof(qint8);
        *reinterpret_cast<Attributes *>(p) = fileIt->attributes;

        stream.writeRawData(&buf[0], bufSize);
    }

    // Folders to rename
    stream << folder.foldersToRename.size();

    for (const auto &fileIt : folder.foldersToRename)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(Attributes);
        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<QByteArray *>(p) = fileIt.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<QByteArray *>(p) = fileIt.second.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<Attributes *>(p) = fileIt.second.second;

        stream.writeRawData(&buf[0], bufSize);
    }

    // Files to move
    stream << folder.filesToMove.size();

    for (const auto &fileIt : folder.filesToMove)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(Attributes);
        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<QByteArray *>(p) = fileIt.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<QByteArray *>(p) = fileIt.second.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<Attributes *>(p) = fileIt.second.second;

        stream.writeRawData(&buf[0], bufSize);
    }

    // Folders to create
    stream << folder.foldersToCreate.size();

    for (const auto &fileIt : folder.foldersToCreate)
    {
        const size_t bufSize = sizeof(QByteArray) + sizeof(Attributes);
        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<QByteArray *>(p) = fileIt.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<Attributes *>(p) = fileIt.second;

        stream.writeRawData(&buf[0], bufSize);
    }

    // Files to copy
    stream << folder.filesToCopy.size();

    for (const auto &fileIt : folder.filesToCopy)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(QDateTime);
        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<QByteArray *>(p) = fileIt.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<QByteArray *>(p) = fileIt.second.first;
        p += sizeof(QByteArray);
        *reinterpret_cast<QDateTime *>(p) = fileIt.second.second;

        stream.writeRawData(&buf[0], bufSize);
    }

    // Folders to remove
    stream << folder.foldersToRemove.size();

    for (const auto &path : folder.foldersToRemove)
        stream.writeRawData(reinterpret_cast<char *>(const_cast<QByteArray *>(&path)), sizeof(path));

    // Files to remove
    stream << folder.filesToRemove.size();

    for (const auto &path : folder.filesToRemove)
        stream.writeRawData(reinterpret_cast<char *>(const_cast<QByteArray *>(&path)), sizeof(path));
}

/*
===================
SyncManager::loadFromFileData
===================
*/
void SyncManager::loadFromFileData(SyncFolder &folder, QDataStream &stream, bool dry)
{
    SET_TIME(startTime);

    qsizetype numOfFiles;
    stream >> numOfFiles;

    if (!dry)
        folder.files.reserve(numOfFiles);

    // File data
    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(SyncFile::Type) + sizeof(qint8) + sizeof(Attributes);
        char buf[bufSize];

        stream.readRawData(&buf[0], bufSize);

        if (!dry)
        {
            char *p = buf;
            hash64_t hash;
            QDateTime date;
            qint64 size;
            SyncFile::Type type;
            qint8 flags;
            Attributes attributes;

            hash = *reinterpret_cast<hash64_t *>(p);
            p += sizeof(hash64_t);
            date = *reinterpret_cast<QDateTime *>(p);
            p += sizeof(QDateTime);
            size = *reinterpret_cast<qint64 *>(p);
            p += sizeof(qint64);
            type = *reinterpret_cast<SyncFile::Type *>(p);
            p += sizeof(SyncFile::Type);
            flags = *reinterpret_cast<qint8 *>(p);
            p += sizeof(qint8);
            attributes = *reinterpret_cast<Attributes *>(p);

            const auto it = folder.files.insert(hash, SyncFile(QByteArray(), type, date, flags | SyncFile::OnRestore));
            it->size = size;
            it->path.squeeze();
            it->attributes = attributes;
        }
    }

    // Folders to rename
    stream >> numOfFiles;

    if (!dry)
        folder.foldersToRename.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(Attributes);
        char buf[bufSize];

        stream.readRawData(&buf[0], bufSize);

        if (!dry)
        {
            char *p = buf;
            QByteArray to;
            QByteArray from;
            Attributes attributes;

            to = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            from = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            attributes = *reinterpret_cast<Attributes *>(p);

            QPair<QByteArray, QPair<QByteArray, Attributes>> pair(to, QPair<QByteArray, Attributes>(from, attributes));
            const auto it = folder.foldersToRename.insert(hash64(to), pair);
            it.value().first.squeeze();
            it.value().second.first.squeeze();
        }
    }

    // Files to move
    stream >> numOfFiles;

    if (!dry)
        folder.filesToMove.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(Attributes);
        char buf[bufSize];

        stream.readRawData(&buf[0], bufSize);

        if (!dry)
        {
            char *p = buf;
            QByteArray to;
            QByteArray from;
            Attributes attributes;

            to = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            from = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            attributes = *reinterpret_cast<Attributes *>(p);

            QPair<QByteArray, QPair<QByteArray, Attributes>> pair(to, QPair<QByteArray, Attributes>(from, attributes));
            const auto it = folder.filesToMove.insert(hash64(to), pair);
            it.value().first.squeeze();
            it.value().second.first.squeeze();
        }
    }

    // Folders to create
    stream >> numOfFiles;

    if (!dry)
        folder.foldersToCreate.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        const size_t bufSize = sizeof(QByteArray) + sizeof(Attributes);
        char buf[bufSize];

        stream.readRawData(&buf[0], bufSize);

        if (!dry)
        {
            char *p = buf;
            QByteArray path;
            Attributes attributes;

            path = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            attributes = *reinterpret_cast<Attributes *>(p);

            const auto it = folder.foldersToCreate.insert(hash64(path), QPair<QByteArray, qint32>(path, attributes));
            it->first.squeeze();
        }
    }    

    // Files to copy
    stream >> numOfFiles;

    if (!dry)
        folder.filesToCopy.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        const size_t bufSize = sizeof(QByteArray) * 2 + sizeof(QDateTime);
        char buf[bufSize];

        stream.readRawData(&buf[0], bufSize);

        if (!dry)
        {
            char *p = buf;
            QByteArray to;
            QByteArray from;
            QDateTime time;

            to = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            from = *reinterpret_cast<QByteArray *>(p);
            p += sizeof(QByteArray);
            time = *reinterpret_cast<QDateTime *>(p);

            QPair<QByteArray, QPair<QByteArray, QDateTime>> pair(to, QPair<QByteArray, QDateTime>(from, time));
            const auto it = folder.filesToCopy.insert(hash64(to), pair);
            it.value().first.squeeze();
            it.value().second.first.squeeze();
        }
    }

    // Folders to remove
    stream >> numOfFiles;

    if (!dry)
        folder.foldersToRemove.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        QByteArray path;
        stream.readRawData(reinterpret_cast<char *>(&path), sizeof(path));

        if (!dry)
        {
            const auto it = folder.foldersToRemove.insert(hash64(path), path);
            it->squeeze();
        }
    }

    // Files to remove
    stream >> numOfFiles;

    if (!dry)
        folder.filesToRemove.reserve(numOfFiles);

    for (qsizetype k = 0; k < numOfFiles; k++)
    {
        QByteArray path;
        stream.readRawData(reinterpret_cast<char *>(&path), sizeof(path));

        if (!dry)
        {
            const auto it = folder.filesToRemove.insert(hash64(path), path);
            it->squeeze();
        }
    }

    folder.optimizeMemoryUsage();

    TIMESTAMP(startTime, "Loaded %s from file data", qUtf8Printable(folder.path));
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

    if (m_saveDatabase && m_loadingPolicy == SyncManager::LoadAsNeeded)
    {
        if (m_databaseLocation == Decentralized)
            loadFileDataDecentralised(profile);
        else
            loadFileDataLocally();
    }

    if (!m_paused)
    {
        QElapsedTimer timer;
        timer.start();

        // Counts active folders in a profile
        for (auto &folder : profile.folders)
            folder.exists = QFileInfo::exists(folder.path);

        if (profile.isActive())
        {
#ifdef DEBUG
            qDebug("=======================================");
            qDebug("Started syncing %s", qUtf8Printable(profile.name));
            qDebug("=======================================");
#endif

            // Gets lists of all files in folders
            SET_TIME(startTime);

            int result = 0;
            QList<QPair<Hash, QFuture<int>>> futureList;

            for (auto &folder : profile.folders)
            {
                hash64_t requiredDevice = hash64(QStorageInfo(folder.path).device());
                QPair<Hash, QFuture<int>> pair(requiredDevice, QFuture(QtConcurrent::run([&](){ return getListOfFiles(folder, profile.excludeList); })));
                pair.second.suspend();
                futureList.append(pair);
            }

            while (!futureList.isEmpty())
            {
                for (auto futureIt = futureList.begin(); futureIt != futureList.end();)
                {
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

#ifdef DEBUG
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
                qDebug("---------------------------------------");
                if (numOfFoldersToRename)   qDebug("Folders to rename: %d", numOfFoldersToRename);
                if (numOfFilesToMove)       qDebug("Files to move: %d", numOfFilesToMove);
                if (numOfFoldersToCreate)   qDebug("Folders to create: %d", numOfFoldersToCreate);
                if (numOffilesToCopy)       qDebug("Files to copy: %d", numOffilesToCopy);
                if (numOfFoldersToRemove)   qDebug("Folders to remove: %d", numOfFoldersToRemove);
                if (numOfFilesToRemove)     qDebug("Files to remove: %d", numOfFilesToRemove);
                qDebug("---------------------------------------");
            }
#endif

            updateStatus();

            if (m_shouldQuit)
                return false;
        }

        syncFiles(profile);
    }

    for (auto &folder : profile.folders)
    {
        folder.clearFilePaths();
        folder.optimizeMemoryUsage();
    }

    // Resets locked flag after finishing files moving & folder renaming
    bool shouldReset = true;

    for (auto &folder : profile.folders)
    {
        if (!folder.filesToMove.empty() && !folder.foldersToRename.empty())
        {
            shouldReset = false;
            break;
        }
    }

    if (shouldReset)
    {
        for (auto &folder : profile.folders)
            for (auto &file : folder.files)
                file.lockedFlag = SyncFile::Unlocked;
    }

    // Last sync date update
    profile.lastSyncDate = QDateTime::currentDateTime();

    for (auto &folder : profile.folders)
        if (folder.isActive())
            folder.lastSyncDate = QDateTime::currentDateTime();

    emit profileSynced(&profile);

    updateStatus();
    updateNextSyncingTime();

    if (m_saveDatabase && m_loadingPolicy == SyncManager::LoadAsNeeded)
    {
        if (m_databaseLocation == Decentralized)
            saveFileDataDecentralised(profile);
        else
            saveFileDataLocally();

		for (auto &profile : profiles())
			for (auto &folder : profile.folders)
				folder.clearData();
    }

    TIMESTAMP(syncTime, "Syncing is complete.");
    return true;
}

/*
===================
SyncManager::getListOfFiles
===================
*/
int SyncManager::getListOfFiles(SyncFolder &folder, const QList<QByteArray> &excludeList)
{
    int totalNumOfFiles = 0;

    // Resets file states
    for (auto &file : folder.files)
    {
        file.setExists(false);
        file.setUpdated(file.onRestore() ? file.updated() : false);     // Keeps value if it was loaded from saved file data
        file.setOnRestore(false);
        file.setNewlyAdded(false);
    }

    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (m_shouldQuit || !folder.isActive())
            return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());

        if (ignoreHiddenFilesEnabled() && fileInfo.isHidden())
            continue;

        // Skips database files
        if (fileInfo.isHidden())
        {
            if (fileInfo.fileName().compare(DATA_FOLDER_PATH, Qt::CaseInsensitive) == 0)
                continue;

            if (fileInfo.fileName().compare(DATABASE_FILENAME, Qt::CaseInsensitive) == 0)
                continue;
        }

        QByteArray fullFilePath = fileInfo.filePath().toUtf8();
        QByteArray filePath(fullFilePath);
        filePath.remove(0, folder.path.size());

        bool shouldExclude = false;

        // Excludes unwanted files and folder from scanning
        for (auto &exclude : excludeList)
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

        // If a file is already in our database
        if (folder.files.contains(fileHash))
        {
            SyncFile &file = folder.files[fileHash];
            QDateTime fileDate(fileInfo.lastModified());

            // Restores filepath
            if (file.path.isEmpty())
            {
                file.path = filePath;
                file.path.squeeze();
            }

            // Quits if a hash collision is detected
            if (file.path != filePath)
            {
#ifndef DEBUG
                QMessageBox::critical(nullptr, QString("Hash collision detected!"), QString("%s vs %s").arg(qUtf8Printable(filePath), qUtf8Printable(file.path)));
#else
                qCritical("Hash collision detected: %s vs %s", qUtf8Printable(filePath), qUtf8Printable(file.path));
#endif

                m_shouldQuit = true;
                qApp->quit();
                return -1;
            }

            if (file.date != fileDate)
                file.setUpdated(true);

            if (file.attributes != getFileAttributes(fullFilePath))
                file.setAttributesUpdated(true);

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

            file.date = fileDate;
            file.size = fileInfo.size();
            file.type = type;
            file.setExists(true);
            file.attributes = getFileAttributes(fullFilePath);
        }
        else
        {
            SyncFile *file = folder.files.insert(fileHash, SyncFile(filePath, type, fileInfo.lastModified())).operator->();
            file->size = fileInfo.size();
            file->setNewlyAdded(true);
            file->path.squeeze();
            file->attributes = getFileAttributes(fullFilePath);
        }

        totalNumOfFiles++;
    }

    folder.optimizeMemoryUsage();
    m_usedDevices.remove(hash64(QStorageInfo(folder.path).device()));

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

                if (file.exists() && otherFile.exists() && file.hasOlderAttributes(otherFile))
                {
                    SyncFile &folder = folderIt->files[otherFileIt.key()];

                    QByteArray from(otherFolderIt->path);
                    from.append(otherFile.path);

                    QByteArray to(folderIt->path);
                    to.append(otherFile.path);

                    setFileAttribute(to, getFileAttributes(from));
                    folder.attributes = otherFile.attributes;
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
                otherFolderPath.append(renamedFolderIt->path);
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
                otherFolderPath.append(renamedFolderIt->path);
                QFileInfo otherFileInfo(otherFolderPath);
                QByteArray otherCurrentFolderName;
                QByteArray otherCurrentFolderPath;

                QFileInfo otherFolder = getCurrentFileInfo(otherFileInfo.absolutePath(), {otherFileInfo.fileName()}, QDir::Dirs);

                if (otherFolder.exists())
                {
                    otherCurrentFolderName = otherFolder.fileName().toUtf8();
                    otherCurrentFolderPath = otherFolder.filePath().toUtf8();
                    otherCurrentFolderPath.remove(0, otherFolderIt->path.size());
                }

                QByteArray folderName(renamedFolderIt->path);
                folderName.remove(0, folderName.lastIndexOf("/") + 1);

                // Both folder names should differ in case
                if (otherCurrentFolderName.compare(folderName, Qt::CaseSensitive) == 0)
                    continue;

                hash64_t otherFolderHash = hash64(otherCurrentFolderPath);

                // Skips if the folder in another sync folder is already in the renaming list
                if (otherFolderIt->foldersToRename.contains(otherFolderHash))
                    continue;

                QString folderPath(folderIt->path);
                folderPath.append(renamedFolderIt->path);

                // Finally, adds the folder from another sync folder to the renaming list
                QPair<QByteArray, QPair<QByteArray, Attributes>> pair(renamedFolderIt->path, QPair<QByteArray, Attributes>(otherFolder.filePath().toUtf8(), getFileAttributes(folderPath)));
                otherFolderIt->foldersToRename.insert(otherFolderHash, pair);

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
                if (missingFileIt.value()->date != newFileIt.value()->date)
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
                destPath.append(newFileIt.value()->path);

                // Aborts if other sync folders have a file at the destination location
                // Also, the both paths should differ, as in the case of changing case of parent folder name, the file still exists in the destination path
                if (QFileInfo::exists(destPath))
                {
                    if (m_caseSensitiveSystem || newFileIt.value()->path.compare(fileToBeMoved.path, Qt::CaseInsensitive) != 0)
                    {
                        abort = true;
                        break;
                    }
                }

                // Aborts if files that need to be moved have different sizes or dates between different sync folders
#if !defined(Q_OS_WIN) && !defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                if (otherFolderIt->files.value(movedFileHash).size != folderIt->files.value(movedFileHash).size)
#else
                if (otherFolderIt->files.value(movedFileHash).size != folderIt->files.value(movedFileHash).size || fileToBeMoved.date != movedFile->date)
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
                pathToMove.append(fileToMove.path);

                QByteArray pathToNewFile(folderIt->path);
                pathToNewFile.append(newFileIt.value()->path);

                otherFolderIt->files[movedFileHash].lockedFlag = SyncFile::Locked;
                otherFolderIt->filesToMove.insert(movedFileHash, QPair<QByteArray, QPair<QByteArray, Attributes>>(newFileIt.value()->path, QPair<QByteArray, Attributes>(pathToMove, getFileAttributes(pathToNewFile))));

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
                bool hasNewer = alreadyAdded && folderIt->filesToCopy.value(otherFileIt.key()).second.second < otherFile.date;

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
                     (!file.exists() && (otherFile.updated() || otherFolderIt->isTopFolderUpdated(otherFile)))))
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
                        folderIt->foldersToCreate.insert(otherFileIt.key(), QPair<QByteArray, qint32>(otherFile.path, otherFile.attributes))->first.squeeze();
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                    }
                    else
                    {
                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);
                        QPair<QByteArray, QPair<QByteArray, QDateTime>> pair(otherFile.path, QPair<QByteArray, QDateTime>(from, otherFile.date));

                        QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>>::iterator it = folderIt->filesToCopy.insert(otherFileIt.key(), pair);
                        it->first.squeeze();
                        it->second.first.squeeze();
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

                    const SyncFile &fileToRemove = anotherFolderIt->files.value(fileIt.key());

                    if (!fileToRemove.path.isEmpty())
                    {
                        QString path(folderIt->path);
                        path.append(fileToRemove.path);

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
                    if (fileIt.value().type == SyncFile::Folder)
                        anotherFolderIt->foldersToRemove.insert(fileIt.key(), fileToRemove.path)->squeeze();
                    else
                        anotherFolderIt->filesToRemove.insert(fileIt.key(), fileToRemove.path)->squeeze();
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
bool SyncManager::removeFile(SyncFolder &folder, const QString &path, const QString &fullPath, const QString &versioningPath, SyncFile::Type type)
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
        QString newLocation(versioningPath);
        newLocation.append(path);

        createParentFolders(folder, QDir::cleanPath(newLocation).toUtf8());
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
===================
*/
void SyncManager::createParentFolders(SyncFolder &folder, QByteArray path)
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
            QByteArray shortPath(foldersToCreate.top());
            shortPath.remove(0, folder.path.size());
            hash64_t hash = hash64(shortPath);

            folder.files.insert(hash, SyncFile(shortPath, SyncFile::Folder, QFileInfo(foldersToCreate.top()).lastModified()));
            folder.foldersToCreate.remove(hash);
            folder.foldersToUpdate.insert(foldersToCreate.top());
        }

        foldersToCreate.pop();
    }
}

/*
===================
SyncManager::renameFolders
===================
*/
void SyncManager::renameFolders(SyncFolder &folder)
{
    for (auto folderIt = folder.foldersToRename.begin(); folderIt != folder.foldersToRename.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        // Removes from the "folders to rename" list if the source file doesn't exist
        if (!QFileInfo::exists(folderIt.value().second.first))
        {
            folderIt = folder.foldersToRename.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>>::const_iterator>(folderIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(folderIt.value().first);
        hash64_t fileHash = hash64(folderIt.value().first);

        if (QDir().rename(folderIt.value().second.first, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(folderIt.value().second.first).path();

            setFileAttribute(filePath, folderIt.value().second.second);

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            folder.files.remove(hash64(QByteArray(folderIt.value().second.first).remove(0, folder.path.size())));
            folder.files.insert(fileHash, SyncFile(folderIt.value().first, SyncFile::Folder, QFileInfo(filePath).lastModified()));
            folderIt = folder.foldersToRename.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>>::const_iterator>(folderIt));
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
void SyncManager::moveFiles(SyncFolder &folder)
{
    for (auto fileIt = folder.filesToMove.begin(); fileIt != folder.filesToMove.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        // Removes from the "files to move" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().second.first))
        {
            fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().first);
        hash64_t fileHash = hash64(fileIt.value().first);

        // Removes from the "files to move" list if a file already exists at the destination location
        if (QFileInfo::exists(filePath))
        {
            // For case-insensitive systems, both paths should not be the same
            if (m_caseSensitiveSystem || fileIt.value().second.first.compare(filePath.toUtf8(), Qt::CaseInsensitive) != 0)
            {
                fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>>::const_iterator>(fileIt));
                continue;
            }
        }

        createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

        if (QFile::rename(fileIt.value().second.first, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(fileIt.value().second.first).path();

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            setFileAttribute(filePath, fileIt.value().second.second);

            hash64_t oldHash = hash64(QByteArray(fileIt.value().second.first).remove(0, folder.path.size()));

            folder.files.remove(oldHash);
            auto it = folder.files.insert(fileHash, SyncFile(fileIt.value().first, SyncFile::File, QFileInfo(filePath).lastModified()));
            it->size = QFileInfo(filePath).size();
            fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, Attributes>>>::const_iterator>(fileIt));
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
void SyncManager::removeFolders(SyncFolder &folder, const QString &versioningPath)
{
    // Sorts the folders for removal from the top to the bottom.
    // This ensures that the trash folder has the exact same folder structure as in the original destination.
    QVector<QString> sortedFoldersToRemove;
    sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
    for (const auto &str : std::as_const(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
    std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

    for (auto folderIt = sortedFoldersToRemove.begin(); folderIt != sortedFoldersToRemove.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        QString folderPath(folder.path);
        folderPath.append(*folderIt);
        hash64_t fileHash = hash64(folderIt->toUtf8());

        if (removeFile(folder, *folderIt, folderPath, versioningPath, SyncFile::Folder) || !QDir().exists(folderPath))
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
void SyncManager::removeFiles(SyncFolder &folder, const QString &versioningPath)
{
    for (auto fileIt = folder.filesToRemove.begin(); fileIt != folder.filesToRemove.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        QString filePath(folder.path);
        filePath.append(*fileIt);
        hash64_t fileHash = hash64(*fileIt);

        if (removeFile(folder, *fileIt, filePath, versioningPath, SyncFile::File) || !QFile().exists(filePath))
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
void SyncManager::createFolders(SyncFolder &folder, const QString &versioningPath)
{
    for (auto folderIt = folder.foldersToCreate.begin(); folderIt != folder.foldersToCreate.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        QString folderPath(folder.path);
        folderPath.append(folderIt->first);
        hash64_t fileHash = hash64(folderIt->first);
        QFileInfo fileInfo(folderPath);

        createParentFolders(folder, QDir::cleanPath(folderPath).toUtf8());

        // Removes a file with the same filename first if exists
        if (fileInfo.exists() && fileInfo.isFile())
            removeFile(folder, folderIt->first, folderPath, versioningPath, SyncFile::File);

        if (QDir().mkdir(folderPath) || fileInfo.exists())
        {
            auto newFolderIt = folder.files.insert(fileHash, SyncFile(folderIt->first, SyncFile::Folder, fileInfo.lastModified()));
            newFolderIt->attributes = folderIt->second;
            folderIt = folder.foldersToCreate.erase(static_cast<QHash<Hash, QPair<QByteArray, Attributes>>::const_iterator>(folderIt));
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
void SyncManager::copyFiles(SyncFolder &folder, const QString &versioningPath)
{
    QString rootPath = QStorageInfo(folder.path).rootPath();
    bool shouldNotify = m_notificationList.contains(rootPath) ? !m_notificationList.value(rootPath)->isActive() : true;

    for (auto fileIt = folder.filesToCopy.begin(); fileIt != folder.filesToCopy.end() && (!m_paused && folder.isActive());)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_saveDatabase)
            break;

        // Removes from the "files to copy" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().second.first) || fileIt.value().first.isEmpty() || fileIt.value().second.first.isEmpty())
        {
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().first);
        hash64_t fileHash = hash64(fileIt.value().first);
        const SyncFile &file = folder.files.value(fileHash);
        QFileInfo destination(filePath);

        // Destination file is a newly added file
        if (file.type == SyncFile::Unknown && destination.exists())
        {
            QFileInfo origin(fileIt.value().second.first);

            // Aborts the copy operation if the origin file is older than the destination file
            if (destination.lastModified() > origin.lastModified())
            {
                fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>>::const_iterator>(fileIt));
                continue;
            }

            // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
            // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
            if (!m_caseSensitiveSystem)
            {
                QByteArray originFilename = getCurrentFileInfo(origin.absolutePath(), {origin.fileName()}, QDir::Files).fileName().toUtf8();

                if (!originFilename.isEmpty())
                {
                    QByteArray fileName = fileIt.value().second.first;
                    fileName.remove(0, fileName.lastIndexOf("/") + 1);

                    // Aborts the copy operation if the origin path and the path on a disk have different cases
                    if (originFilename.compare(fileName, Qt::CaseSensitive) != 0)
                    {
                        fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>>::const_iterator>(fileIt));
                        continue;
                    }
                }
            }
        }

        createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

        // Removes a file with the same filename first if exists
        if (destination.exists())
            removeFile(folder, fileIt.value().first, filePath, versioningPath, file.type);

        if (QFile::copy(fileIt.value().second.first, filePath))
        {
            QFileInfo fileInfo(filePath);

#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
            setFileModificationDate(filePath, fileIt.value().second.second);
#endif

            // Do not touch QFileInfo(filePath).lastModified()), as we want to get the latest modified date
            auto it = folder.files.insert(fileHash, SyncFile(fileIt.value().first, SyncFile::File, fileInfo.lastModified()));
            it->size = fileInfo.size();
            it->attributes = getFileAttributes(filePath);
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, QPair<QByteArray, QPair<QByteArray, QDateTime>>>::const_iterator>(fileIt));

            QString parentPath = destination.path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            // Not enough disk space notification
            if (m_notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(fileIt.value().second.first).size())
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

        QString versioningPath(folder.path);
        versioningPath.remove(versioningPath.lastIndexOf("/", 1), versioningPath.size());
        versioningPath.append("_");
        versioningPath.append(m_versionFolder);
        versioningPath.append("/");
        versioningPath.append(QDateTime::currentDateTime().toString(m_versionPattern));
        versioningPath.append("/");

        renameFolders(folder);
        moveFiles(folder);
        removeFolders(folder, versioningPath);
        removeFiles(folder, versioningPath);
        createFolders(folder, versioningPath);
        copyFiles(folder, versioningPath);

        // Updates the modified date of the parent folders as adding/removing files and folders change their modified date
        for (auto folderIt = folder.foldersToUpdate.begin(); folderIt != folder.foldersToUpdate.end();)
        {
            hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, folder.path.size()));

            if (folder.files.contains(folderHash))
                folder.files[folderHash].date = QFileInfo(*folderIt).lastModified();

            folderIt = folder.foldersToUpdate.erase(static_cast<QSet<QByteArray>::const_iterator>(folderIt));
        }
    }

    for (auto &folder : profile.folders)
        for (auto &file : folder.files)
            file.setAttributesUpdated(false);
}
