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

#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#endif

#ifdef DEBUG
#include <chrono>

#define SET_TIME(t) debugSetTime(t);
#define TIMESTAMP(t, ...) debugTimestamp(t, __VA_ARGS__);

std::chrono::high_resolution_clock::time_point startTime;

/*
===================
debugSetTime
===================
*/
void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime)
{
    startTime = std::chrono::high_resolution_clock::now();
}

/*
===================
debugTimestamp
===================
*/
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...)
{
    char buffer[256];

    va_list ap;
    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
    qDebug("%lld ms - %s", ml.count(), buffer);
}
#else

#define SET_TIME(t)
#define TIMESTAMP(t, m, ...)

#endif // DEBUG

/*
===================
hash64
===================
*/
hash64_t hash64(const QByteArray &str)
{
    QByteArray hash = QCryptographicHash::hash(str, QCryptographicHash::Md5);
    QDataStream stream(hash);
    quint64 a, b;
    stream >> a >> b;
    return a ^ b;
}

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
    m_rememberFiles = settings.value("RememberFiles", true).toBool();
    m_detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    m_syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (m_syncTimeMultiplier <= 0) m_syncTimeMultiplier = 1;

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
    if (m_rememberFiles) saveData();
}

/*
===================
SyncManager::addToQueue
===================
*/
void SyncManager::addToQueue(int profileNumber)
{
    if (m_profiles.isEmpty())
        return;

    if (!m_queue.contains(profileNumber))
    {
        // Adds the passed profile number to the sync queue
        if (profileNumber >= 0 && profileNumber < m_profiles.size())
        {
            if ((!m_profiles[profileNumber].paused || m_syncingMode != Automatic) && !m_profiles[profileNumber].toBeRemoved)

            m_queue.enqueue(profileNumber);
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
    SyncProfile &profile = m_profiles[m_queue.head()];

#ifdef DEBUG
    std::chrono::high_resolution_clock::time_point syncTime;
    debugSetTime(syncTime);
#endif

    if (!m_paused)
    {
        int activeFolders = 0;
        QElapsedTimer timer;
        timer.start();

        // Counts active folders in a profile
        for (auto &folder : profile.folders)
        {
            folder.exists = QFileInfo::exists(folder.path);

            if (!folder.paused && folder.exists && !folder.toBeRemoved)
                activeFolders++;
        }

        if (activeFolders >= 2)
        {
#ifdef DEBUG
            qDebug("=======================================");
            qDebug("Started syncing %s", qUtf8Printable(profile.name));
            qDebug("=======================================");
#endif

            // Gets lists of all files in folders
            SET_TIME(startTime);

            int result = 0;
            QList<QPair<hash64_t, QFuture<int>>> futureList;

            for (auto &folder : profile.folders)
            {
                hash64_t requiredDevice = hash64(QStorageInfo(folder.path).device());
                QPair<hash64_t, QFuture<int>> pair(requiredDevice, QFuture(QtConcurrent::run([&](){ return getListOfFiles(folder, profile.excludeList); })));
                pair.second.suspend();
                futureList.append(pair);
            }

            while (!futureList.isEmpty())
            {
                int i = 0;

                for (auto it = futureList.begin(); it != futureList.end();)
                {
                    if (!m_usedDevices.contains(it->first))
                    {
                        m_usedDevices.insert(it->first);
                        it->second.resume();
                    }

                    if (it->second.isFinished())
                    {
                        result += it->second.result();
                        m_usedDevices.remove(it->first);
                        it = futureList.erase(static_cast<QList<QPair<hash64_t, QFuture<int>>>::const_iterator>(it));
                    }
                    else
                    {
                        it++;
                    }

                    i++;
                }

                if (m_shouldQuit) return;
            }

            TIMESTAMP(startTime, "Found %d files in %s.", result, qUtf8Printable(profile.name));

            checkForChanges(profile);

            bool countAverage = profile.syncTime ? true : false;
            profile.syncTime += timer.elapsed();
            if (countAverage) profile.syncTime /= 2;

#ifdef DEBUG
            int numOfFoldersToRename = 0;
            int numOfFilesToMove = 0;
            int numOfFoldersToAdd = 0;
            int numOfFilesToAdd = 0;
            int numOfFoldersToRemove = 0;
            int numOfFilesToRemove = 0;

            for (auto &folder : profile.folders)
            {
                numOfFoldersToRename += folder.foldersToRename.size();
                numOfFilesToMove += folder.filesToMove.size();
                numOfFoldersToAdd += folder.foldersToAdd.size();
                numOfFilesToAdd += folder.filesToAdd.size();
                numOfFoldersToRemove += folder.foldersToRemove.size();
                numOfFilesToRemove += folder.filesToRemove.size();
            }

            if (numOfFoldersToRename || numOfFilesToMove || numOfFoldersToAdd || numOfFilesToAdd || numOfFoldersToRemove || numOfFilesToRemove)
            {
                qDebug("---------------------------------------");
                if (numOfFoldersToRename)   qDebug("Folders to rename: %d", numOfFoldersToRename);
                if (numOfFilesToMove)       qDebug("Files to move: %d", numOfFilesToMove);
                if (numOfFoldersToAdd)      qDebug("Folders to add: %d", numOfFoldersToAdd);
                if (numOfFilesToAdd)        qDebug("Files to add: %d", numOfFilesToAdd);
                if (numOfFoldersToRemove)   qDebug("Folders to remove: %d", numOfFoldersToRemove);
                if (numOfFilesToRemove)     qDebug("Files to remove: %d", numOfFilesToRemove);
                qDebug("---------------------------------------");
            }
#endif

            updateStatus();
            if (m_shouldQuit) return;
        }

        if (m_syncing) syncFiles(profile);
    }

    // Optimizes memory usage
    for (auto &folder : profile.folders)
    {
        folder.files.squeeze();
        folder.filesToMove.squeeze();
        folder.foldersToAdd.squeeze();
        folder.filesToAdd.squeeze();
        folder.foldersToRemove.squeeze();
        folder.filesToRemove.squeeze();

        for (auto &file : folder.files)
            file.path.clear();

        for (auto it = folder.sizeList.begin(); it != folder.sizeList.end();)
        {
            if (!folder.files.contains(it.key()))
                it = folder.sizeList.erase(static_cast<QHash<hash64_t, qint64>::const_iterator>(it));
            else
                ++it;
        }
    }

    profile.lastSyncDate = QDateTime::currentDateTime();

    for (auto &folder : profile.folders)
        if (!folder.paused)
            folder.lastSyncDate = QDateTime::currentDateTime();

    emit profileSynced(&profile);

    m_queue.dequeue();
    m_busy = false;
    updateStatus();
    updateNextSyncingTime();

    TIMESTAMP(syncTime, "Syncing is complete.");

    // Starts synchronization of the next profile in the queue if exists
    if (!m_queue.empty())
        sync();

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

        m_existingProfiles++;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((!m_queue.isEmpty() && m_queue.head() != i ) || profile.toBeRemoved)
                continue;

            if (!folder.toBeRemoved)
            {
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
            }

            if (m_busy && folder.exists && !folder.paused && (!folder.foldersToRename.isEmpty() ||
                                                              !folder.filesToMove.isEmpty() ||
                                                              !folder.foldersToAdd.isEmpty() ||
                                                              !folder.filesToAdd.isEmpty() ||
                                                              !folder.foldersToRemove.isEmpty() ||
                                                              !folder.filesToRemove.isEmpty()))
            {
                m_syncing = true;
                profile.syncing = true;
                folder.syncing = true;
            }
        }
    }

    // Number of files left to sync
    qsizetype size = 0;

    if (m_busy)
    {
        for (auto &folder : m_profiles[m_queue.head()].folders)
        {
            if (folder.exists && !folder.paused)
            {
                size = folder.foldersToRename.size();
                size += folder.filesToMove.size();
                size += folder.foldersToAdd.size();
                size += folder.filesToAdd.size();
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
    {
        if (profile.paused)
            continue;

        int activeFolders = 0;

        for (const auto &folder : profile.folders)
            if (!folder.paused && folder.exists)
                activeFolders++;

        if (activeFolders >= 2)
            time += profile.syncTime;
    }

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

    if (time < SYNC_MIN_DELAY) time = SYNC_MIN_DELAY;
    m_syncEvery = time;
}

/*
===================
SyncManager::saveData
===================
*/
void SyncManager::saveData() const
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    if (!data.open(QIODevice::WriteOnly))
        return;

    QDataStream stream(&data);
    stream << m_profiles.size();

    QStringList profileNames;

    for (auto &profile : m_profiles)
        profileNames.append(profile.name);

    for (int i = 0; auto &profile : m_profiles)
    {
        if (profile.toBeRemoved) continue;

        stream << profileNames[i];
        stream << profile.folders.size();

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved)
                continue;

            // File data
            stream << folder.path;
            stream << folder.files.size();

            for (auto it = folder.files.begin(); it != folder.files.end(); it++)
            {
                stream << it.key();
                stream << it->date;
                stream << it->type;
                stream << it->updated;
                stream << it->exists;
            }

            // File sizes
            stream << folder.sizeList.size();

            for (auto it = folder.sizeList.begin(); it != folder.sizeList.end(); it++)
            {
                stream << it.key();
                stream << it.value();
            }

            // Folders to rename
            stream << folder.foldersToRename.size();

            for (const auto &it : folder.foldersToRename)
            {
                stream << it.first;
                stream << it.second;
            }

            // Files to move
            stream << folder.filesToMove.size();

            for (const auto &it : folder.filesToMove)
            {
                stream << it.first;
                stream << it.second;
            }

            // Folders to add
            stream << folder.foldersToAdd.size();

            for (const auto &path : folder.foldersToAdd)
                stream << path;

            // Files to add
            stream << folder.filesToAdd.size();

            for (const auto &it : folder.filesToAdd)
            {
                stream << it.first;
                stream << it.second;
            }

            // Folders to remove
            stream << folder.foldersToRemove.size();

            for (const auto &path : folder.foldersToRemove)
                stream << path;

            // Files to remove
            stream << folder.filesToRemove.size();

            for (const auto &path : folder.filesToRemove)
                stream << path;
        }

        i++;
    }
}

