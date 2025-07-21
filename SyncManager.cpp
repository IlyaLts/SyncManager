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
    m_versioningPath = settings.value("VersioningPath", "").toString();
    m_databaseLocation = static_cast<SyncManager::DatabaseLocation>(settings.value("DatabaseLocation", SyncManager::Decentralized).toInt());
    m_detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    m_syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (m_syncTimeMultiplier <= 0) m_syncTimeMultiplier = 1;
    m_fileMinSize = settings.value("FileMinSize", 0).toInt();
    m_fileMaxSize = settings.value("FileMaxSize", 0).toInt();
    m_movedFileMinSize = settings.value("MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt();
    m_includeList = settings.value("IncludeList").toStringList();
    m_excludeList = settings.value("ExcludeList").toStringList();
    m_caseSensitiveSystem = settings.value("caseSensitiveSystem", m_caseSensitiveSystem).toBool();
    m_versioningFolder = settings.value("VersionFolder", "[Deletions]").toString();
    m_versioningPattern = settings.value("VersionPattern", "yyyy_M_d_h_m_s_z").toString();
}

/*
===================
SyncManager::addToQueue
===================
*/
void SyncManager::addToQueue(SyncProfile *profile)
{
    if (m_profiles.empty() || (profile && m_queue.contains(profile)))
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
            profileIt = m_profiles.erase(static_cast<std::list<SyncProfile>::const_iterator>(profileIt));
            continue;
        }

        // Folders
        for (auto folderIt = profileIt->folders.begin(); folderIt != profileIt->folders.end();)
        {
            if (folderIt->toBeRemoved)
                folderIt = profileIt->folders.erase(static_cast<std::list<SyncFolder>::const_iterator>(folderIt));
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
    qDebug() << "=======================================";
    qDebug() << "Started syncing" << qUtf8Printable(profile.name);
    qDebug() << "=======================================";
#endif

    if (m_databaseLocation == Decentralized)
        profile.loadDatebasesDecentralised();
    else
        profile.loadDatabasesLocally();

    // Gets lists of all files in folders
    SET_TIME(startTime);

    int result = 0;
    QList<QPair<Hash, QFuture<int>>> futureList;

    for (auto &folder : profile.folders)
    {
        hash64_t requiredDevice = hash64(QStorageInfo(folder.path).device());
        QPair<Hash, QFuture<int>> pair(requiredDevice, QFuture(QtConcurrent::run([&](){ return scanFiles(profile, folder); })));
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
        qDebug() << "---------------------------------------";
        if (numOfFoldersToRename)
            qDebug() << "Folders to rename:" << numOfFoldersToRename;
        if (numOfFilesToMove)
            qDebug() << "Files to move:" << numOfFilesToMove;
        if (numOfFoldersToCreate)
            qDebug() << "Folders to create:" << numOfFoldersToCreate;
        if (numOffilesToCopy)
            qDebug() << "Files to copy:" << numOffilesToCopy;
        if (numOfFoldersToRemove)
            qDebug() << "Folders to remove:" << numOfFoldersToRemove;
        if (numOfFilesToRemove)
            qDebug() << "Files to remove:" << numOfFilesToRemove;
        qDebug() << "---------------------------------------";
#endif
    }

    updateStatus();

    if (m_shouldQuit)
        return false;

    syncFiles(profile);
    profile.removeInvalidFileData();

    if (profile.resetLocks())
        m_databaseChanged = true;

    if (m_databaseChanged)
    {
        if (m_databaseLocation == Decentralized)
            profile.saveDatabasesDecentralised();
        else
            profile.saveDatabasesLocally();
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
SyncManager::scanFiles
===================
*/
int SyncManager::scanFiles(SyncProfile &profile, SyncFolder &folder)
{
    int totalNumOfFiles = 0;
    QStringList nameFilters(m_includeList);
    nameFilters.removeAll(""); // It's important for proper iteration because the include list may contain empty strings

    if (nameFilters.isEmpty())
        nameFilters.append("*");

    QDirIterator dir(folder.path, nameFilters, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    for (auto &file : folder.files)
        file.flags = 0;

    while (dir.hasNext())
    {
        if (m_shouldQuit || !folder.isActive())
            return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        qint64 fileSize = fileInfo.size();

        // If the file is a symlink, this function returns information about the target, not the symlink
        if (fileInfo.isSymLink())
            fileSize = 0;

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

        if (m_fileMinSize > fileSize)
            continue;

        if (m_fileMaxSize > 0 && fileSize > m_fileMaxSize)
            continue;

        QByteArray absoluteFilePath = fileInfo.filePath().toUtf8();
        QByteArray filePath(absoluteFilePath);
        filePath.remove(0, folder.path.size());

        bool shouldExclude = false;

        // Excludes unwanted files and folder from scanning
        for (QString &exclude : m_excludeList)
        {
            QRegularExpression::PatternOption option = m_caseSensitiveSystem ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
            QRegularExpression re(QRegularExpression::wildcardToRegularExpression(exclude), option);
            re.setPattern(QRegularExpression::anchoredPattern(re.pattern()));

            if (re.match(filePath).hasMatch())
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
            if (file.scanned())
            {
#ifndef DEBUG
                QString title("Hash collision detected!");
                QString text = QString("%1 vs %2").arg(qUtf8Printable(filePath), qUtf8Printable(profile.filePath(fileHash)));
                QMessageBox::critical(nullptr, title, text);
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

            if (file.size != fileSize)
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
            file.size = fileSize;
            file.type = type;
            file.attributes = getFileAttributes(absoluteFilePath);
            file.setExists(true);
            file.setScanned(true);
        }
        // If a file is new
        else
        {
            SyncFile *file = folder.files.insert(fileHash, SyncFile(type, fileInfo.lastModified())).operator->();
            file->size = fileSize;
            file->attributes = getFileAttributes(absoluteFilePath);
            file->setNewlyAdded(true);
            file->setScanned(true);

            m_databaseChanged = true;
        }

        totalNumOfFiles++;
    }

    // Since we only synchronize one-way folders in one direction,,
    // we need to clear all file data because files that
    // no longer exist there don't get removed from the database.
    // This is just the easiest way to make one-way work properly.
    if (folder.syncType == SyncFolder::ONE_WAY)
        folder.removeNonExistentFiles();

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
        if (!folderIt->isActive() || folderIt->syncType == SyncFolder::ONE_WAY || folderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
            continue;

        for (QHash<Hash, SyncFile>::iterator renamedFolderIt = folderIt->files.begin(); renamedFolderIt != folderIt->files.end(); ++renamedFolderIt)
        {
            // Only a newly added folder can indicate that the case of folder name was changed
            if (renamedFolderIt->type != SyncFile::Folder || !renamedFolderIt->newlyAdded() || !renamedFolderIt->exists())
                continue;

            // Skips if the folder is already scheduled to be moved, especially when there are three or more sync folders
            if (renamedFolderIt->lockedFlag == SyncFile::Locked)
                continue;

            QByteArray renamedFolderPath(profile.filePath(renamedFolderIt.key()));
            bool abort = false;

            // Aborts if the folder doesn't exist in any other sync folder
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                QByteArray otherFolderFullPath(otherFolderIt->path);
                otherFolderFullPath.append(renamedFolderPath);

                if (!QFileInfo(otherFolderFullPath).exists())
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

                QByteArray otherFolderFullPath(otherFolderIt->path);
                otherFolderFullPath.append(renamedFolderPath);
                QByteArray otherCurrentFolderName;
                QByteArray otherCurrentFolderPath;

                QByteArray newFolderName(renamedFolderPath);
                newFolderName.remove(0, newFolderName.lastIndexOf("/") + 1);

                QFileInfo otherFolder = getCurrentFileInfo(otherFolderFullPath);

                if (otherFolder.exists())
                {
                    otherCurrentFolderName = otherFolder.fileName().toUtf8();
                    otherCurrentFolderPath = otherFolder.filePath().toUtf8();
                    otherCurrentFolderPath.remove(0, otherFolderIt->path.size());
                }

                // Both folder names should differ in case
                if (otherCurrentFolderName.compare(newFolderName, Qt::CaseSensitive) == 0)
                    continue;

                hash64_t otherFolderHash = hash64(otherCurrentFolderPath);

                // Skips if the folder in another sync folder is already in the renaming list
                if (otherFolderIt->foldersToRename.contains(otherFolderHash))
                    continue;

                QByteArray folderFullPath(folderIt->path);
                folderFullPath.append(renamedFolderPath);

                // Finally, adds the folder from other sync folder to the renaming list
                otherFolderIt->foldersToRename.insert(otherFolderHash, {renamedFolderPath, otherCurrentFolderPath, getFileAttributes(folderFullPath)});

                // Do not reorder these, as it could lead to a crash because sometimes
                // renamedFolderIt and otherFolderHash both lead to the same sync file
                renamedFolderIt->lockedFlag = SyncFile::Locked;
                folderIt->files.remove(otherFolderHash);

                otherFolderIt->files[otherFolderHash].lockedFlag = SyncFile::Locked;

                // Marks all subdirectories of the renamed folder in our sync folder as locked
                QDirIterator dirIterator(folderFullPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();

                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files.contains(hash))
                        folderIt->files[hash].lockedFlag = SyncFile::LockedInternal;
                }

                // Marks all subdirectories of the folder that doesn't exist anymore in our sync folder as to be removed using the path from other sync folder
                QByteArray oldFullPath(folderIt->path);
                oldFullPath.append(otherCurrentFolderPath);
                QDirIterator oldDirIterator(oldFullPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (oldDirIterator.hasNext())
                {
                    oldDirIterator.next();

                    QByteArray path(oldDirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path.size());
                    folderIt->files.remove(hash64(path));
                }

                // Marks all subdirectories of the current folder in other sync folder as to be locked
                QByteArray otherFullPathToRename(otherFolderIt->path);
                otherFullPathToRename.append(otherCurrentFolderPath);
                QDirIterator otherDirIterator(otherFullPathToRename, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (otherDirIterator.hasNext())
                {
                    otherDirIterator.next();

                    QByteArray path(otherDirIterator.filePath().toUtf8());
                    path.remove(0, otherFolderIt->path.size());
                    hash64_t hash = hash64(path);

                    if (otherFolderIt->files.contains(hash))
                        otherFolderIt->files[hash].lockedFlag = SyncFile::LockedInternal;
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
        if (!folderIt->isActive() || folderIt->syncType == SyncFolder::ONE_WAY || folderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
            continue;

        QHash<Hash, SyncFile *> missingFiles;
        QHash<Hash, SyncFile *> newFiles;

        // Finds files that no longer exist in our sync folder
        for (QHash<Hash, SyncFile>::iterator missingFileIt = folderIt->files.begin(); missingFileIt != folderIt->files.end(); ++missingFileIt)
            if (missingFileIt.value().type == SyncFile::File && !missingFileIt.value().exists() && missingFileIt->size >= m_movedFileMinSize)
                missingFiles.insert(missingFileIt.key(), &missingFileIt.value());

        removeDuplicatesBySizeAndDate(missingFiles);

        if (missingFiles.isEmpty())
            continue;

        // Finds files that are new in our sync folder
        for (QHash<Hash, SyncFile>::iterator newFileIt = folderIt->files.begin(); newFileIt != folderIt->files.end(); ++newFileIt)
            if (newFileIt->type == SyncFile::File && newFileIt->newlyAdded() && newFileIt->exists() && newFileIt->size >= m_movedFileMinSize)
                newFiles.insert(newFileIt.key(), &newFileIt.value());

        removeDuplicatesBySizeAndDate(newFiles);

        for (QHash<Hash, SyncFile *>::iterator newFileIt = newFiles.begin(); newFileIt != newFiles.end(); ++newFileIt)
        {
            bool abort = false;
            SyncFile *movedFile = nullptr;
            hash64_t movedFileHash;
            hash64_t newFileHash;
            QByteArray movedFilePath;

            // Searches for a match between a missed file and a newly added file
            for (QHash<Hash, SyncFile *>::iterator missingFileIt = missingFiles.begin(); missingFileIt != missingFiles.end(); ++missingFileIt)
            {
                if (!missingFileIt.value()->hasSameSizeAndDate(*newFileIt.value()))
                    continue;

                movedFile = &folderIt->files[missingFileIt.key()];
                movedFileHash = missingFileIt.key().data;
                newFileHash = newFileIt.key().data;
                movedFilePath = profile.filePath(movedFileHash);
                break;
            }

            if (!movedFile)
                continue;

            // Additional checks for other sync folders
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                if (!otherFolderIt->files.contains(movedFileHash))
                    continue;

                abort = true;
                const SyncFile &fileToMove = otherFolderIt->files.value(movedFileHash);

                // If the file that needs to move does not exist
                if (!fileToMove.exists())
                    break;

                // If a file already exists at the destination location in the database
                if (otherFolderIt->files.contains(newFileIt.key()))
                    break;

                QByteArray newFullPath(otherFolderIt->path);
                newFullPath.append(profile.filePath(newFileIt.key()));

                // If a file already exists at the destination location on disk
                if (QFileInfo::exists(newFullPath))
                {
                    // Both paths should differ, as in the case of changing case of parent folder name, the file still exists in the destination path
                    if (m_caseSensitiveSystem || profile.filePath(newFileIt.key()).compare(movedFilePath, Qt::CaseInsensitive) != 0)
                        break;
                }

                // If the file that needs to move and the moved file don't have the same size and modified date
                if (!fileToMove.hasSameSizeAndDate(*movedFile))
                    break;

                abort = false;
            }

            if (abort)
                continue;

            newFileIt.value()->lockedFlag = SyncFile::Locked;
            folderIt->files.remove(movedFileHash);

            QByteArray newPathToFile = profile.filePath(newFileIt.key());
            QByteArray fullNewPathToFile = folderIt->path;
            fullNewPathToFile.append(newPathToFile);

            // Adds files for moving
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->isActive())
                    continue;

                QByteArray pathToMove(otherFolderIt->path);
                pathToMove.append(movedFilePath);

                otherFolderIt->files[movedFileHash].lockedFlag = SyncFile::Locked;

                QByteArray fromPath = movedFilePath;

                // Removes the old file to move operation in cases where the file was not moved or
                // renamed in other sync folders, but was moved or renamed in the main sync folder again
                if (otherFolderIt->filesToMove.contains(movedFileHash))
                {
                    fromPath = otherFolderIt->filesToMove[movedFileHash].fromPath;
                    otherFolderIt->filesToMove.remove(movedFileHash);
                }

                otherFolderIt->filesToMove.insert(newFileHash, {newPathToFile, fromPath, getFileAttributes(fullNewPathToFile)});

#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                const SyncFile &fileToMove = otherFolderIt->files.value(movedFileHash);
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
            if (folderIt == otherFolderIt || !otherFolderIt->exists || otherFolderIt->syncType == SyncFolder::ONE_WAY || otherFolderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
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

                bool alreadyAdded = folderIt->filesToCopy.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToCopy.value(otherFileIt.key()).modifiedDate < otherFile.modifiedDate;

                // Removes a file path from the "to remove" list if the file was updated
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
                     (!file.exists() && (otherFile.updated() || profile.isTopFolderUpdated(*otherFolderIt, otherFileIt.key().data)))))
                {
                    if (otherFile.type == SyncFile::File)
                    {
                        if (otherFolderIt->filesToRemove.contains(otherFileIt.key()))
                            continue;

                        QByteArray to(profile.filePath(otherFileIt.key()));
                        QByteArray from(otherFolderIt->path);
                        from.append(profile.filePath(otherFileIt.key()));

                        auto it = folderIt->filesToCopy.insert(otherFileIt.key(), {to, from, otherFile.modifiedDate});
                        it->toPath.squeeze();
                        it->fromFullPath.squeeze();
                        folderIt->filesToRemove.remove(otherFileIt.key());
                    }
                    else if (otherFile.type == SyncFile::Folder)
                    {
                        if (otherFolderIt->foldersToRemove.contains(otherFileIt.key()))
                            continue;

                        QByteArray path = profile.filePath(otherFileIt.key());

                        auto it = folderIt->foldersToCreate.insert(otherFileIt.key(), {path, otherFile.attributes});
                        it->path.squeeze();

                        folderIt->foldersToRemove.remove(otherFileIt.key());
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
        if (folderIt->syncType == SyncFolder::ONE_WAY || folderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
            continue;

        for (QHash<Hash, SyncFile>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (!folderIt->isActive())
                break;

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
                if (profile.hasFilePath(fileIt.key()))
                {
                    QString path(folderIt->path);
                    path.append(profile.filePath(fileIt.key()));

                    if (QFileInfo::exists(path))
                    {
                        ++fileIt;
                        continue;
                    }
                }
            }

            // Prevents the removal of folders that do not have a file path in any sync folders.
            // This fixes the issue where, after renaming the case of a folder containing nested folders,
            // the nested folders from their previous location would incorrectly be detected as removed.
            // This was caused by the database retaining the old hashes of the renamed nested folders.
            // This could also lead to the deletion of the sync folder itself, as the profile does not have paths under these old hashes.
            if (!profile.hasFilePath(fileIt.key()))
            {
                ++fileIt;
                continue;
            }

            // Adds files from other folders for removal
            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt || !otherFolderIt->isActive())
                    continue;

                if (otherFolderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
                    continue;

                const SyncFile &fileToRemove = otherFolderIt->files.value(fileIt.key());

                if (fileToRemove.exists())
                {
                    QByteArray path = profile.filePath(fileIt.key());

                    if (fileIt.value().type == SyncFile::Folder)
                        otherFolderIt->foldersToRemove.insert(fileIt.key(), path)->squeeze();
                    else
                        otherFolderIt->filesToRemove.insert(fileIt.key(), path)->squeeze();
                }
                else
                {
                    otherFolderIt->files.remove(fileIt.key());
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
}

/*
===================
SyncManager::removeFile
===================
*/
bool SyncManager::removeFile(SyncProfile &profile, SyncFolder &folder, const QString &path, const QString &fullPath, SyncFile::Type type)
{
    // Prevents the deletion of a sync folder itself in case something bad happens
    if (path.isEmpty())
        return true;

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

        if (type == SyncFile::File)
        {
            // Adds a timestamp to the end of the filename of a deleted file
            if (versioningFormat() == FileTimestampBefore)
            {
                int nameEndIndex = newLocation.lastIndexOf('.');
                int slashIndex = newLocation.lastIndexOf('/');
                int backlashIndex = newLocation.lastIndexOf('\\');

                if (nameEndIndex == -1 || slashIndex >= nameEndIndex || backlashIndex >= nameEndIndex)
                    nameEndIndex = newLocation.length();

                newLocation.insert(nameEndIndex, "_" + QDateTime::currentDateTime().toString(m_versioningPattern));
            }
            // Adds a timestamp to a deleted file before the extension
            else if (versioningFormat() == FileTimestampAfter)
            {
                newLocation.append("_" + QDateTime::currentDateTime().toString(m_versioningPattern));

                // Adds a file extension after the timestamp
                int dotIndex = path.lastIndexOf('.');
                int slashIndex = path.lastIndexOf('/');
                int backlashIndex = path.lastIndexOf('\\');

                if (dotIndex != -1 && slashIndex < dotIndex && backlashIndex < dotIndex)
                    newLocation.append(path.mid(dotIndex));
            }

            // As we want to have only the latest version of files,
            // we need to delete the existing files in the versioning folder first,
            // but only if the deleted file still exists, in case the parent folder was removed earlier.
            if (m_versioningFormat == LastVersion)
            {
                if (!QFile(fullPath).exists())
                    return true;

                QFile::remove(newLocation);
            }
        }

        createParentFolders(profile, folder, QDir::cleanPath(newLocation).toUtf8());
        bool renamed = QFile::rename(fullPath, newLocation);

        // If we're using a file timestamp or last version formats for versioning,
        // a folder might fail to move to the versioning folder if the folder
        // with the exact same filename already exists. In that case, we need
        // to check if the existing folder is empty, and if so, delete it permanently.
        if (!renamed && type == SyncFile::Folder)
            if (m_versioningFormat == FileTimestampBefore || m_versioningFormat == FileTimestampAfter || m_versioningFormat == LastVersion)
                if (QDir(fullPath).isEmpty())
                    return QDir(fullPath).removeRecursively() || !QFileInfo::exists(fullPath);

        return renamed;
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
    QStack<QString> foldersToCreate;

    while (!QDir(path = QFileInfo(path).path().toUtf8()).exists())
        foldersToCreate.append(path);

    while (!foldersToCreate.isEmpty())
    {
        if (QDir().mkdir(foldersToCreate.top()))
        {
            Qt::CaseSensitivity cs;

            if (m_caseSensitiveSystem)
                cs = Qt::CaseSensitive;
            else
                cs = Qt::CaseInsensitive;

            if (foldersToCreate.top().startsWith(folder.path, cs))
            {
                QByteArray relativePath(foldersToCreate.top().toUtf8());
                relativePath.remove(0, folder.path.size());

                hash64_t hash = hash64(relativePath);

                folder.files.insert(hash, SyncFile(SyncFile::Folder, QFileInfo(foldersToCreate.top()).lastModified()));
                folder.foldersToCreate.remove(hash);
                folder.foldersToUpdate.insert(foldersToCreate.top().toUtf8());
                profile.addFilePath(hash, relativePath);
            }
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

        QString fromFullPath(folder.path);
        fromFullPath.append(folderIt->fromPath);

        // Removes from the list if the source file doesn't exist
        if (folderIt->fromPath.isEmpty() || folderIt->toPath.isEmpty() || !QFileInfo::exists(fromFullPath))
        {
            folderIt = folder.foldersToRename.erase(static_cast<QHash<Hash, FolderToRenameInfo>::const_iterator>(folderIt));
            continue;
        }

        QString toFullPath(folder.path);
        toFullPath.append(folderIt->toPath);

        if (QDir().rename(fromFullPath, toFullPath))
        {
            QString parentFrom = QFileInfo(toFullPath).path();
            QString parentTo = QFileInfo(fromFullPath).path();

            setFileAttribute(toFullPath, folderIt->attributes);

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            hash64_t hash = hash64(folderIt->toPath);
            folder.files.remove(folderIt.key());
            auto it = folder.files.insert(hash, SyncFile(SyncFile::Folder, QFileInfo(toFullPath).lastModified()));
            it->lockedFlag = SyncFile::Locked;
            profile.addFilePath(hash, folderIt->toPath);
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

        QByteArray fromFullPath(folder.path);
        fromFullPath.append(fileIt->fromPath);

        // Removes from the list if the source file doesn't exist
        if (fileIt->fromPath.isEmpty() || fileIt->toPath.isEmpty() || !QFileInfo::exists(fromFullPath))
        {
            fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
            continue;
        }

        QByteArray toFullPath(folder.path);
        toFullPath.append(fileIt->toPath);

        // Removes from the list if the file already exists at the destination location
        if (QFileInfo::exists(toFullPath))
        {
            if (m_caseSensitiveSystem)
            {
                fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
                continue;
            }

            QByteArray currentToPath = getCurrentFileInfo(toFullPath).absolutePath().toUtf8();
            currentToPath.remove(0, folder.path.size());

            QByteArray expectedToPath = QFileInfo(toFullPath).absolutePath().toUtf8();
            expectedToPath.remove(0, folder.path.size());

            // For case-insensitive systems, both paths should have the same case.
            // Also, comparing the current parent folder path with the expected parent folder path allows us
            // to postpone moving files until the parent folders have been renamed by case. If the parent folders
            // haven't been renamed to match the expected case, the full path will have a different hash in
            // the database compared to the expected hash. This could lead to false detection, where a moved file is considered as a new.
            if (fromFullPath.compare(toFullPath, Qt::CaseSensitive) == 0 || currentToPath.compare(expectedToPath, Qt::CaseSensitive) == 0)
                fileIt = folder.filesToMove.erase(static_cast<QHash<Hash, FileToMoveInfo>::const_iterator>(fileIt));
            else
                ++fileIt;

            continue;
        }

        createParentFolders(profile, folder, QDir::cleanPath(toFullPath).toUtf8());

        if (QFile::rename(fromFullPath, toFullPath))
        {
            QString parentFrom = QFileInfo(toFullPath).path();
            QString parentTo = QFileInfo(fromFullPath).path();

            if (QFileInfo::exists(parentFrom))
                folder.foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                folder.foldersToUpdate.insert(parentTo.toUtf8());

            setFileAttribute(toFullPath, fileIt->attributes);

            folder.files.remove(hash64(fileIt->fromPath));
            hash64_t hash = hash64(fileIt->toPath);
            auto it = folder.files.insert(hash, SyncFile(SyncFile::File, QFileInfo(toFullPath).lastModified()));
            it->size = QFileInfo(toFullPath).size();
            it->lockedFlag = SyncFile::Locked;
            profile.addFilePath(hash, fileIt->toPath);
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
    // This ensures that the trash folder maintains the same folder structure as in the original destination.
    QVector<QPair<Hash, QByteArray>> sortedFoldersToRemove;
    sortedFoldersToRemove.reserve(folder.foldersToRemove.size());

    for (auto it = folder.foldersToRemove.begin(); it != folder.foldersToRemove.end(); ++it)
        sortedFoldersToRemove.append({it.key(), it.value()});

    std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const auto &a, const auto &b) -> bool { return a.second.size() < b.second.size(); });

    for (auto folderIt = sortedFoldersToRemove.begin(); folderIt != sortedFoldersToRemove.end() && (!m_paused && folder.isActive());)
    {
        if (m_shouldQuit)
            break;

        // Prevents the deletion of the main sync folder in case of a false detection during synchronization
        if (folderIt->second.isEmpty())
        {
            folder.foldersToRemove.remove(folderIt->first);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QPair<Hash, QByteArray>>::const_iterator>(folderIt));
            continue;
        }

        QString fullPath(folder.path);
        fullPath.append(folderIt->second);

        if (removeFile(profile, folder, folderIt->second, fullPath, SyncFile::Folder) || !QDir().exists(fullPath))
        {
            hash64_t hash = hash64(folderIt->second);
            folder.files.remove(hash);
            folder.foldersToRemove.remove(hash);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QPair<Hash, QByteArray>>::const_iterator>(folderIt));

            QString parentPath = QFileInfo(fullPath).path();

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

        // Prevents the deletion of the main sync folder in case of a false detection during synchronization
        if (fileIt->isEmpty())
        {
            fileIt = folder.filesToRemove.erase(static_cast<QHash<Hash, QByteArray>::const_iterator>(fileIt));
            continue;
        }

        QString fullPath(folder.path);
        fullPath.append(*fileIt);

        if (removeFile(profile, folder, *fileIt, fullPath, SyncFile::File) || !QFile().exists(fullPath))
        {
            hash64_t hash = hash64(*fileIt);
            folder.files.remove(hash);
            fileIt = folder.filesToRemove.erase(static_cast<QHash<Hash, QByteArray>::const_iterator>(fileIt));

            QString parentPath = QFileInfo(fullPath).path();

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

        if (folderIt->path.isEmpty())
        {
            folderIt = folder.foldersToCreate.erase(static_cast<QHash<Hash, FolderToCreateInfo>::const_iterator>(folderIt));
            continue;
        }

        QString fullPath(folder.path);
        fullPath.append(folderIt->path);
        QFileInfo fileInfo(fullPath);

        createParentFolders(profile, folder, QDir::cleanPath(fullPath).toUtf8());

        // Removes a file with the same filename first, if it exists
        if (fileInfo.exists() && fileInfo.isFile())
            removeFile(profile, folder, folderIt->path, fullPath, SyncFile::File);

        if (QDir().mkdir(fullPath) || fileInfo.exists())
        {
            hash64_t hash = hash64(folderIt->path);
            auto newFolderIt = folder.files.insert(hash, SyncFile(SyncFile::Folder, fileInfo.lastModified()));
            newFolderIt->attributes = folderIt->attributes;
            profile.addFilePath(hash, folderIt->path);
            folderIt = folder.foldersToCreate.erase(static_cast<QHash<Hash, FolderToCreateInfo>::const_iterator>(folderIt));
            setFileAttribute(fullPath, newFolderIt->attributes);

            QString parentPath = QFileInfo(fullPath).path();

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
        if (!QFileInfo::exists(fileIt->fromFullPath) || fileIt->toPath.isEmpty() || fileIt->fromFullPath.isEmpty())
        {
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
            continue;
        }

        QString toFullPath(folder.path);
        toFullPath.append(fileIt->toPath);
        hash64_t toHash = hash64(fileIt->toPath);
        const SyncFile &toFile = folder.files.value(toHash);
        QFileInfo toFileInfo(toFullPath);

        if (!toFile.exists() && toFileInfo.exists())
        {
            QFileInfo fromFileInfo(fileIt->fromFullPath);

            // Aborts the copy operation if the source file is older than the destination file
            if (toFileInfo.lastModified() > fromFileInfo.lastModified())
            {
                fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
                continue;
            }

            // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
            // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
            if (!m_caseSensitiveSystem)
            {
                QByteArray fromFileName = fileIt->fromFullPath;
                fromFileName.remove(0, fromFileName.lastIndexOf("/") + 1);

                QByteArray currentFilename = getCurrentFileInfo(fileIt->fromFullPath).fileName().toUtf8();

                if (!currentFilename.isEmpty())
                {
                    // Aborts the copy operation if the current path and the path on a disk have different cases
                    if (currentFilename.compare(fromFileName, Qt::CaseSensitive) != 0)
                    {
                        fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));
                        continue;
                    }
                }
            }
        }

        createParentFolders(profile, folder, QDir::cleanPath(toFullPath).toUtf8());

        // Removes a file with the same filename first if exists
        if (toFileInfo.exists())
            removeFile(profile, folder, fileIt->toPath, toFullPath, toFile.type);

        if (QFile::copy(fileIt->fromFullPath, toFullPath))
        {
#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
            setFileModificationDate(filePath, fileIt->modifiedDate);
#endif

            // Do not reorder QQFileInfo fileInfo(fullPath) with setFileModificationDate(), as we want to get the latest modified date
            QFileInfo fileInfo(toFullPath);
            auto it = folder.files.insert(toHash, SyncFile(SyncFile::File, fileInfo.lastModified()));
            it->size = fileInfo.size();
            it->attributes = getFileAttributes(toFullPath);
            profile.addFilePath(toHash, fileIt->toPath);
            fileIt = folder.filesToCopy.erase(static_cast<QHash<Hash, FileToCopyInfo>::const_iterator>(fileIt));

            QByteArray parentPath = toFileInfo.path().toUtf8();

            if (QFileInfo::exists(parentPath))
                folder.foldersToUpdate.insert(parentPath);
        }
        else
        {
            // Not enough disk space notification
            if (m_notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(fileIt->fromFullPath).size())
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
SyncManager::removeUniqueFiles

Removes files from a synchronization folder that do not exist in other synchronization folders
===================
*/
void SyncManager::removeUniqueFiles(SyncProfile &profile, SyncFolder &folder)
{
    const int typeSize = 2;
    SyncFile::Type types[typeSize];

    // In case we add a timestamp to files or keep the last version in the versioning folder,
    // we need to remove the files first. This is mostly because we can't move or delete a folder first
    // if it contains files and already exists in the versioning folder. As a result, at the end of synchronization,
    // we still have that empty folder remaining. Also, in case if we use file timestamp format
    // we want to avoid adding timestamps to each file individually after placing the parent folder
    // in the versioning folder, as it would impact performance.
    if (m_deletionMode == Versioning && (m_versioningFormat == FileTimestampBefore || m_versioningFormat == FileTimestampAfter || m_versioningFormat == LastVersion))
    {
        types[0] = SyncFile::File;
        types[1] = SyncFile::Folder;
    }
    else
    {
        types[0] = SyncFile::Folder;
        types[1] = SyncFile::File;
    }

    for (int i = 0; i < typeSize; i++)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folder.files.begin(); fileIt != folder.files.end();)
        {
            bool exists = false;
            bool hasTwoWay = false;

            if (fileIt->exists() && fileIt->type != types[i])
            {
                ++fileIt;
                continue;
            }

            for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
            {
                if (&(*otherFolderIt) == &folder || !otherFolderIt->exists || !otherFolderIt->isActive())
                    continue;

                // Prevents files from being removed if there are no folders to mirror from
                if (otherFolderIt->syncType == SyncFolder::TWO_WAY)
                    hasTwoWay = true;

                if (otherFolderIt->syncType == SyncFolder::ONE_WAY || otherFolderIt->syncType == SyncFolder::ONE_WAY_UPDATE)
                    continue;

                if (otherFolderIt->files.contains(fileIt.key()))
                {
                    exists = true;
                    break;
                }
            }

            if (!exists && hasTwoWay)
            {
                QString path(profile.filePath(fileIt.key()));
                QString fullPath(folder.path + path);

                removeFile(profile, folder, path, fullPath, fileIt->type);
                fileIt = folder.files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
            }
            else
            {
                ++fileIt;
            }
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
            folder.updateVersioningPath(m_versioningFormat, m_versioningLocation, m_versioningPath, profile.name, m_versioningFolder, m_versioningPattern);

        renameFolders(profile, folder);
        moveFiles(profile, folder);

        // In case we add a timestamp to files or keep the last version in the versioning folder,
        // we need to remove the files first. This is mostly because we can't move or delete a folder first
        // if it contains files and already exists in the versioning folder. As a result, at the end of synchronization,
        // we still have that empty folder remaining. Also, in case if we use file timestamp format
        // we want to avoid adding timestamps to each file individually after placing the parent folder
        // in the versioning folder, as it would impact performance.
        if (m_deletionMode == Versioning && (m_versioningFormat == FileTimestampBefore || m_versioningFormat == FileTimestampAfter || m_versioningFormat == LastVersion))
        {
            removeFiles(profile, folder);
            removeFolders(profile, folder);
        }
        else
        {
            removeFolders(profile, folder);
            removeFiles(profile, folder);
        }

        createFolders(profile, folder);
        copyFiles(profile, folder);

        // We don't want files in one-way folders that don't exist in other folders
        if (folder.syncType == SyncFolder::ONE_WAY)
            removeUniqueFiles(profile, folder);

        folder.versioningPath.clear();

        // Updates the modified date of parent folders as adding or removing files and folders changes their modified date.
        // This is needed for conflict resolution in cases where a file has been modified in one location and deleted in another.
        // SyncManager must synchronize the modified file, effectively ignoring the deletion.
        for (auto folderIt = folder.foldersToUpdate.begin(); folderIt != folder.foldersToUpdate.end();)
        {
            hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, folder.path.size()));

            if (folder.files.contains(folderHash))
                folder.files[folderHash].modifiedDate = QFileInfo(*folderIt).lastModified();

            folderIt = folder.foldersToUpdate.erase(static_cast<QSet<QByteArray>::const_iterator>(folderIt));
        }
    }
}
