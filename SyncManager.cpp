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
removeDuplicateFiles

Removes duplicates from the list of files by file size and modified date
===================
*/
static void removeDuplicateFiles(QHash<hash64_t, File *> &files)
{
    for (QHash<hash64_t, File *>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        bool dup = false;

        for (QHash<hash64_t, File *>::iterator anotherFileIt = files.begin(); anotherFileIt != files.end();)
        {
            bool anotherDup = true;

            if (fileIt.key() == anotherFileIt.key())
            {
                ++anotherFileIt;
                continue;
            }

            if (fileIt.value()->size != anotherFileIt.value()->size)
                anotherDup = false;

#ifndef Q_OS_LINUX
            if (fileIt.value()->date != anotherFileIt.value()->date)
                anotherDup = false;
#endif

            if (anotherDup)
            {
                dup = true;
                anotherFileIt = files.erase(static_cast<QHash<hash64_t, File *>::const_iterator>(anotherFileIt));
                continue;
            }

            ++anotherFileIt;
        }

        if (dup)
            fileIt = files.erase(static_cast<QHash<hash64_t, File *>::const_iterator>(fileIt));
        else
            ++fileIt;
    }
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
    if (m_rememberFiles)
        saveData();
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

            if (m_busy && folder.exists && !folder.paused)
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
            if (folder.exists && !folder.paused)
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

    if (time < SYNC_MIN_DELAY)
        time = SYNC_MIN_DELAY;

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

            for (auto fileIt = folder.files.begin(); fileIt != folder.files.end(); fileIt++)
            {
                stream << fileIt.key();
                stream << fileIt->date;
                stream << fileIt->size;
                stream << fileIt->type;
                stream << fileIt->updated;
                stream << fileIt->exists;
            }

            // Folders to rename
            stream << folder.foldersToRename.size();

            for (const auto &fileIt : folder.foldersToRename)
            {
                stream << fileIt.first;
                stream << fileIt.second;
            }

            // Files to move
            stream << folder.filesToMove.size();

            for (const auto &fileIt : folder.filesToMove)
            {
                stream << fileIt.first;
                stream << fileIt.second;
            }

            // Folders to create
            stream << folder.foldersToCreate.size();

            for (const auto &path : folder.foldersToCreate)
                stream << path;

            // Files to create
            stream << folder.filesToCopy.size();

            for (const auto &fileIt : folder.filesToCopy)
            {
                stream << fileIt.first;
                stream << fileIt.second;
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
            QByteArray folderPath;
            qsizetype numOfFiles;

            stream >> folderPath;
            stream >> numOfFiles;

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
            for (qsizetype k = 0; k < numOfFiles; k++)
            {
                hash64_t hash;
                QDateTime date;
                qint64 size;
                File::Type type;
                bool updated;
                bool exists;

                stream >> hash;
                stream >> date;
                stream >> size;
                stream >> type;
                stream >> updated;
                stream >> exists;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].files.insert(hash, File(QByteArray(), type, date, updated, exists, true));
                    it->size = size;
                    it->path.squeeze();
                }
            }

            // Folders to rename
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
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
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
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

            // Folders to create
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const auto it = m_profiles[profileIndex].folders[folderIndex].foldersToCreate.insert(hash64(path), path);
                    it->squeeze();
                }
            }

            // Files to copy
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
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
                    const auto it = m_profiles[profileIndex].folders[folderIndex].filesToCopy.insert(hash64(to), pair);
                    it.value().first.first.squeeze();
                    it.value().first.second.squeeze();
                }
            }

            // Folders to remove
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
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
            stream >> numOfFiles;

            for (qsizetype k = 0; k < numOfFiles; k++)
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
SyncManager::syncProfile
===================
*/
bool SyncManager::syncProfile(SyncProfile &profile)
{
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

                for (auto futureIt = futureList.begin(); futureIt != futureList.end();)
                {
                    if (!m_usedDevices.contains(futureIt->first))
                    {
                        m_usedDevices.insert(futureIt->first);
                        futureIt->second.resume();
                    }

                    if (futureIt->second.isFinished())
                    {
                        result += futureIt->second.result();
                        m_usedDevices.remove(futureIt->first);
                        futureIt = futureList.erase(static_cast<QList<QPair<hash64_t, QFuture<int>>>::const_iterator>(futureIt));
                    }
                    else
                    {
                        futureIt++;
                    }

                    i++;
                }

                if (m_shouldQuit)
                    return false;
            }

            TIMESTAMP(startTime, "Found %d files in %s.", result, qUtf8Printable(profile.name));

            checkForChanges(profile);

            bool countAverage = profile.syncTime ? true : false;
            profile.syncTime += timer.elapsed();
            if (countAverage) profile.syncTime /= 2;

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

        if (m_syncing)
            syncFiles(profile);
    }

    // Optimizes memory usage
    for (auto &folder : profile.folders)
    {
        folder.files.squeeze();
        folder.filesToMove.squeeze();
        folder.foldersToCreate.squeeze();
        folder.filesToCopy.squeeze();
        folder.foldersToRemove.squeeze();
        folder.filesToRemove.squeeze();

        for (auto &file : folder.files)
            file.path.clear();
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
                file.locked = false;
    }

    // Last sync date update
    profile.lastSyncDate = QDateTime::currentDateTime();

    for (auto &folder : profile.folders)
        if (!folder.paused)
            folder.lastSyncDate = QDateTime::currentDateTime();

    emit profileSynced(&profile);

    updateStatus();
    updateNextSyncingTime();

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

    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (folder.paused)
            return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        QByteArray filePath(fileInfo.filePath().toUtf8());
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

        File::Type type = fileInfo.isDir() ? File::folder : File::file;
        hash64_t fileHash = hash64(filePath);

        // If a file is already in our database
        if (folder.files.contains(fileHash))
        {
            File &file = folder.files[fileHash];
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
                file.updated = true;

            // Marks all parent folders as updated if the current folder was updated
            if (file.updated)
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

            file.date = fileDate;
            file.size = fileInfo.size();
            file.type = type;
            file.exists = true;
        }
        else
        {
            File *file = folder.files.insert(fileHash, File(filePath, type, fileInfo.lastModified())).operator->();
            file->size = fileInfo.size();
            file->newlyAdded = true;
            file->path.squeeze();
        }

        totalNumOfFiles++;

        if (m_shouldQuit || folder.toBeRemoved)
            return -1;
    }

    m_usedDevices.remove(hash64(QStorageInfo(folder.path).device()));

    return totalNumOfFiles;
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
        if (folderIt->paused || folderIt->toBeRemoved)
            continue;

        for (QHash<hash64_t, File>::iterator renamedFolderIt = folderIt->files.begin(); renamedFolderIt != folderIt->files.end(); ++renamedFolderIt)
        {
            // Only a newly added folder can indicate that the case of folder name was changed
            if (renamedFolderIt->type != File::folder || !renamedFolderIt->newlyAdded || !renamedFolderIt->exists)
                continue;

            // Skips if the folder is already scheduled to be moved, especially when there are three or more sync folders
            if (renamedFolderIt->locked)
                continue;

            bool abort = false;

            // Aborts if the folder doesn't exist in any other sync folder
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (otherFolderIt->paused || otherFolderIt->toBeRemoved)
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

            if (abort) continue;

            // Adds folders from other sync folders for renaming
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (otherFolderIt->paused || otherFolderIt->toBeRemoved)
                    continue;

                QByteArray otherFolderPath(otherFolderIt->path);
                otherFolderPath.append(renamedFolderIt->path);
                QFileInfo otherFileInfo(otherFolderPath);
                QByteArray otherCurrentFolderName;
                QByteArray otherCurrentFolderPath;

                // Gets the current folder name and path in another sync folder on a disk
                // The only way to find out this is to use QDirIterator
                QDirIterator otherFolderIterator(otherFileInfo.absolutePath(), {otherFileInfo.fileName()}, QDir::Dirs);

                if (otherFolderIterator.hasNext())
                {
                    otherFolderIterator.next();
                    otherCurrentFolderName = otherFolderIterator.fileName().toUtf8();
                    otherCurrentFolderPath = otherFolderIterator.filePath().toUtf8();
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

                // Finally, adds the folder from another sync folder to the renaming list
                QPair<QByteArray, QByteArray> pair(renamedFolderIt->path, otherFolderIterator.filePath().toUtf8());
                otherFolderIt->foldersToRename.insert(otherFolderHash, pair);

                renamedFolderIt->locked = true;
                otherFolderIt->files[otherFolderHash].locked = true;

                // Marks all subdirectories of the renamed folder in our sync folder as locked
                QString folderPath(folderIt->path);
                folderPath.append(renamedFolderIt->path);
                QDirIterator dirIterator(folderPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();

                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].locked = true;
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
                        folderIt->files[hash].toBeRemoved = true;
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
                        folderIt->files[hash].locked = true;
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
        if (folderIt->paused || folderIt->toBeRemoved)
            continue;

        QHash<hash64_t, File *> missingFiles;
        QHash<hash64_t, File *> newFiles;

        // Finds files that don't exist in our sync folder anymore
        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
            if (fileIt.value().type == File::file && !fileIt.value().exists && fileIt->size >= m_movedFileMinSize)
                missingFiles.insert(fileIt.key(), &fileIt.value());

        removeDuplicateFiles(missingFiles);

        if (missingFiles.isEmpty())
            continue;

        // Finds files that are new in our sync folder
        for (QHash<hash64_t, File>::iterator newFileIt = folderIt->files.begin(); newFileIt != folderIt->files.end(); ++newFileIt)
            if (newFileIt->type == File::file && newFileIt->newlyAdded && newFileIt->exists && newFileIt->size >= m_movedFileMinSize)
                newFiles.insert(newFileIt.key(), &newFileIt.value());

        removeDuplicateFiles(newFiles);

        for (QHash<hash64_t, File *>::iterator newFileIt = newFiles.begin(); newFileIt != newFiles.end(); ++newFileIt)
        {
            bool abort = false;
            File *movedFile = nullptr;
            hash64_t movedFileHash;

            // Searches for a match between missed file and a newly added file
            for (QHash<hash64_t, File *>::iterator missingFileIt = missingFiles.begin(); missingFileIt != missingFiles.end(); ++missingFileIt)
            {
                if (missingFileIt.value()->size != newFileIt.value()->size)
                    continue;

#ifndef Q_OS_LINUX
                if (missingFileIt.value()->date != newFileIt.value()->date)
                    continue;
#endif

                movedFile = &folderIt->files[missingFileIt.key()];
                movedFileHash = missingFileIt.key();
                break;
            }

            if (!movedFile)
                continue;

            // Additional checks
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (otherFolderIt->paused || otherFolderIt->toBeRemoved)
                    continue;

                const File &fileToBeMoved = otherFolderIt->files.value(movedFileHash);

                // Aborts if a file that needs to be moved doesn't exists in any sync folder
                if (!fileToBeMoved.exists)
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
#ifdef Q_OS_LINUX
                if (otherFolderIt->files.value(movedFileHash).size != folderIt->files.value(movedFileHash).size)
#else
                if (otherFolderIt->files.value(movedFileHash).size != folderIt->files.value(movedFileHash).size || fileToBeMoved.date != movedFile->date)
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
                if (folderIt == otherFolderIt)
                    continue;

                if (otherFolderIt->paused || otherFolderIt->toBeRemoved)
                    continue;

                const File &fileToBeMoved = otherFolderIt->files.value(movedFileHash);

                newFileIt.value()->locked = true;
                movedFile->toBeRemoved = true;

                QByteArray pathToMove(otherFolderIt->path);
                pathToMove.append(fileToBeMoved.path);

                otherFolderIt->files[movedFileHash].locked = true;
                otherFolderIt->filesToMove.insert(movedFileHash, QPair<QByteArray, QByteArray>(newFileIt.value()->path, pathToMove));

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
        if (!folderIt->exists)
            continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists)
                continue;

            if (folderIt->paused || folderIt->toBeRemoved)
                break;

            for (QHash<hash64_t, File>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (otherFolderIt->paused || otherFolderIt->toBeRemoved)
                    break;

                if (!otherFileIt.value().exists)
                    continue;

                const File &file = folderIt->files.value(otherFileIt.key());
                const File &otherFile = otherFileIt.value();

                if (file.locked || file.toBeRemoved || otherFile.locked || otherFile.toBeRemoved)
                    continue;

                bool alreadyAdded = folderIt->filesToCopy.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToCopy.value(otherFileIt.key()).second < otherFile.date;

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

                // Checks for the newest version of a file in case if we have three folders or more
                if (alreadyAdded && !hasNewer)
                    continue;

                if ((!folderIt->files.contains(otherFileIt.key()) ||
                // Or if we have a newer version of a file from other folders
                ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists &&
#ifdef Q_OS_LINUX
                 (((!file.updated && otherFile.updated) || (file.updated && otherFile.updated && file.date < otherFile.date)))) ||
#else
                 (((!file.updated && otherFile.updated) || (file.updated == otherFile.updated && file.date < otherFile.date)))) ||
#endif
                // Or if other folders has a new version of a file and our file was removed
                (!file.exists && (otherFile.updated || otherFolderIt->files.value(hash64(QByteArray(otherFile.path).remove(otherFile.path.indexOf('/'), otherFile.path.size()))).updated))))
                {
                    // Aborts if a file is supposed to be removed
                    if (otherFile.type == File::file)
                    {
                        if (otherFolderIt->filesToRemove.contains(otherFileIt.key()))
                            continue;
                    }
                    else if (otherFile.type == File::folder)
                    {
                        if (otherFolderIt->foldersToRemove.contains(otherFileIt.key()))
                            continue;
                    }

                    if (otherFile.type == File::folder)
                    {
                        folderIt->foldersToCreate.insert(otherFileIt.key(), otherFile.path)->squeeze();
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                    }
                    else
                    {
                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);
                        QPair<QPair<QByteArray, QByteArray>, QDateTime> pair(QPair<QByteArray, QByteArray>(otherFile.path, from), otherFile.date);

                        QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator it = folderIt->filesToCopy.insert(otherFileIt.key(), pair);
                        it->first.first.squeeze();
                        it->first.second.squeeze();
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
        if (!folderIt->exists)
            continue;

        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (folderIt->paused || folderIt->toBeRemoved)
                break;

            // A file was moved and should be removed from a file datebase
            if (fileIt->toBeRemoved)
            {
                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
                continue;
            }

            if (fileIt->exists || fileIt->locked)
            {
                ++fileIt;
                continue;
            }

            if (fileIt->type == File::file)
            {
                if (folderIt->filesToMove.contains(fileIt.key()) ||
                    folderIt->filesToCopy.contains(fileIt.key()) ||
                    folderIt->filesToRemove.contains(fileIt.key()))
                {
                    ++fileIt;
                    continue;
                }
            }
            else if (fileIt->type == File::folder)
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
                    if (folderIt == anotherFolderIt || !anotherFolderIt->exists || anotherFolderIt->paused || anotherFolderIt->toBeRemoved)
                        continue;

                    const File &fileToRemove = anotherFolderIt->files.value(fileIt.key());

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
                if (folderIt == anotherFolderIt || !anotherFolderIt->exists || anotherFolderIt->paused || anotherFolderIt->toBeRemoved)
                    continue;

                const File &fileToRemove = anotherFolderIt->files.value(fileIt.key());

                if (fileToRemove.exists)
                {
                    if (fileIt.value().type == File::folder)
                        anotherFolderIt->foldersToRemove.insert(fileIt.key(), fileToRemove.path)->squeeze();
                    else
                        anotherFolderIt->filesToRemove.insert(fileIt.key(), fileToRemove.path)->squeeze();
                }
                else
                {
                    anotherFolderIt->files.remove(fileIt.key());
                }
            }

            fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
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

    // Fixes double syncing of updated files on restart, as these states are no longer needed in the file database after syncing
    for (auto &folder : profile.folders)
    {
        for (auto &file : folder.files)
        {
            file.updated = false;
            file.newlyAdded = false;
        }
    }
}