/*
===================
SyncManager::restoreData
===================
*/
void SyncManager::restoreData()
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    if (!data.open(QIODevice::ReadOnly))
        return;

    QDataStream stream(&data);

    qsizetype profilesSize;
    stream >> profilesSize;

    QStringList profileNames;

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
            qsizetype size;
            QByteArray folderPath;

            stream >> folderPath;
            stream >> size;

            QStringList folderPaths;
            bool restore = profileIndex >= 0;

            if (restore)
            {
                for (auto &folder : m_profiles[profileIndex].folders)
                    folderPaths.append(folder.path);
            }

            int folderIndex = folderPaths.indexOf(folderPath);
            restore = restore && folderIndex >= 0;

            // File data
            for (qsizetype k = 0; k < size; k++)
            {
                hash64_t hash;
                QDateTime date;
                File::Type type;
                bool updated;
                bool exists;

                stream >> hash;
                stream >> date;
                stream >> type;
                stream >> updated;
                stream >> exists;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].files.insert(hash, File(QByteArray(), type, date, updated, exists, true));
                    it->path.squeeze();
                }
            }

            // File sizes
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                hash64_t fileHash;
                qint64 fileSize;

                stream >> fileHash;
                stream >> fileSize;

                if (restore)
                {
                    m_profiles[profileIndex].folders[folderIndex].sizeList.insert(fileHash, fileSize);
                }
            }

            // Folders to rename
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray to;
                QByteArray from;

                stream >> to;
                stream >> from;

                if (restore)
                {
                    QPair<QByteArray, QByteArray> pair(to, from);
                    const auto it = m_profiles[profileIndex].folders[folderIndex].foldersToRename.insert(hash64(to), pair);
                    it.value().first.squeeze();
                    it.value().second.squeeze();
                }
            }

            // Files to move
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray to;
                QByteArray from;

                stream >> to;
                stream >> from;

                if (restore)
                {
                    QPair<QByteArray, QByteArray> pair(to, from);
                    const auto it = m_profiles[profileIndex].folders[folderIndex].filesToMove.insert(hash64(to), pair);
                    it.value().first.squeeze();
                    it.value().second.squeeze();
                }
            }

            // Folders to add
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].foldersToAdd.insert(hash64(path), path);
                    it->squeeze();
                }
            }

            // Files to add
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray to;
                QByteArray from;
                QDateTime time;

                stream >> to;
                stream >> from;
                stream >> time;

                if (restore)
                {
                    QPair<QPair<QByteArray, QByteArray>, QDateTime> pair(QPair<QByteArray, QByteArray>(to, from), time);
                    const auto it = m_profiles[profileIndex].folders[folderIndex].filesToAdd.insert(hash64(to), pair);
                    it.value().first.first.squeeze();
                    it.value().first.second.squeeze();
                }
            }

            // Folders to remove
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].foldersToRemove.insert(hash64(path), path);
                    it->squeeze();
                }
            }

            // Files to remove
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].filesToRemove.insert(hash64(path), path);
                    it->squeeze();
                }
            }
        }
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

            folder.files.insert(hash, File(shortPath, File::folder, QFileInfo(foldersToCreate.top()).lastModified()));
            folder.foldersToAdd.remove(hash);
            foldersToUpdate.insert(foldersToCreate.top());
        }

        foldersToCreate.pop();
    }
}

