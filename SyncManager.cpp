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
SyncManager::addToQueue
===================
*/
void SyncManager::addToQueue(int profileNumber)
{
    if (profiles.isEmpty()) return;

    if (!queue.contains(profileNumber))
    {
        // Adds the passed profile number to the sync queue
        if (profileNumber >= 0 && profileNumber < profiles.size())
        {
            if ((!profiles[profileNumber].paused || syncingMode != Automatic) && !profiles[profileNumber].toBeRemoved)

            queue.enqueue(profileNumber);
        }
        // If a profile number is not passed, adds all remaining profiles to the sync queue
        else
        {
            for (int i = 0; i < profiles.size(); i++)
            {
                if ((!profiles[i].paused || syncingMode != Automatic) && !profiles[i].toBeRemoved && !queue.contains(i))
                {
                    queue.enqueue(i);
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
    if (busy || queue.isEmpty()) return;

    busy = true;
    syncing = false;

#ifdef DEBUG
    std::chrono::high_resolution_clock::time_point syncTime;
    debugSetTime(syncTime);
#endif

    if (!paused)
    {
        int activeFolders = 0;
        QElapsedTimer timer;
        timer.start();

        // Counts active folders in a profile
        for (auto &folder : profiles[queue.head()].folders)
        {
            folder.exists = QFileInfo::exists(folder.path);

            if (!folder.paused && folder.exists && !folder.toBeRemoved)
                activeFolders++;
        }

        if (activeFolders >= 2)
        {
#ifdef DEBUG
            qDebug("=======================================");
            qDebug("Started syncing %s", qUtf8Printable(profiles[queue.head()].name));
            qDebug("=======================================");
#endif

            // Gets lists of all files in folders
            SET_TIME(startTime);

            int result = 0;
            QList<QPair<hash64_t, QFuture<int>>> futureList;

            for (auto &folder : profiles[queue.head()].folders)
            {
                hash64_t requiredDevice = hash64(QStorageInfo(folder.path).device());
                QPair<hash64_t, QFuture<int>> pair(requiredDevice, QFuture(QtConcurrent::run([&](){ return getListOfFiles(folder); })));
                pair.second.suspend();
                futureList.append(pair);
            }

            while (!futureList.isEmpty())
            {
                int i = 0;

                for (auto it = futureList.begin(); it != futureList.end();)
                {
                    if (!usedDevices.contains(it->first))
                    {
                        usedDevices.insert(it->first);
                        it->second.resume();
                    }

                    if (it->second.isFinished())
                    {
                        result += it->second.result();
                        usedDevices.remove(it->first);
                        it = futureList.erase(static_cast<QList<QPair<hash64_t, QFuture<int>>>::const_iterator>(it));
                    }
                    else
                    {
                        it++;
                    }

                    i++;
                }

                if (shouldQuit) return;
            }

            TIMESTAMP(startTime, "Found %d files in %s.", result, qUtf8Printable(profiles[queue.head()].name));

            checkForChanges(profiles[queue.head()]);

            bool countAverage = profiles[queue.head()].syncTime ? true : false;
            profiles[queue.head()].syncTime += timer.elapsed();
            if (countAverage) profiles[queue.head()].syncTime /= 2;

#ifdef DEBUG
            int numOfFoldersToRename = 0;
            int numOfFilesToMove = 0;
            int numOfFoldersToAdd = 0;
            int numOfFilesToAdd = 0;
            int numOfFoldersToRemove = 0;
            int numOfFilesToRemove = 0;

            for (auto &folder : profiles[queue.head()].folders)
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
            if (shouldQuit) return;
        }

        if (syncing)
        {
            // Synchronizes files/folders
            for (auto &folder : profiles[queue.head()].folders)
            {
                QString rootPath = QStorageInfo(folder.path).rootPath();
                bool shouldNotify = notificationList.contains(rootPath) ? !notificationList.value(rootPath)->isActive() : true;
                QSet<QString> foldersToUpdate;

                QString timeStampFolder(folder.path);
                timeStampFolder.append(versionFolder);
                timeStampFolder.append("/");
                timeStampFolder.append(QDateTime::currentDateTime().toString(versionPattern));
                timeStampFolder.append("/");

                auto createParentFolders = [&](QString path)
                {
                    QStack<QString> foldersToCreate;

                    while ((path = QFileInfo(path).path()).length() > folder.path.length())
                    {
                        if (QDir(path).exists()) break;
                        foldersToCreate.append(path);
                    }

                    while (!foldersToCreate.isEmpty())
                    {
                        if (QDir().mkdir(foldersToCreate.top()))
                        {
                            QString shortPath(foldersToCreate.top());
                            shortPath.remove(0, folder.path.size());
                            hash64_t hash = hash64(shortPath.toUtf8());

                            folder.files.insert(hash, File(shortPath.toUtf8(), File::folder, QFileInfo(foldersToCreate.top()).lastModified()));
                            folder.foldersToAdd.remove(hash);
                            foldersToUpdate.insert(foldersToCreate.top());
                        }

                        foldersToCreate.pop();
                    }
                };

                // Sorts the folders for removal from the top to the bottom.
                // This ensures that the trash folder has the exact same folder structure as in the original destination.
                QVector<QString> sortedFoldersToRemove;
                sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
                for (const auto &str : qAsConst(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
                std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

                // Folders to rename
                for (auto it = folder.foldersToRename.begin(); it != folder.foldersToRename.end() && (!paused && !folder.paused);)
                {
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

                        if (QFileInfo::exists(parentFrom)) foldersToUpdate.insert(parentFrom);
                        if (QFileInfo::exists(parentTo)) foldersToUpdate.insert(parentTo);

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

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Files to move
                for (auto it = folder.filesToMove.begin(); it != folder.filesToMove.end() && (!paused && !folder.paused);)
                {
                    // Removes from the "files to move" list if the source file doesn't exist
                    if (!QFileInfo::exists(it.value().second))
                    {
                        it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                        continue;
                    }

                    QString filePath(folder.path);
                    filePath.append(it.value().first);
                    hash64_t fileHash = hash64(it.value().first);

                    createParentFolders(QDir::cleanPath(filePath));

                    // TODO: Fix the case if a file exists at the given destination file path
                    if (QFile::rename(it.value().second, filePath))
                    {
                        QString parentFrom = QFileInfo(filePath).path();
                        QString parentTo = QFileInfo(it.value().second).path();

                        if (QFileInfo::exists(parentFrom)) foldersToUpdate.insert(parentFrom);
                        if (QFileInfo::exists(parentTo)) foldersToUpdate.insert(parentTo);

                        folder.files.remove(hash64(QByteArray(it.value().second).remove(0, folder.path.size())));
                        folder.files.insert(fileHash, File(it.value().first, File::file, QFileInfo(filePath).lastModified()));
                        folder.sizeList[fileHash] = QFileInfo(filePath).size();
                        it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Folders to remove
                for (auto it = sortedFoldersToRemove.begin(); it != sortedFoldersToRemove.end() && (!paused && !folder.paused);)
                {
                    QString folderPath(folder.path);
                    folderPath.append(*it);
                    hash64_t fileHash = hash64(it->toUtf8());
                    QString parentPath = QFileInfo(folderPath).path();

                    bool success;

                    if (deletionMode == MoveToTrash)
                    {
                        // Used to make sure that moveToTrash function really moved a folder
                        // to the trash as it can return true even though it failed to do so
                        QString pathInTrash;

                        success = QFile::moveToTrash(folderPath) && !pathInTrash.isEmpty();
                    }
                    else if (deletionMode == Versioning)
                    {
                        QString newLocation(timeStampFolder);
                        newLocation.append(*it);

                        createParentFolders(QDir::cleanPath(newLocation));
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

                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Files to remove
                for (auto it = folder.filesToRemove.begin(); it != folder.filesToRemove.end() && (!paused && !folder.paused);)
                {
                    QString filePath(folder.path);
                    filePath.append(*it);
                    hash64_t fileHash = hash64(*it);
                    QString parentPath = QFileInfo(filePath).path();

                    bool success;

                    if (deletionMode == MoveToTrash)
                    {
                        // Used to make sure that moveToTrash function really moved a folder
                        // to the trash as it can return true even though it failed to do so
                        QString pathInTrash;

                        success = QFile::moveToTrash(filePath, &pathInTrash) && !pathInTrash.isEmpty();
                    }
                    else if (deletionMode == Versioning)
                    {
                        QString newLocation(timeStampFolder);
                        newLocation.append(*it);

                        createParentFolders(QDir::cleanPath(newLocation));
                        success = QFile().rename(filePath, newLocation);
                    }
                    else
                    {
                        success = QFile::remove(filePath) || !QFileInfo::exists(filePath);
                    }

                    if (success || !QFile().exists(filePath))
                    {
                        folder.files.remove(fileHash);
                        it = folder.filesToRemove.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(it));

                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Folders to add
                for (auto it = folder.foldersToAdd.begin(); it != folder.foldersToAdd.end() && (!paused && !folder.paused);)
                {
                    QString folderPath(folder.path);
                    folderPath.append(*it);
                    hash64_t fileHash = hash64(*it);
                    QFileInfo fileInfo(folderPath);

                    createParentFolders(QDir::cleanPath(folderPath));

                    // Removes a file with the same filename first if exists
                    if (fileInfo.exists() && fileInfo.isFile())
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            QFile::moveToTrash(folderPath);
                        }
                        else if (deletionMode == Versioning)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(*it);

                            createParentFolders(QDir::cleanPath(newLocation));
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
                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Files to copy
                for (auto it = folder.filesToAdd.begin(); it != folder.filesToAdd.end() && (!paused && !folder.paused);)
                {
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
                        if (!caseSensitiveSystem)
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

                    createParentFolders(QDir::cleanPath(filePath));

                    // Removes a file with the same filename first if it exists
                    if (destination.exists())
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            QFile::moveToTrash(filePath);
                        }
                        else if (deletionMode == Versioning)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(it.value().first.first);

                            createParentFolders(QDir::cleanPath(newLocation));
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
                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        // Not enough disk space notification
                        if (notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(it.value().first.second).size())
                        {
                            if (!notificationList.contains(rootPath))
                                notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                            shouldNotify = false;
                            notificationList.value(rootPath)->start(NOTIFICATION_DELAY);
                            emit warning(QString("Not enough disk space on %1 (%2)").arg(QStorageInfo(folder.path).displayName(), rootPath), "");
                        }

                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (shouldQuit && rememberFiles) return;
                }

                // Updates the modified date of the parent folders as adding/removing files and folders change their modified date
                for (auto &folderPath : foldersToUpdate)
                {
                    hash64_t folderHash = hash64(QByteArray(folderPath.toUtf8()).remove(0, folder.path.size()));
                    if (folder.files.contains(folderHash)) folder.files[folderHash].date = QFileInfo(folderPath).lastModified();
                }
            }
        }
    }

    // Optimizes memory usage
    for (auto &folder : profiles[queue.head()].folders)
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

    profiles[queue.head()].lastSyncDate = QDateTime::currentDateTime();
    emit profileSynced(&profiles[queue.head()]);

    queue.dequeue();
    busy = false;
    updateStatus();
    updateNextSyncingTime();

    TIMESTAMP(syncTime, "Syncing is complete.");

    // Starts synchronization of the next profile in the queue if exists
    if (!queue.empty()) sync();

    // Removes profiles/folders completely if we remove them during syncing
    for (auto profileIt = profiles.begin(); profileIt != profiles.end();)
    {
        // Profiles
        if (profileIt->toBeRemoved)
        {
            profileIt = profiles.erase(static_cast<QList<SyncProfile>::const_iterator>(profileIt));
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

    syncHidden = false;
}

/*
===================
SyncManager::getListOfFiles
===================
*/
int SyncManager::getListOfFiles(SyncFolder &folder)
{
    if (folder.paused || !folder.exists) return -1;

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
        if (folder.paused) return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        QByteArray filePath(fileInfo.filePath().toUtf8());
        filePath.remove(0, folder.path.size());
        File::Type type = fileInfo.isDir() ? File::folder : File::file;
        hash64_t fileHash = hash64(filePath);

        // Excludes the versioning folder from scanning
        QByteArray vf(versionFolder.toUtf8());

        if (caseSensitiveSystem ? qstrncmp(vf, filePath, vf.length()) == 0 : qstrnicmp(vf, filePath, vf.length()) == 0)
            continue;

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

                shouldQuit = true;
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
                        if (folder.files.value(hash).updated) break;

                        folder.files[hash].updated = true;
                    }
                }
            }
            else if (type == File::file && file.date != fileDate)
            {
                updated = true;
            }

            file.date = fileDate;
            file.type = type;
            file.exists = true;
            file.updated = updated;
        }
        else
        {
            File *file = const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, fileInfo.lastModified())).operator->());
            file->path.squeeze();
            file->newlyAdded = true;
        }

        if (type == File::file) folder.sizeList[fileHash] = fileInfo.size();

        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#elif defined(USE_STD_FILESYSTEM)
#error FIX: An update required
    for (auto const &dir : std::filesystem::recursive_directory_iterator{std::filesystem::path{folder.path.toStdString()}})
    {
        if (folder.paused) return -1;

        QString filePath(dir.path().string().c_str());
        filePath.remove(0, folder.path.size());
        filePath.replace("\\", "/");

        File::Type type = dir.is_directory() ? File::dir : File::file;
        hash_t fileHash = hash64(filePath);

        const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, QDateTime(), false)).operator->())->path.squeeze();
        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#endif

    usedDevices.remove(hash64(QStorageInfo(folder.path).device()));

    return totalNumOfFiles;
}

/*
===================
SyncManager::checkForChanges
===================
*/
void SyncManager::checkForChanges(SyncProfile &profile)
{
    if ((syncingMode == Automatic && profile.paused) || profile.folders.size() < 2) return;

    // Checks for changed case of folders
    if (detectMovedFiles && !caseSensitiveSystem)
    {
        SET_TIME(startTime);

        for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
        {
            for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
            {
                // Only newly added folder can indicate that the case of filename was changed
                if (fileIt->type != File::folder || !fileIt->newlyAdded || !fileIt->exists)
                    continue;

                bool abort = false;
                QByteArray fileName(fileIt->path);
                fileName.remove(0, fileName.lastIndexOf("/") + 1);

                // Pre checks
                for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                {
                    if (folderIt == otherFolderIt) continue;

                    QString otherPath(otherFolderIt->path);
                    otherPath.append(fileIt->path);
                    QFileInfo otherFileInfo(otherPath);
                    QByteArray otherCurrentlFilename;

                    // Aborts remove operation if other folders don't contain that folder or contain a folder with the same path
                    if (!otherFileInfo.exists() || otherFolderIt->files.contains(fileIt.key()))
                    {
                        abort = true;
                        break;
                    }

                    // Gets the current filename and path of a folder in other sync folder on a disk
                    // Using QDirIterator is the only way to find out this
                    QDirIterator otherDirIterator(otherFileInfo.absolutePath(), {otherFileInfo.fileName()}, QDir::Dirs);

                    if (otherDirIterator.hasNext())
                    {
                        otherDirIterator.next();
                        otherCurrentlFilename = otherDirIterator.fileName().toUtf8();
                    }

                    // Aborts if other folders contain at least a folder with the same case
                    if (otherCurrentlFilename.compare(fileName, Qt::CaseSensitive) == 0)
                    {
                        abort = true;
                        break;
                    }
                }

                if (abort) continue;

                // Finally adds to the folder renaming list
                for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                {
                    if (folderIt == otherFolderIt) continue;

                    QByteArray otherPath(otherFolderIt->path);
                    otherPath.append(fileIt->path);
                    QFileInfo otherFileInfo(otherPath);
                    QByteArray otherCurrentlFilename;
                    QByteArray otherCurrentPath;

                    // Gets the current filename and path of a folder in other sync folder on a disk
                    // Using QDirIterator is the only way to find out this
                    QDirIterator otherDirIterator(otherFileInfo.absolutePath(), {otherFileInfo.fileName()}, QDir::Dirs);

                    if (otherDirIterator.hasNext())
                    {
                        otherDirIterator.next();
                        otherCurrentlFilename = otherDirIterator.fileName().toUtf8();
                        otherCurrentPath = otherDirIterator.filePath().toUtf8();
                        otherCurrentPath.remove(0, otherFolderIt->path.size());
                    }

                    QByteArray newPath(otherCurrentPath);
                    newPath.chop(fileName.size());
                    newPath.append(fileName);

                    // Both folder should have the same filename, but differ in case
                    if (otherCurrentlFilename.compare(fileName, Qt::CaseInsensitive) == 0 && otherCurrentlFilename.compare(fileName, Qt::CaseSensitive) != 0)
                    {
                        if (!fileIt->moved || !otherFolderIt->files.value(hash64(otherCurrentPath)).moved)
                        {
                            QPair<QByteArray, QByteArray> pair(fileIt->path, otherDirIterator.filePath().toUtf8());
                            otherFolderIt->foldersToRename.insert(hash64(otherCurrentPath), pair);
                            qDebug("Rename folder from %s to %s", qUtf8Printable(pair.second), qUtf8Printable(pair.first));
                        }

                        fileIt->moved = true;

                        // Marks all subdirectories in folderIt as moved
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

                        // Marks all subdirectories in folderIt as moved
                        hash64_t otherHash = hash64(otherCurrentPath);

                        if (otherFolderIt->files.contains(otherHash))
                        {
                            File &file = otherFolderIt->files[otherHash];

                            file.moved = true;
                            file.movedSource = true;
                        }

                        QString oldPath(folderIt->path);
                        oldPath.append(otherCurrentPath);

                        QDirIterator oldDirIterator(oldPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                        if (oldDirIterator.hasNext())
                        {
                            oldDirIterator.next();
                            QByteArray path(oldDirIterator.filePath().toUtf8());
                            path.remove(0, folderIt->path.size());
                            hash64_t hash = hash64(path);

                            if (folderIt->files.contains(hash))
                                folderIt->files[hash].movedSource = true;
                        }
                    }
                }
            }
        }

        TIMESTAMP(startTime, "Checked for changed case of folders.");
    }

    // Checks for moved/renamed files
    if (detectMovedFiles)
    {
        SET_TIME(startTime);

        for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
        {
            for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
            {
                bool abort = false;

                // Only newly added file can be renamed or moved
                if (fileIt->type != File::file || !fileIt->newlyAdded || !fileIt->exists)
                    continue;

                // Other folders should not contain the same file
                for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                    if (folderIt != otherFolderIt && otherFolderIt->files.contains(fileIt.key()))
                        abort = true;

                if (abort) continue;

                int matches = 0;
                File *matchedFile;
                hash64_t matchedHash;
                QFileInfo fileInfo(QString(folderIt->path).append(fileIt->path));

                // Searches for potential matches by comparing the size of moved or renamed files to the size of their counterpart
                for (auto &match : folderIt->sizeList.keys(fileInfo.size()))
                {
                    if (!folderIt->files.contains(match))
                        continue;

                    // A potential match should not exist and have the same modified date as the moved/renamed file
                    if (!folderIt->files.value(match).exists && fileInfo.lastModified() == fileIt->date)
                    {
                        matches++;
                        matchedFile = &folderIt->files[match];
                        matchedHash = match;

                        // We only need one match, not more
                        if (matches > 1) break;
                    }
                }

                // Moves a file only if we have one match, otherwise deletes and copies the file
                if (matches == 1)
                {
                    // Pre move checks
                    for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                    {
                        if (folderIt == otherFolderIt || !otherFolderIt->files.contains(matchedHash))
                            continue;

                        QString path(otherFolderIt->path);
                        path.append(fileIt->path);

                        const File &otherFile = otherFolderIt->files.value(matchedHash);

                        // Aborts move operation if other folders have at least a file at the destination location
                        if ((QFileInfo::exists(path) && fileIt->path.compare(otherFile.path, Qt::CaseInsensitive) != 0) || otherFolderIt->files.contains(fileIt.key()))
                        {
                            abort = true;
                            break;
                        }

                        // Aborts move operation if files have different sizes and dates
                        if (otherFolderIt->sizeList.value(matchedHash) != folderIt->sizeList.value(matchedHash) || otherFile.date != fileIt->date)
                        {
                            abort = true;
                            break;
                        }
                    }

                    if (abort) continue;

                    // Finally adds to the file moving list
                    for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                    {
                        if (folderIt == otherFolderIt || !otherFolderIt->files.contains(matchedHash))
                            continue;

                        const File &otherFile = otherFolderIt->files.value(matchedHash);

                        if (!otherFile.exists)
                            continue;

                        hash64_t hash = hash64(fileIt->path);

                        // Marks a file as moved, which prevents it from being added to other folders
                        if (!otherFolderIt->files.contains(hash))
                            otherFolderIt->files[hash].moved = true;

                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);

                        otherFolderIt->files[matchedHash].moved = true;
                        otherFolderIt->filesToMove.insert(matchedHash, QPair<QByteArray, QByteArray>(fileIt->path, from));

                        matchedFile->moved = true;
                        matchedFile->movedSource = true;
                        fileIt->moved = true;
                    }
                }
            }
        }

        TIMESTAMP(startTime, "Checked for moved/renamed files.");
    }

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

                if (file.moved || otherFile.moved || otherFile.movedSource) continue;

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
                 ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated && otherFile.updated && file.date < otherFile.date)))) ||
 #else
                 ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated == otherFile.updated && file.date < otherFile.date)))) ||
 #endif                // Or if other folders has a new version of a file and our file was removed
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
    SET_TIME(startTime);

    // Checks for removed files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (folderIt->paused) break;

            // This file was moved and should be removed from the files list
            if (fileIt.value().movedSource)
            {
                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
                continue;
            }

            if (fileIt.value().moved)
            {
                ++fileIt;
                continue;
            }

            if (!fileIt.value().exists && !folderIt->filesToMove.contains(fileIt.key()) && !folderIt->foldersToAdd.contains(fileIt.key()) && !folderIt->filesToAdd.contains(fileIt.key()) &&
               ((fileIt.value().type == File::file && !folderIt->filesToRemove.contains(fileIt.key())) ||
               (fileIt.value().type == File::folder && !folderIt->foldersToRemove.contains(fileIt.key()))))
            {
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
SyncManager::updateStatus
===================
*/
void SyncManager::updateStatus()
{
    existingProfiles = 0;
    isThereIssue = true;
    isThereWarning = false;

    // Syncing status
    for (int i = -1; auto &profile : profiles)
    {
        i++;
        profile.syncing = false;
        int existingFolders = 0;

        existingProfiles++;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((!queue.isEmpty() && queue.head() != i ) || profile.toBeRemoved)
                continue;

            if (!folder.toBeRemoved)
            {
                if (folder.exists)
                {
                    existingFolders++;

                    if (existingFolders >= 2) isThereIssue = false;
                }
                else
                {
                    isThereWarning = true;
                }
            }

            if (busy && folder.exists && !folder.paused && (!folder.foldersToRename.isEmpty() ||
                                                            !folder.filesToMove.isEmpty() ||
                                                            !folder.foldersToAdd.isEmpty() ||
                                                            !folder.filesToAdd.isEmpty() ||
                                                            !folder.foldersToRemove.isEmpty() ||
                                                            !folder.filesToRemove.isEmpty()))
            {
                syncing = true;
                profile.syncing = true;
                folder.syncing = true;
            }
        }
    }
}