/*
===================
SyncManager::removeFile
===================
*/
bool SyncManager::removeFile(SyncFolder &folder, const QString &path, const QString &fullPath, const QString &versioningPath, File::Type type)
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
        if (type == File::folder)
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

            folder.files.insert(hash, File(shortPath, File::folder, QFileInfo(foldersToCreate.top()).lastModified()));
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
    for (auto folderIt = folder.foldersToRename.begin(); folderIt != folder.foldersToRename.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        // Removes from the "folders to rename" list if the source file doesn't exist
        if (!QFileInfo::exists(folderIt.value().second))
        {
            folderIt = folder.foldersToRename.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(folderIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(folderIt.value().first);
        hash64_t fileHash = hash64(folderIt.value().first);

        if (QDir().rename(folderIt.value().second, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(folderIt.value().second).path();

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            folder.files.remove(hash64(QByteArray(folderIt.value().second).remove(0, folder.path.size())));
            folder.files.insert(fileHash, File(folderIt.value().first, File::folder, QFileInfo(filePath).lastModified()));
            folderIt = folder.foldersToRename.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(folderIt));
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
    for (auto fileIt = folder.filesToMove.begin(); fileIt != folder.filesToMove.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        // Removes from the "files to move" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().second))
        {
            fileIt = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().first);
        hash64_t fileHash = hash64(fileIt.value().first);

        // Removes from the "files to move" list if a file already exists at the destination location
        if (QFileInfo::exists(filePath))
        {
            // For case-insensitive systems, both paths should not be the same
            if (m_caseSensitiveSystem || fileIt.value().second.compare(filePath.toUtf8(), Qt::CaseInsensitive) != 0)
            {
                fileIt = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(fileIt));
                continue;
            }
        }

        createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

        if (QFile::rename(fileIt.value().second, filePath))
        {
            QString parentFrom = QFileInfo(filePath).path();
            QString parentTo = QFileInfo(fileIt.value().second).path();

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            hash64_t oldHash = hash64(QByteArray(fileIt.value().second).remove(0, folder.path.size()));

            folder.files.remove(oldHash);
            auto it = folder.files.insert(fileHash, File(fileIt.value().first, File::file, QFileInfo(filePath).lastModified()));
            it->size = QFileInfo(filePath).size();
            fileIt = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(fileIt));
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

    for (auto folderIt = sortedFoldersToRemove.begin(); folderIt != sortedFoldersToRemove.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        QString folderPath(folder.path);
        folderPath.append(*folderIt);
        hash64_t fileHash = hash64(folderIt->toUtf8());

        if (removeFile(folder, *folderIt, folderPath, versioningPath, File::folder) || !QDir().exists(folderPath))
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
    for (auto fileIt = folder.filesToRemove.begin(); fileIt != folder.filesToRemove.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        QString filePath(folder.path);
        filePath.append(*fileIt);
        hash64_t fileHash = hash64(*fileIt);

        if (removeFile(folder, *fileIt, filePath, versioningPath, File::file) || !QFile().exists(filePath))
        {
            folder.files.remove(fileHash);
            fileIt = folder.filesToRemove.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(fileIt));

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
    for (auto folderIt = folder.foldersToCreate.begin(); folderIt != folder.foldersToCreate.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        QString folderPath(folder.path);
        folderPath.append(*folderIt);
        hash64_t fileHash = hash64(*folderIt);
        QFileInfo fileInfo(folderPath);

        createParentFolders(folder, QDir::cleanPath(folderPath).toUtf8());

        // Removes a file with the same filename first if exists
        if (fileInfo.exists() && fileInfo.isFile())
            removeFile(folder, *folderIt, folderPath, versioningPath, File::file);

        if (QDir().mkdir(folderPath) || fileInfo.exists())
        {
            folder.files.insert(fileHash, File(*folderIt, File::folder, fileInfo.lastModified()));
            folderIt = folder.foldersToCreate.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(folderIt));

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

    for (auto fileIt = folder.filesToCopy.begin(); fileIt != folder.filesToCopy.end() && (!m_paused && !folder.paused);)
    {
        // Breaks only if "remember files" is enabled, otherwise all made changes will be lost
        if (m_shouldQuit && m_rememberFiles)
            break;

        // Removes from the "files to copy" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt.value().first.second) || fileIt.value().first.first.isEmpty() || fileIt.value().first.second.isEmpty())
        {
            fileIt = folder.filesToCopy.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(fileIt));
            continue;
        }

        QString filePath(folder.path);
        filePath.append(fileIt.value().first.first);
        hash64_t fileHash = hash64(fileIt.value().first.first);
        const File &file = folder.files.value(fileHash);
        QFileInfo destination(filePath);

        // Destination file is a newly added file
        if (file.type == File::unknown && destination.exists())
        {
            QFileInfo origin(fileIt.value().first.second);

            // Aborts the copy operation if the origin file is older than the destination file
            if (destination.lastModified() > origin.lastModified())
            {
                fileIt = folder.filesToCopy.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(fileIt));
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

                    QString fileName = fileIt.value().first.second;
                    fileName.remove(0, fileName.lastIndexOf("/") + 1);

                    // Aborts the copy operation if the origin path and the path on a disk have different cases
                    if (originIterator.fileName().compare(fileName, Qt::CaseSensitive) != 0)
                    {
                        fileIt = folder.filesToCopy.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(fileIt));
                        continue;
                    }
                }
            }
        }

        createParentFolders(folder, QDir::cleanPath(filePath).toUtf8());

        // Removes a file with the same filename first if exists
        if (destination.exists())
            removeFile(folder, fileIt.value().first.first, filePath, versioningPath, file.type);

        if (QFile::copy(fileIt.value().first.second, filePath))
        {
            // Do not touch QFileInfo(filePath).lastModified()), as we want to get the latest modified date
            auto it = folder.files.insert(fileHash, File(fileIt.value().first.first, File::file, QFileInfo(filePath).lastModified()));
            it->size = QFileInfo(filePath).size();
            fileIt = folder.filesToCopy.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(fileIt));

            QString parentPath = destination.path();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            // Not enough disk space notification
            if (m_notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(fileIt.value().first.second).size())
            {
                if (!m_notificationList.contains(rootPath))
                    m_notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                shouldNotify = false;
                m_notificationList.value(rootPath)->start(NOTIFICATION_COOLDOWN);
                emit warning(QString("Not enough disk space on %1 (%2)").arg(QStorageInfo(folder.path).displayName(), rootPath), "");
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
    for (auto &folder : profile.folders)
    {
        if (folder.paused || folder.toBeRemoved)
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
            if (folder.paused || folder.toBeRemoved)
                continue;

            hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, folder.path.size()));

            if (folder.files.contains(folderHash))
                folder.files[folderHash].date = QFileInfo(*folderIt).lastModified();

            folderIt = folder.foldersToUpdate.erase(static_cast<QSet<QByteArray>::const_iterator>(folderIt));
        }
    }
}