/*
===================
SyncManager::getListOfFiles
===================
*/
int SyncManager::getListOfFiles(SyncFolder &folder, const QList<QByteArray> &excludeList)
{
    if (folder.paused || !folder.exists)
        return -1;

    int totalNumOfFiles = 0;

    // Resets file states
    for (auto &file : folder.files)
    {
        file.exists = false;
        file.updated = (file.onRestore) ? file.updated : false;     // Keeps value if it was loaded from saved file data
        file.onRestore = false;
        file.newlyAdded = false;
    }

#ifndef USE_STD_FILESYSTEM
    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (folder.paused)
            return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        QByteArray filePath(fileInfo.filePath().toUtf8());
        filePath.remove(0, folder.path.size());
        File::Type type = fileInfo.isDir() ? File::folder : File::file;
        hash64_t fileHash = hash64(filePath);

        // Excludes unwanted files and folder from scanning
        for (auto &exclude : excludeList)
        {
            if (m_caseSensitiveSystem ? qstrncmp(exclude, filePath, exclude.length()) == 0 : qstrnicmp(exclude, filePath, exclude.length()) == 0)
            {
                continue;
            }
        }

        // If a file is already in our database
        if (folder.files.contains(fileHash))
        {
            File &file = folder.files[fileHash];
            QDateTime fileDate(fileInfo.lastModified());
            bool updated = file.updated;

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

            // Sets updated flag if the last modified date of a file differs with a new one
            if (type == File::folder)
            {
                updated = (file.date < fileDate);

                // Marks all parent folders as updated if the current folder was updated
                if (updated)
                {
                    QByteArray folderPath(fileInfo.filePath().toUtf8());

                    while (folderPath.remove(folderPath.lastIndexOf("/"), folderPath.length()).length() > folder.path.length())
                    {
                        hash64_t hash = hash64(QByteArray(folderPath).remove(0, folder.path.size()));

                        if (folder.files.value(hash).updated)
                            break;

                        folder.files[hash].updated = true;
                    }
                }
            }
            else if (type == File::file && file.date != fileDate)
            {
                updated = true;
            }

            file.date = fileDate;
            file.size = fileInfo.size();
            file.type = type;
            file.exists = true;
            file.updated = updated;
        }
        else
        {
            File *file = const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, fileInfo.lastModified())).operator->());
            file->path.squeeze();
            file->size = fileInfo.size();
            file->newlyAdded = true;
        }

        if (type == File::file)
            folder.sizeList[fileHash] = fileInfo.size();

        totalNumOfFiles++;

        if (m_shouldQuit || folder.toBeRemoved)
            return -1;
    }