/*
===================
SyncManager::updateTimer
===================
*/
void SyncManager::updateTimer()
{
    if (syncingMode == SyncManager::Automatic)
    {
        if ((!busy && syncTimer.isActive()) || (!syncTimer.isActive() || syncEvery < syncTimer.remainingTime()))
        {
            syncTimer.start(syncEvery);
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
    for (const auto &profile : profiles)
    {
        if (profile.paused) continue;

        int activeFolders = 0;

        for (const auto &folder : profile.folders)
            if (!folder.paused && folder.exists)
                activeFolders++;

        if (activeFolders >= 2) time += profile.syncTime;
    }

    // Multiplies sync time by 2
    for (int i = 0; i < syncTimeMultiplier - 1; i++)
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
    syncEvery = time;
}

/*
===================
SyncManager::saveData
===================
*/
void SyncManager::saveData() const
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    if (!data.open(QIODevice::WriteOnly)) return;

    QDataStream stream(&data);
    stream << profiles.size();

    QStringList profileNames;

    for (auto &profile : profiles)
        profileNames.append(profile.name);

    for (int i = 0; auto &profile : profiles)
    {
        if (profile.toBeRemoved) continue;

        stream << profileNames[i];
        stream << profile.folders.size();

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved) continue;

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
    if (!data.open(QIODevice::ReadOnly)) return;

    QDataStream stream(&data);

    qsizetype profilesSize;
    stream >> profilesSize;

    QStringList profileNames;

    for (auto &profile : profiles)
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
                for (auto &folder : profiles[profileIndex].folders)
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
                    const_cast<File *>(profiles[profileIndex].folders[folderIndex].files.insert(hash, File(QByteArray(), type, date, updated, exists, true)).operator->())->path.squeeze();
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
                    profiles[profileIndex].folders[folderIndex].sizeList.insert(fileHash, fileSize);
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
                    const auto &it = profiles[profileIndex].folders[folderIndex].foldersToRename.insert(hash64(to), pair);
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().first.squeeze();
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().second.squeeze();
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
                    const auto &it = profiles[profileIndex].folders[folderIndex].filesToMove.insert(hash64(to), pair);
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().first.squeeze();
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().second.squeeze();
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
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToAdd.insert(hash64(path), path).operator->())->squeeze();
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
                    const auto &it = profiles[profileIndex].folders[folderIndex].filesToAdd.insert(hash64(to), pair);
                    const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.first.squeeze();
                    const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.second.squeeze();
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
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToRemove.insert(hash64(path), path).operator->())->squeeze();
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
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].filesToRemove.insert(hash64(path), path).operator->())->squeeze();
                }
            }
        }
    }
}