#elif defined(USE_STD_FILESYSTEM)
#error FIX: An update required
    for (auto const &dir : std::filesystem::recursive_directory_iterator{std::filesystem::path{folder.path.toStdString()}})
    {
        if (folder.paused)
            return -1;

        QString filePath(dir.path().string().c_str());
        filePath.remove(0, folder.path.size());
        filePath.replace("\\", "/");

        File::Type type = dir.is_directory() ? File::dir : File::file;
        hash_t fileHash = hash64(filePath);

        const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, QDateTime(), false)).operator->())->path.squeeze();
        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved)
            return -1;
    }
#endif

    m_usedDevices.remove(hash64(QStorageInfo(folder.path).device()));

    return totalNumOfFiles;
}

/*
===================
SyncManager::checkForRenamedFolders

Detects only changes in the name case of folders
===================
*/
void SyncManager::checkForRenamedFolders(SyncProfile &profile)
{
    if (m_caseSensitiveSystem)
        return;

    SET_TIME(startTime);

    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
        {
            // Only a newly added folder can indicate that the case of folder name was changed
            if (fileIt->type != File::folder || !fileIt->newlyAdded || !fileIt->exists)
                continue;

            // Skips if the folder is already scheduled to be moved, especially when there are three or more sync folders
            if (fileIt->toBeMoved)
                continue;

            // Aborts if the folder doesn't exist in any other sync folder
            bool abort = false;

            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                QString otherPath(otherFolderIt->path);
                otherPath.append(fileIt->path);
                QFileInfo otherFileInfo(otherPath);

                if (!otherFileInfo.exists())
                {
                    abort = true;
                    break;
                }
            }

            if (abort) continue;

            // Adds folders from other sync folders for renaming
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                QByteArray otherPath(otherFolderIt->path);
                otherPath.append(fileIt->path);
                QFileInfo otherFileInfo(otherPath);
                QByteArray otherCurrentFilename;
                QByteArray otherCurrentPath;

                // Gets the current filename and path of the folder in another sync folder on a disk
                // The only way to find out this is to use QDirIterator
                QDirIterator otherDirIterator(otherFileInfo.absolutePath(), {otherFileInfo.fileName()}, QDir::Dirs);

                if (otherDirIterator.hasNext())
                {
                    otherDirIterator.next();
                    otherCurrentFilename = otherDirIterator.fileName().toUtf8();
                    otherCurrentPath = otherDirIterator.filePath().toUtf8();
                    otherCurrentPath.remove(0, otherFolderIt->path.size());
                }

                QByteArray fileName(fileIt->path);
                fileName.remove(0, fileName.lastIndexOf("/") + 1);

                // Both folder names should differ in case
                if (otherCurrentFilename.compare(fileName, Qt::CaseSensitive) == 0)
                    continue;

                hash64_t otherFileHash = hash64(otherCurrentPath);

                // Skips if the folder in another sync folder is already in the renaming list
                if (otherFolderIt->foldersToRename.contains(otherFileHash))
                    continue;

                // Finally, adds the folder from another sync folder to the renaming list
                QPair<QByteArray, QByteArray> pair(fileIt->path, otherDirIterator.filePath().toUtf8());
                otherFolderIt->foldersToRename.insert(otherFileHash, pair);
                fileIt->moved = true;
                otherFolderIt->files[otherFileHash].toBeMoved = true;

                // Marks all subdirectories of the renamed folder in our sync folder as moved
                QString folderPath(folderIt->path);
                folderPath.append(fileIt->path);
                QDirIterator dirIterator(folderPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();
                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].moved = true;
                }

                // Marks all subdirectories of the folder that doesn't exist anymore in our sync folder as moved using the path from another sync folder
                // ..

                // Marks all subdirectories of the current folder in another sync folder as to be moved
                QByteArray oldPath(otherFolderIt->path);
                oldPath.append(otherCurrentPath);
                QDirIterator oldDirIterator(oldPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (oldDirIterator.hasNext())
                {
                    oldDirIterator.next();
                    QByteArray path(oldDirIterator.filePath().toUtf8());
                    path.remove(0, otherFolderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].toBeMoved = true;
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
        QHash<hash64_t, qint64> missingFiles;

        // Finds files that don't exist in a sync folder anymore
        for (QHash<hash64_t, qint64>::iterator fileIt = folderIt->sizeList.begin(); fileIt != folderIt->sizeList.end(); ++fileIt)
            if (!folderIt->files.value(fileIt.key()).exists)
                missingFiles.insert(fileIt.key(), fileIt.value());

        for (QHash<hash64_t, File>::iterator newFileIt = folderIt->files.begin(); newFileIt != folderIt->files.end(); ++newFileIt)
        {
            bool abort = false;

            // Only a newly added file can be renamed or moved
            if (newFileIt->type != File::file || !newFileIt->newlyAdded || !newFileIt->exists)
                continue;

            // Other sync folders should not contain the newly added file
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                if (folderIt != otherFolderIt && otherFolderIt->files.contains(newFileIt.key()))
                    abort = true;

            if (abort) continue;

            int matches = 0;
            File *matchedFile;
            hash64_t matchedHash;

            hash64_t hash = hash64(newFileIt->path);
            const File &file = folderIt->files.value(hash);

            // Searches for potential matches
            for (QHash<hash64_t, qint64>::iterator missingFileIt = missingFiles.begin(); missingFileIt != missingFiles.end(); ++missingFileIt)
            {
                // Compares the sizes of possibly moved/renamed files with the size of a newly added file
                if (missingFileIt.value() != newFileIt.value().size)
                    continue;

                // A potential match should have the same modified date as the moved/renamed file
                if (file.date == newFileIt->date)
                {
                    matches++;
                    matchedFile = &folderIt->files[missingFileIt.key()];
                    matchedHash = missingFileIt.key();

                    if (matches > 1) break;
                }
            }

            // Aborts if there are multiple matches, as it cannot definitively detect which specific file was moved/renamed
            if (matches != 1)
                continue;

            // Pre checks
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt || !otherFolderIt->files.contains(matchedHash))
                    continue;

                QString path(otherFolderIt->path);
                path.append(newFileIt->path);

                const File &otherFile = otherFolderIt->files.value(matchedHash);

                // Aborts if other sync folders have at least a file at the destination location
                if ((QFileInfo::exists(path) && newFileIt->path.compare(otherFile.path, Qt::CaseInsensitive) != 0) || otherFolderIt->files.contains(newFileIt.key()))
                {
                    abort = true;
                    break;
                }

                // Aborts the move operation if files have different sizes and dates
#ifdef Q_OS_LINUX
                if (otherFolderIt->sizeList.value(matchedHash) != folderIt->sizeList.value(matchedHash))
#else
                if (otherFolderIt->sizeList.value(matchedHash) != folderIt->sizeList.value(matchedHash) || otherFile.date != newFileIt->date)
#endif
                {
                    abort = true;
                    break;
                }
            }

            if (abort) continue;

            // Adds a moved/renamed file for moving
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt || !otherFolderIt->files.contains(matchedHash))
                    continue;

                const File &otherFile = otherFolderIt->files.value(matchedHash);

                if (!otherFile.exists)
                    continue;

                // Marks a file as moved, that prevents it from being added to other sync folders
                if (!otherFolderIt->files.contains(hash))
                    otherFolderIt->files[hash].moved = true;

                QByteArray from(otherFolderIt->path);
                from.append(otherFile.path);

                otherFolderIt->files[matchedHash].moved = true;
                otherFolderIt->filesToMove.insert(matchedHash, QPair<QByteArray, QByteArray>(newFileIt->path, from));

                matchedFile->moved = true;
                matchedFile->toBeMoved = true;
            }
        }
    }

    TIMESTAMP(startTime, "Checked for moved/renamed files.");
}

/*
===================
SyncManager::checkForAddedFiles
===================
*/
void SyncManager::checkForAddedFiles(SyncProfile &profile)
{
    SET_TIME(startTime);

    // Checks for added/modified files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists) continue;
            if (folderIt->paused) break;

            for (QHash<hash64_t, File>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (otherFolderIt->paused) break;
                if (!otherFileIt.value().exists) continue;

                const File &file = folderIt->files.value(otherFileIt.key());
                const File &otherFile = otherFileIt.value();

                if (file.moved || otherFile.moved || otherFile.toBeMoved) continue;

                bool alreadyAdded = folderIt->filesToAdd.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToAdd.value(otherFileIt.key()).second < otherFile.date;

                // Removes a file path from the "to remove" list if a file was updated
                if (otherFile.type == File::file)
                {
                    if (otherFile.updated)
                        otherFolderIt->filesToRemove.remove(otherFileIt.key());

                    if (file.updated || (otherFile.exists && folderIt->filesToRemove.contains(otherFileIt.key()) && !otherFolderIt->filesToRemove.contains(otherFileIt.key())))
                        folderIt->filesToRemove.remove(otherFileIt.key());
                }
                else if (otherFile.type == File::folder)
                {
                    if (otherFile.updated)
                        otherFolderIt->foldersToRemove.remove(otherFileIt.key());

                    if (file.updated || (otherFile.exists && folderIt->foldersToRemove.contains(otherFileIt.key()) && !otherFolderIt->foldersToRemove.contains(otherFileIt.key())))
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                }

                if ((!folderIt->files.contains(otherFileIt.key()) ||
                // Or if we have a newer version of a file from other folders
 #ifdef Q_OS_LINUX
                ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists &&
                 (((!file.updated && otherFile.updated) || (file.updated && otherFile.updated && file.date < otherFile.date)))) ||
 #else
                ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists &&
                 (((!file.updated && otherFile.updated) || (file.updated == otherFile.updated && file.date < otherFile.date)))) ||
 #endif
                // Or if other folders has a new version of a file and our file was removed
                (!file.exists && (otherFile.updated || otherFolderIt->files.value(hash64(QByteArray(otherFile.path).remove(QByteArray(otherFile.path).indexOf('/', 0), 999999))).updated))) &&
                // Checks for the newest version of a file in case if we have three folders or more
                (!alreadyAdded || hasNewer) &&
                // Checks whether we supposed to remove this file or not
                ((otherFile.type == File::file && !otherFolderIt->filesToRemove.contains(otherFileIt.key())) ||
                 (otherFile.type == File::folder && !otherFolderIt->foldersToRemove.contains(otherFileIt.key()))))
                {
                    if (otherFile.type == File::folder)
                    {
                        const_cast<QByteArray *>(folderIt->foldersToAdd.insert(otherFileIt.key(), otherFile.path).operator->())->squeeze();
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                    }
                    else
                    {
                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);
                        QPair<QPair<QByteArray, QByteArray>, QDateTime> pair(QPair<QByteArray, QByteArray>(otherFile.path, from), otherFile.date);
                        const auto &it = folderIt->filesToAdd.insert(otherFileIt.key(), pair);
                        const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.first.squeeze();
                        const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.second.squeeze();
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
        if (!folderIt->exists) continue;

        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (folderIt->paused) break;

            // This file was moved and should be removed from the file datebase
            if (fileIt.value().toBeMoved)
            {
                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
                continue;
            }

            if (fileIt.value().moved)
            {
                ++fileIt;
                continue;
            }

            if (!fileIt.value().exists && !folderIt->filesToMove.contains(fileIt.key()) &&
                !folderIt->foldersToAdd.contains(fileIt.key()) && !folderIt->filesToAdd.contains(fileIt.key()) &&
               ((fileIt.value().type == File::file && !folderIt->filesToRemove.contains(fileIt.key())) ||
               (fileIt.value().type == File::folder && !folderIt->foldersToRemove.contains(fileIt.key()))))
            {
                // Aborts if a removed file still exists, but with a different case
                if (!m_caseSensitiveSystem)
                {
                    bool abort = false;

                    // As a removed file doesn't have a file path anymore, we need to construct a file path using other files in other folders
                    for (auto removeIt = profile.folders.begin(); removeIt != profile.folders.end(); ++removeIt)
                    {
                        if (folderIt == removeIt || !removeIt->exists || removeIt->paused) continue;

                        const File &fileToRemove = removeIt->files.value(fileIt.key());

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
                for (auto removeIt = profile.folders.begin(); removeIt != profile.folders.end(); ++removeIt)
                {
                    if (folderIt == removeIt || !removeIt->exists || removeIt->paused) continue;

                    const File &fileToRemove = removeIt->files.value(fileIt.key());

                    if (fileToRemove.exists)
                    {
                        if (fileIt.value().type == File::folder)
                            const_cast<QByteArray *>(removeIt->foldersToRemove.insert(fileIt.key(), fileToRemove.path).operator->())->squeeze();
                        else
                            const_cast<QByteArray *>(removeIt->filesToRemove.insert(fileIt.key(), fileToRemove.path).operator->())->squeeze();
                    }
                    else
                    {
                        removeIt->files.remove(fileIt.key());
                    }
                }

                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
            }
            else
            {
                ++fileIt;
            }
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
    if ((m_syncingMode == Automatic && profile.paused) || profile.folders.size() < 2)
        return;

    if (m_detectMovedFiles)
    {
        checkForRenamedFolders(profile);
        checkForMovedFiles(profile);
    }

    checkForAddedFiles(profile);
    checkForRemovedFiles(profile);
}

/*
===================
SyncManager::syncFiles
===================
*/
void SyncManager::syncFiles(SyncProfile &profile)
{
    for (auto &folder : profile.folders)
    {
        QString rootPath = QStorageInfo(folder.path).rootPath();
        bool shouldNotify = m_notificationList.contains(rootPath) ? !m_notificationList.value(rootPath)->isActive() : true;

        QString timeStampFolder(folder.path);
        timeStampFolder.remove(timeStampFolder.lastIndexOf("/", 1), timeStampFolder.size());
        timeStampFolder.append("_");
        timeStampFolder.append(m_versionFolder);
        timeStampFolder.append("/");
        timeStampFolder.append(QDateTime::currentDateTime().toString(m_versionPattern));
        timeStampFolder.append("/");

        // Sorts the folders for removal from the top to the bottom.
        // This ensures that the trash folder has the exact same folder structure as in the original destination.
        QVector<QString> sortedFoldersToRemove;
        sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
        for (const auto &str : qAsConst(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
        std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

        // Folders to rename
        for (auto it = folder.foldersToRename.begin(); it != folder.foldersToRename.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            // Removes from the "files to move" list if the source file doesn't exist
            if (!QFileInfo::exists(it.value().second))
            {
                it = folder.foldersToRename.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                continue;
            }

            QString filePath(folder.path);
            filePath.append(it.value().first);
            hash64_t fileHash = hash64(it.value().first);

            if (QDir().rename(it.value().second, filePath))
            {
                QString parentFrom = QFileInfo(filePath).path();
                QString parentTo = QFileInfo(it.value().second).path();

                if (QFileInfo::exists(parentFrom)) foldersToUpdate.insert(parentFrom.toUtf8());
                if (QFileInfo::exists(parentTo)) foldersToUpdate.insert(parentTo.toUtf8());

                folder.files.remove(hash64(QByteArray(it.value().second).remove(0, folder.path.size())));
                folder.files.insert(fileHash, File(it.value().first, File::folder, QFileInfo(filePath).lastModified()));
                it = folder.foldersToRename.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));

                // Unsets moved flag from all subdirectories
                QDirIterator dirIterator(filePath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();
                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folder.path.size());
                    hash64_t hash = hash64(path);

                    if (folder.files.contains(hash))
                        folder.files[hash].moved = false;
                }
            }
            else
            {
                ++it;
            }
        }

        // Files to move
        for (auto it = folder.filesToMove.begin(); it != folder.filesToMove.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            // Removes from the "files to move" list if the source file doesn't exist
            if (!QFileInfo::exists(it.value().second))
            {
                it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                continue;
            }

            QString filePath(folder.path);
            filePath.append(it.value().first);
            hash64_t fileHash = hash64(it.value().first);

            createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

            if (QFile::rename(it.value().second, filePath))
            {
                QString parentFrom = QFileInfo(filePath).path();
                QString parentTo = QFileInfo(it.value().second).path();

                if (QFileInfo::exists(parentFrom)) foldersToUpdate.insert(parentFrom.toUtf8());
                if (QFileInfo::exists(parentTo)) foldersToUpdate.insert(parentTo.toUtf8());

                hash64_t oldHash = hash64(QByteArray(it.value().second).remove(0, folder.path.size()));

                folder.files.remove(oldHash);
                folder.files.insert(fileHash, File(it.value().first, File::file, QFileInfo(filePath).lastModified()));
                folder.sizeList.remove(oldHash);
                folder.sizeList[fileHash] = QFileInfo(filePath).size();
                it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
            }
            else
            {
                ++it;
            }
        }

        // Folders to remove
        for (auto it = sortedFoldersToRemove.begin(); it != sortedFoldersToRemove.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            QString folderPath(folder.path);
            folderPath.append(*it);
            hash64_t fileHash = hash64(it->toUtf8());
            QString parentPath = QFileInfo(folderPath).path();

            bool success;

            if (m_deletionMode == MoveToTrash)
            {
                // Used to make sure that moveToTrash function really moved a folder
                // to the trash as it can return true even though it failed to do so
                QString pathInTrash;

                success = QFile::moveToTrash(folderPath) && !pathInTrash.isEmpty();
            }
            else if (m_deletionMode == Versioning)
            {
                QString newLocation(timeStampFolder);
                newLocation.append(*it);

                createParentFolders(folder, QDir::cleanPath(newLocation).toUtf8());
                success = QDir().rename(folderPath, newLocation);
            }
            else
            {
                success = QDir(folderPath).removeRecursively();
            }

            if (success || !QDir().exists(folderPath))
            {
                folder.files.remove(fileHash);
                folder.foldersToRemove.remove(fileHash);
                it = sortedFoldersToRemove.erase(static_cast<QVector<QString>::const_iterator>(it));

                if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath.toUtf8());
            }
            else
            {
                ++it;
            }
        }

        // Files to remove
        for (auto it = folder.filesToRemove.begin(); it != folder.filesToRemove.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            QString filePath(folder.path);
            filePath.append(*it);
            hash64_t fileHash = hash64(*it);
            QString parentPath = QFileInfo(filePath).path();

            bool success;

            if (m_deletionMode == MoveToTrash)
            {
                // Used to make sure that moveToTrash function really moved a folder
                // to the trash as it can return true even though it failed to do so
                QString pathInTrash;

                success = QFile::moveToTrash(filePath, &pathInTrash) && !pathInTrash.isEmpty();
            }
            else if (m_deletionMode == Versioning)
            {
                QString newLocation(timeStampFolder);
                newLocation.append(*it);

                createParentFolders(folder, QDir::cleanPath(newLocation).toUtf8());
                success = QFile().rename(filePath, newLocation);
            }
            else
            {
                success = QFile::remove(filePath) || !QFileInfo::exists(filePath);
            }

            if (success || !QFile().exists(filePath))
            {
                folder.files.remove(fileHash);
                folder.sizeList.remove(fileHash);

                it = folder.filesToRemove.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(it));

                if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath.toUtf8());
            }
            else
            {
                ++it;
            }
        }

        // Folders to add
        for (auto it = folder.foldersToAdd.begin(); it != folder.foldersToAdd.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            QString folderPath(folder.path);
            folderPath.append(*it);
            hash64_t fileHash = hash64(*it);
            QFileInfo fileInfo(folderPath);

            createParentFolders(folder, QDir::cleanPath(folderPath).toUtf8());

            // Removes a file with the same filename first if exists
            if (fileInfo.exists() && fileInfo.isFile())
            {
                if (m_deletionMode == MoveToTrash)
                {
                    QFile::moveToTrash(folderPath);
                }
                else if (m_deletionMode == Versioning)
                {
                    QString newLocation(timeStampFolder);
                    newLocation.append(*it);

                    createParentFolders(folder, QDir::cleanPath(newLocation).toUtf8());
                    QFile::rename(folderPath, newLocation);
                }
                else
                {
                    QFile::remove(folderPath);
                }
            }

            if (QDir().mkdir(folderPath) || fileInfo.exists())
            {
                folder.files.insert(fileHash, File(*it, File::folder, fileInfo.lastModified()));
                it = folder.foldersToAdd.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(it));

                QString parentPath = fileInfo.path();
                if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath.toUtf8());
            }
            else
            {
                ++it;
            }
        }

        // Files to copy
        for (auto it = folder.filesToAdd.begin(); it != folder.filesToAdd.end() && (!m_paused && !folder.paused);)
        {
            // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
            if (m_shouldQuit && m_rememberFiles) break;

            // Removes from the "files to add" list if the source file doesn't exist
            if (!QFileInfo::exists(it.value().first.second) || it.value().first.first.isEmpty() || it.value().first.second.isEmpty())
            {
                it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                continue;
            }

            QString filePath(folder.path);
            filePath.append(it.value().first.first);
            hash64_t fileHash = hash64(it.value().first.first);
            const File &file = folder.files.value(fileHash);
            QFileInfo destination(filePath);

            // Destination file is a newly added file
            if (file.type == File::unknown && destination.exists())
            {
                QFileInfo origin(it.value().first.second);

                // Aborts the copy operation if the origin file is older than the destination file
                if (destination.lastModified() > origin.lastModified())
                {
                    it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                    continue;
                }

                // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
                // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
                if (!m_caseSensitiveSystem)
                {
                    QDirIterator originIterator(origin.absolutePath(), {origin.fileName()}, QDir::Files);

                    // Using QDirIterator is the only way to find out the current filename on a disk
                    if (originIterator.hasNext())
                    {
                        originIterator.next();

                        QString fileName = it.value().first.second;
                        fileName.remove(0, fileName.lastIndexOf("/") + 1);

                        // Aborts the copy operation if the origin path and the path on a disk have different cases
                        if (originIterator.fileName().compare(fileName, Qt::CaseSensitive) != 0)
                        {
                            it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                            continue;
                        }
                    }
                }
            }

            createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

            // Removes a file with the same filename first if it exists
            if (destination.exists())
            {
                if (m_deletionMode == MoveToTrash)
                {
                    QFile::moveToTrash(filePath);
                }
                else if (m_deletionMode == Versioning)
                {
                    QString newLocation(timeStampFolder);
                    newLocation.append(it.value().first.first);

                    createParentFolders(folder, QDir::cleanPath(newLocation).toUtf8());
                    QFile::rename(filePath, newLocation);
                }
                else
                {
                    if (file.type == File::folder)
                        QDir(filePath).removeRecursively();
                    else
                        QFile::remove(filePath);
                }
            }

            if (QFile::copy(it.value().first.second, filePath))
            {
                // Do not touch QFileInfo(filePath).lastModified()), as we want to get the latest modified date
                folder.files.insert(fileHash, File(it.value().first.first, File::file, QFileInfo(filePath).lastModified()));
                folder.sizeList[fileHash] = QFileInfo(filePath).size();
                it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));

                QString parentPath = destination.path();
                if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath.toUtf8());
            }
            else
            {
                // Not enough disk space notification
                if (m_notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(it.value().first.second).size())
                {
                    if (!m_notificationList.contains(rootPath))
                        m_notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                    shouldNotify = false;
                    m_notificationList.value(rootPath)->start(NOTIFICATION_DELAY);
                    emit warning(QString("Not enough disk space on %1 (%2)").arg(QStorageInfo(folder.path).displayName(), rootPath), "");
                }

                ++it;
            }
        }

        // Updates the modified date of the parent folders as adding/removing files and folders change their modified date
        for (auto folderIt = foldersToUpdate.begin(); folderIt != foldersToUpdate.end();)
        {
            hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, folder.path.size()));

            if (folder.files.contains(folderHash))
            {
                folder.files[folderHash].date = QFileInfo(*folderIt).lastModified();
                folderIt = foldersToUpdate.erase(static_cast<QSet<QByteArray>::const_iterator>(folderIt));
            }
            else
            {
                folderIt++;
            }
        }
    }
}
