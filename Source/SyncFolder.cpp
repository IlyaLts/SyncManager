/*
===============================================================================
    Copyright (C) 2022-2026 Ilya Lyakhovets

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

#include "SyncFolder.h"
#include "Application.h"
#include "FileManager.h"
#include <QStandardPaths>
#include <QSettings>
#include <QStack>
#include <QRandomGenerator>
#include <QStorageInfo>
#include <QDirIterator>
#include <QSaveFile>

/*
===================
SyncFolder::SyncFolder
===================
*/
SyncFolder::SyncFolder(SyncProfile *profile, const QByteArray &path)
{
    m_profile = profile;
    m_path = path;
    m_deviceHash = hash64(QStorageInfo(path).device());

    m_paused = profile->paused();
}

/*
===================
SyncFolder::loadSettings
===================
*/
void SyncFolder::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString folderKey(m_profile->name() + QLatin1String("_profile/") + m_path);

    m_exists = QFileInfo::exists(m_path);
    m_lastSyncDate = settings.value(folderKey + QLatin1String("_LastSyncDate")).toDateTime();
    m_paused = settings.value(folderKey + QLatin1String("_Paused"), false).toBool();
    setType(static_cast<SyncFolder::Type>(settings.value(folderKey + QLatin1String("_SyncType"), SyncFolder::TWO_WAY).toInt()));
    m_unsyncedList = settings.value(folderKey + QLatin1String("_UnsyncedFiles")).toString();

    if (!m_paused)
        syncApp->syncManager()->setPaused(false);
}

/*
===================
SyncFolder::saveSettings
===================
*/
void SyncFolder::saveSettings() const
{
    if (m_toBeRemoved)
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString folderKey(m_profile->name() + QLatin1String("_profile/") + m_path);

    settings.setValue(folderKey + QLatin1String("_LastSyncDate"), m_lastSyncDate);
    settings.setValue(folderKey + QLatin1String("_Paused"), m_paused);
    settings.setValue(folderKey + QLatin1String("_SyncType"), m_type);
    settings.setValue(folderKey + QLatin1String("_UnsyncedFiles"), m_unsyncedList);
}

/*
===================
SyncFolder::removeSettings
===================
*/
void SyncFolder::removeSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(m_profile->name() + QLatin1String("_profile/") + m_path);
}

/*
===================
SyncFolder::createParentFolders

Creates all necessary parent directories for a given file path
===================
*/
void SyncFolder::createParentFolders(const QByteArray &path)
{
    QStack<QString> list;
    QByteArray currentPath = path;

    while (!QDir(currentPath = QFileInfo(currentPath).path().toUtf8()).exists())
        list.append(currentPath);

    while (!list.isEmpty())
    {
        syncApp->throttleDown();

        if (QDir().mkdir(list.top()))
        {
            Qt::CaseSensitivity cs;

            if (m_caseSensitive)
                cs = Qt::CaseSensitive;
            else
                cs = Qt::CaseInsensitive;

            if (list.top().startsWith(currentPath, cs))
            {
                QByteArray relativePath(list.top().toUtf8());
                relativePath.remove(0, currentPath.size());

                hash64_t hash = hash64(relativePath);

                m_files.emplace(hash, SyncFile(SyncFile::Folder, QFileInfo(list.top()).lastModified()));
                m_foldersToCreate.remove(hash);
                m_foldersToUpdate.insert(list.top().toUtf8());
            }
        }

        list.pop();
    }
}

/*
===================
SyncFolder::removeFile
===================
*/
bool SyncFolder::removeFile(const QString &path, SyncFile::Type type)
{
    // Prevents the deletion of a sync folder itself in case something bad happens
    if (path.isEmpty())
        return true;

    QString fullPath(this->m_path);
    fullPath.append(path);

    if (profile().deletionMode() == SyncProfile::MoveToTrash)
    {
        return syncApp->fileManager()->moveToTrash(fullPath);
    }
    else if (profile().deletionMode() == SyncProfile::Versioning)
    {
        QString newLocation(m_versioningPath);
        newLocation.append(path);

        if (type == SyncFile::File)
        {
            // Adds a timestamp to the end of the filename of a deleted file
            if (profile().versioningFormat() == SyncProfile::FileTimestampBefore)
            {
                addTimestampBeforeExt(newLocation, profile().versioningPattern(), "_");
            }
            // Adds a timestamp to a deleted file before the extension
            else if (profile().versioningFormat() == SyncProfile::FileTimestampAfter)
            {
                addTimestampAfterExt(newLocation, profile().versioningPattern(), "_");
            }
            // As we want to have only the latest version of files,
            // we need to delete the existing files in the versioning folder first,
            // but only if the deleted file still exists, in case the parent folder was removed earlier.
            else if (profile().versioningFormat() == SyncProfile::LastVersion)
            {
                if (!QFile(fullPath).exists())
                    return true;

                if (!syncApp->fileManager()->remove(newLocation))
                    return false;
            }
        }

        createParentFolders(QDir::cleanPath(newLocation).toUtf8());
        bool renamed = QFile::rename(fullPath, newLocation);

        // If we're using a file timestamp or last version formats for versioning,
        // a folder might fail to move to the versioning folder if the folder
        // with the exact same filename already exists. In that case, we need
        // to check if the existing folder is empty, and if so, delete it permanently.
        if (!renamed && type == SyncFile::Folder)
            if (profile().versioningFormat() != SyncProfile::FolderTimestamp)
                if (QDir(fullPath).isEmpty())
                    return syncApp->fileManager()->remove(fullPath);

        return renamed;
    }
    else
    {
        return syncApp->fileManager()->remove(fullPath);
    }
}

/*
===================
SyncFolder::cleanup

Used by one-way synchronization folders.
Removes m_files from a synchronization folder that do not exist in other two-way synchronization folders
===================
*/
void SyncFolder::cleanup()
{
    if (m_type != ONE_WAY)
        return;

    const int typeSize = 2;
    SyncFile::Type types[typeSize];

    // In case we add a timestamp to files or keep the last version in the versioning folder,
    // we need to remove the files first. This is mostly because we can't move or delete a folder first
    // if it contains files and already exists in the versioning folder. As a result, at the end of synchronization,
    // we still have that empty folder remaining. Also, in case if we use file timestamp format
    // we want to avoid adding timestamps to each file individually after placing the parent folder
    // in the versioning folder, as it would impact performance.
    if (profile().deletionMode() == SyncProfile::Versioning && profile().versioningFormat() != SyncProfile::FolderTimestamp)
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
        for (Files::iterator fileIt = m_files.begin(); fileIt != m_files.end();)
        {
            bool exists = false;
            bool hasTwoWay = false;

            if (fileIt->exists() && fileIt->type != types[i])
            {
                ++fileIt;
                continue;
            }

            for (auto otherFolderIt = profile().folders().begin(); otherFolderIt != profile().folders().end(); ++otherFolderIt)
            {
                if (&(*otherFolderIt) == this || !otherFolderIt->m_exists || !otherFolderIt->active())
                    continue;

                // Prevents files from being removed if there are no folders to mirror from
                if (otherFolderIt->type() == TWO_WAY)
                    hasTwoWay = true;
                else
                    continue;

                if (otherFolderIt->m_files.contains(fileIt.key()))
                {
                    exists = true;
                    break;
                }

                // In case there is a file, but with a different case name
                if (!otherFolderIt->m_caseSensitive && QFile(profile().getFilePath(fileIt.key())).exists())
                {
                    exists = true;
                    break;
                }
            }

            if (!exists && hasTwoWay)
            {
                removeFile(profile().getFilePath(fileIt.key()), fileIt->type);
                fileIt = m_files.erase(static_cast<Files::const_iterator>(fileIt));
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
SyncFolder::clearData
===================
*/
void SyncFolder::clearData()
{
    m_files.clear();
    m_conflictedFilesToRename.clear();
    m_foldersToRename.clear();
    m_filesToMove.clear();
    m_foldersToCreate.clear();
    m_filesToCopy.clear();
    m_foldersToRemove.clear();
    m_filesToRemove.clear();
    m_foldersToUpdate.clear();
}

/*
===================
SyncFolder::optimizeMemoryUsage
===================
*/
void SyncFolder::optimizeMemoryUsage()
{
    m_files.squeeze();
    m_conflictedFilesToRename.squeeze();
    m_foldersToRename.squeeze();
    m_filesToMove.squeeze();
    m_foldersToCreate.squeeze();
    m_filesToCopy.squeeze();
    m_foldersToRemove.squeeze();
    m_filesToRemove.squeeze();
    m_foldersToUpdate.squeeze();
}

/*
===================
SyncFolder::updateVersioningPath
===================
*/
void SyncFolder::updateVersioningPath()
{
    if (m_profile->versioningLocation() == SyncProfile::CustomLocation)
    {
        m_versioningPath.assign(m_profile->versioningPath());
    }
    else
    {
        m_versioningPath.assign(this->m_path);
        m_versioningPath.remove(m_versioningPath.lastIndexOf("/", 1), m_versioningPath.size());
        m_versioningPath.append("_");
        m_versioningPath.append(m_profile->versioningFolder());
    }

    m_versioningPath.append("/");

    if (m_profile->versioningLocation() == SyncProfile::CustomLocation)
        m_versioningPath.append(m_profile->name() + "/");

    if (m_profile->versioningFormat() == SyncProfile::FolderTimestamp)
        m_versioningPath.append(QDateTime::currentDateTime().toString(m_profile->versioningPattern()) + "/");
}

/*
===================
SyncFolder::checkCaseSensitive

Updates whether the folder is case-sensitive or not
===================
*/
void SyncFolder::checkCaseSensitive()
{
    if (!m_exists)
        return;

    QDir dir(m_path);

    if (!dir.exists())
        return;

    QString uniqueFilename;
    QString letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int numberOfLetters = letters.length();

    for (int i = 0; i < 10; ++i)
        uniqueFilename.append(letters.at(QRandomGenerator::global()->bounded(numberOfLetters)));

    uniqueFilename.append(".tmp");

    QString lowerCaseFilename = uniqueFilename.toLower();
    QString upperCaseFilename = uniqueFilename.toUpper();
    QString fullPath = dir.absoluteFilePath(uniqueFilename);
    bool caseSensitive = true;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly))
        return;

    file.close();

    if (uniqueFilename != lowerCaseFilename)
        if (QFile::exists(dir.absoluteFilePath(lowerCaseFilename)))
            caseSensitive = false;

    if (caseSensitive && uniqueFilename != upperCaseFilename)
        if (QFile::exists(dir.absoluteFilePath(upperCaseFilename)))
            caseSensitive = false;

    syncApp->fileManager()->remove(fullPath);
    this->m_caseSensitive = caseSensitive;
}

/*
===================
SyncFolder::loadDatabasesLocally
===================
*/
void SyncFolder::loadDatabasesLocally()
{
    if (!active() || toBeRemoved())
        return;

    QByteArray filename(QByteArray::number(hash64(path())) + ".db");
    loadDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
}

/*
===================
SyncFolder::loadDatabasesDecentralised
===================
*/
void SyncFolder::loadDatabasesDecentralised()
{
    if (!active() || toBeRemoved())
        return;

    loadDatabase(path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
}

/*
===================
SyncFolder::saveDatabasesLocally
===================
*/
void SyncFolder::saveDatabasesLocally()
{
    if (!active() || toBeRemoved())
        return;

    QByteArray filename(QByteArray::number(hash64(path())) + ".db");
    saveDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    m_databaseChanged = false;
}

/*
===================
SyncFolder::saveDatabasesDecentralised
===================
*/
void SyncFolder::saveDatabasesDecentralised()
{
    if (!active() || toBeRemoved())
        return;

    QDir().mkdir(path() + DATA_FOLDER_PATH);

    if (!QDir(path() + DATA_FOLDER_PATH).exists())
        return;

    saveDatabase(path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);

#ifdef Q_OS_WIN
    FileManager::setHiddenAttribute(QString(path() + DATA_FOLDER_PATH), true);
    FileManager::setHiddenAttribute(QString(path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
#endif

    m_databaseChanged = false;
}

/*
===================
SyncFolder::removeDatabase
===================
*/
void SyncFolder::removeDatabase() const
{
    QByteArray filename = QByteArray::number(hash64(m_path));
    syncApp->fileManager()->remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename + ".db");
    syncApp->fileManager()->remove(m_path + DATA_FOLDER_PATH);
}

/*
===================
SyncFolder::removeNonexistentFiles

Used to make mirroring work properly
===================
*/
void SyncFolder::removeNonexistentFiles()
{
    for (Files::iterator fileIt = m_files.begin(); fileIt != m_files.end();)
    {
        if (!fileIt->exists())
            fileIt = m_files.erase(static_cast<Files::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
SyncFolder::active
===================
*/
bool SyncFolder::active() const
{
    return !m_paused && !m_toBeRemoved && m_exists;
}

/*
===================
SyncFolder::hasUnsyncedFiles
===================
*/
bool SyncFolder::hasUnsyncedFiles() const
{
    return !m_conflictedFilesToRename.isEmpty() ||
           !m_foldersToRename.isEmpty() ||
           !m_filesToMove.isEmpty() ||
           !m_foldersToCreate.isEmpty() ||
           !m_filesToCopy.isEmpty() ||
           !m_foldersToRemove.isEmpty() ||
           !m_filesToRemove.isEmpty();
}

/*
===================
SyncFolder::partiallySynchronized
===================
*/
bool SyncFolder::partiallySynchronized() const
{
    return !m_unsyncedList.isEmpty();
}

/*
===================
SyncFolder::updateUnsyncedList
===================
*/
void SyncFolder::updateUnsyncedList()
{
    m_unsyncedList.clear();

    if (hasUnsyncedFiles())
    {
        m_unsyncedList.append(syncApp->translate("The following files are not synchronized:"));
        m_unsyncedList.append("\n\n");

        for (auto &path : m_conflictedFilesToRename)
            m_unsyncedList.append(path.path + "\n");

        for (auto &path : m_foldersToRename)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : m_filesToMove)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : m_foldersToCreate)
            m_unsyncedList.append(path.path + "\n");

        for (auto &path : m_filesToCopy)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : m_foldersToRemove)
            m_unsyncedList.append(path + "\n");

        for (auto &path : m_filesToRemove)
            m_unsyncedList.append(path + "\n");

        m_unsyncedList.append("\n");
    }

    if (hasCorruptedFiles())
    {
        m_unsyncedList.append(syncApp->translate("The following files are corrupted:"));
        m_unsyncedList.append("\n\n");

        for (auto fileIt = m_files.begin(); fileIt != m_files.end(); fileIt++)
            if (fileIt->corrupted())
                m_unsyncedList.append(m_profile->getFilePath(fileIt.key()) + "\n");

        m_unsyncedList.append("\n");
    }

    if (hasConflictedFiles())
    {
        m_unsyncedList.append(syncApp->translate("The following files are conflicted:"));
        m_unsyncedList.append("\n\n");

        for (auto fileIt = m_files.begin(); fileIt != m_files.end(); fileIt++)
            if (fileIt->conflictDetected())
                m_unsyncedList.append(m_profile->getFilePath(fileIt.key()) + "\n");

        m_unsyncedList.append("\n");
    }
}

/*
===================
SyncFolder::checkForFolderContentPrecedence
===================
*/
void SyncFolder::checkForFolderContentPrecedence()
{
    for (auto fileIt = files().begin(); fileIt != files().end(); ++fileIt)
    {
        if (fileIt->exists() && !fileIt->newlyAdded())
            continue;

        QByteArray path = m_profile->getFilePath(fileIt.key());
        path.remove(path.indexOf('/'), path.size());

        files()[hash64(path)].setPrecedence(true);
    }
}

/*
===================
SyncFolder::checkForCorruptedFiles

This checks only for corrupted files within a corrupted, unreadable folder,
which are completely invisible on the disk. So, we have to check the parent folders
for a corruption flag and manually mark their files as corrupted as well, and vice versa.
===================
*/
void SyncFolder::checkForCorruptedFiles()
{
    for (auto fileIt = m_files.begin(); fileIt != m_files.end();)
    {
        // We only need files that we cannot see
        if (fileIt->exists())
        {
            fileIt++;
            continue;
        }

        bool corrupted = false;
        QByteArray path = m_profile->getFilePath(fileIt.key());

        // Such a file doesn't exist in all sync folders
        if (path.isEmpty())
        {
            fileIt = m_files.erase(fileIt);
            continue;
        }

        path.insert(0, m_path);

        // Goes through all parent folders
        while ((path = QFileInfo(path).path().toUtf8()).length() > m_path.length())
        {
            QByteArray relativePath(path);
            relativePath.remove(0, m_path.length());

            const SyncFile &file = m_files.value(hash64(relativePath));

            if (file.exists())
            {
                if (file.corrupted())
                    corrupted = true;

                break;
            }
        }

        // Since we cannot see the file and the parent folder is no longer corrupted,
        // we need to remove the file data from the database to make the app sync
        // those lost files again after fixing the disk.
        if (!corrupted && fileIt->corrupted())
        {
            fileIt = m_files.erase(fileIt);
            m_databaseChanged = true;
        }
        else
        {
            m_databaseChanged = fileIt->corrupted() != corrupted ? true : m_databaseChanged;
            fileIt->setCorrupted(corrupted);
            fileIt++;
        }
    }

    m_hasCorruptedFiles = false;

    for (const auto &file : m_files)
    {
        if (file.corrupted())
        {
            m_hasCorruptedFiles = true;
            return;
        }
    }
}

/*
===================
SyncFolder::checkForConflictedFiles
===================
*/
void SyncFolder::checkForConflictedFiles()
{
    m_hasConflictedFiles = false;

    for (const auto &file : m_files)
    {
        if (file.conflictDetected())
        {
            m_hasConflictedFiles = true;
            return;
        }
    }
}

/*
===================
SyncFolder::removeNonexistentFileData

If a file saved in the database isn't found on disk,
it no longer exists in the folders, so we don't want
to keep it in the database unless it's corrupted
===================
*/
void SyncFolder::removeNonexistentFileData()
{
    for (Files::iterator fileIt = m_files.begin(); fileIt != m_files.end();)
    {
        if ((!fileIt->exists() || fileIt->type == SyncFile::Unknown) && !fileIt->corrupted())
        {
            m_databaseChanged = true;
            fileIt = m_files.erase(static_cast<Files::const_iterator>(fileIt));
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::remove
===================
*/
void SyncFolder::remove()
{
    setPaused(true);
    m_toBeRemoved = true;
    removeSettings();
}

/*
===================
SyncFolder::scanFiles
===================
*/
int SyncFolder::scanFiles()
{
    auto deviceRead = syncApp->fileManager()->deviceRead(m_deviceHash);
    int totalNumOfFiles = 0;
    QStringList nameFilters(m_profile->includeList());
    nameFilters.removeAll(""); // It's important for proper iteration because the include list may contain empty strings

    if (nameFilters.isEmpty())
        nameFilters.append("*");

    QDir::Filters filters = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden;
    QDirIterator dir(path(), nameFilters, filters, QDirIterator::Subdirectories);
    QStringList systemDirList;

    // Reset flags except the corrupted flag
    for (auto &file : m_files)
        file.flags &= SyncFile::Corrupted;

    while (dir.hasNext())
    {
        if (syncApp->syncManager()->quitting() || !active())
            return -1;

        syncApp->throttleDown();
        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        qint64 fileSize = fileInfo.size();
        QByteArray absoluteFilePath = fileInfo.filePath().toUtf8();

        deviceRead += fileSize;

        // If the file is a symlink, this function returns information about the target, not the symlink
        if (fileInfo.isSymLink())
            fileSize = 0;

        // Skips system files
        if (m_profile->ignoreSystemFiles() && FileManager::isSystem(absoluteFilePath))
        {
            if (fileInfo.isDir())
            {
                systemDirList.append(absoluteFilePath);
                systemDirList.last().append("/*");
            }

            continue;
        }

        // Skips hidden files
        if (m_profile->ignoreHiddenFiles() && fileInfo.isHidden())
            continue;

        // Skips database files
        if (m_profile->databaseLocation() == SyncProfile::Decentralized)
        {
            if (fileInfo.isHidden())
            {
                if (fileInfo.fileName().compare(DATA_FOLDER_PATH, Qt::CaseInsensitive) == 0)
                    continue;

                if (fileInfo.fileName().compare(DATABASE_FILENAME, Qt::CaseInsensitive) == 0)
                    continue;
            }
        }

        if (fileInfo.isFile())
        {
            if (m_profile->fileMinSize() > static_cast<quint64>(fileSize))
                continue;

            if (m_profile->fileMaxSize() > 0 && static_cast<quint64>(fileSize) > m_profile->fileMaxSize())
                continue;
        }

        QByteArray filePath(absoluteFilePath);
        filePath.remove(0, path().size());

        if (fileInfo.isFile() && fileInfo.suffix().compare(TEMP_EXTENSION, Qt::CaseInsensitive) == 0)
            syncApp->fileManager()->remove(absoluteFilePath);

        // Excludes system files
        if (m_profile->ignoreSystemFiles())
            if (hasMatch(systemDirList, absoluteFilePath, caseSensitive()))
                continue;

        // Excludes unwanted files
        if (hasMatch(m_profile->excludeList(), filePath, caseSensitive()))
            continue;

        SyncFile::Type type = fileInfo.isDir() ? SyncFile::Folder : SyncFile::File;
        hash64_t fileHash = hash64(filePath);

        m_profile->addFilePath(fileHash, filePath);

        auto fileIt = m_files.find(fileHash);

        // If a file is already in our database
        if (fileIt != m_files.end())
        {
            SyncFile &file = *fileIt;
            QDateTime modifiedDate(fileInfo.lastModified());
            attributes_t attributes = FileManager::getAttributes(absoluteFilePath);

            // Quits if a hash collision is detected
            if (file.scanned())
            {
#ifndef DEBUG
                QString title("Hash collision detected!");
                QString text = QString("%1 vs %2").arg(qUtf8Printable(filePath), qUtf8Printable(m_profile->getFilePath(fileHash)));
                QMessageBox::critical(nullptr, title, text);
#else
                qCritical("Hash collision detected: %s vs %s", qUtf8Printable(filePath), qUtf8Printable(m_profile->getFilePath(fileHash)));
#endif

                syncApp->syncManager()->quit();
                qApp->quit();
                return -1;
            }

            if (file.modifiedDate != modifiedDate)
            {
                m_databaseChanged = true;
                file.setUpdated(true);
            }

            if (file.size != static_cast<quint64>(fileSize))
                m_databaseChanged = true;

            if (file.type != type)
                m_databaseChanged = true;

            if (file.attributes != attributes)
            {
                m_databaseChanged = true;
                file.setAttributesUpdated(true);
            }

            // Marks all parent folders as updated if the current folder was updated
            if (file.updated())
            {
                QByteArray folderPath(fileInfo.filePath().toUtf8());

                while (folderPath.remove(folderPath.lastIndexOf("/"), folderPath.length()).length() > path().length())
                {
                    hash64_t hash = hash64(QByteArray(folderPath).remove(0, path().size()));

                    auto parentIt = m_files.find(hash);

                    if (parentIt != m_files.end())
                    {
                        if (parentIt->updated())
                            break;

                        parentIt->setUpdated(true);
                    }
                }
            }

            file.modifiedDate = modifiedDate;
            file.size = fileSize;
            file.type = type;
            file.attributes = attributes;
            file.setExists(true);
            file.setScanned(true);
            file.setReadOnly(!fileInfo.isWritable());

            bool corrupted = !QFileInfo::exists(absoluteFilePath);
            m_databaseChanged = file.corrupted() != corrupted ? true : m_databaseChanged;
            file.setCorrupted(corrupted);

            m_profile->removeUnneededFilePath(fileHash);
        }
        // If a file is new
        else
        {
            SyncFile *file = m_files.emplace(fileHash, SyncFile(type, fileInfo.lastModified())).operator->();
            file->size = fileSize;
            file->attributes = FileManager::getAttributes(absoluteFilePath);
            file->setNewlyAdded(true);
            file->setScanned(true);
            file->setReadOnly(!fileInfo.isWritable());
            file->setCorrupted(!QFileInfo::exists(absoluteFilePath));

            m_databaseChanged = true;
        }

        totalNumOfFiles++;
    }

    return totalNumOfFiles;
}

/*
===================
SyncFolder::renameConflictedFiles
===================
*/
void SyncFolder::renameConflictedFiles()
{
    for (auto fileIt = m_conflictedFilesToRename.begin(); fileIt != m_conflictedFilesToRename.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        QString fromFullPath(path());
        fromFullPath.append(fileIt->path);

        // Removes from the list if the source file doesn't exist
        if (!QFileInfo::exists(fromFullPath))
        {
            fileIt = m_conflictedFilesToRename.erase(static_cast<ConflictedFileRenameList::const_iterator>(fileIt));
            continue;
        }

        QString newPath(fileIt->path);
        addTimestampBeforeExt(newPath, "yyyy_M_d_h_m_s_z", "_Conflict_");

        // Adds a postfix number in case a file with that name already exists
        for (int i = 2;; i++)
        {
            bool exists = false;

            for (auto &otherFolder : profile().folders())
            {
                if (&otherFolder == this)
                    continue;

                if (QFileInfo::exists(otherFolder.path() + newPath))
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
                break;

            newPath = fileIt->path;
            addTimestampBeforeExt(newPath, "yyyy_M_d_h_m_s_z_" + QString::number(i), "_Conflict_");
        }

        QString toFullPath(path());
        toFullPath.append(newPath);

        hash64_t fromHash = hash64(fileIt->path);
        hash64_t newHash = hash64(newPath.toUtf8());

        if (QFile::rename(fromFullPath, toFullPath))
        {
            QFileInfo toFileInfo(toFullPath);
            m_files.remove(fromHash);
            auto it = m_files.emplace(newHash, SyncFile(SyncFile::File, toFileInfo.lastModified()));
            it->size = toFileInfo.size();
            it->attributes = FileManager::getAttributes(toFullPath);
            profile().addFilePath(newHash, newPath.toUtf8());
            fileIt = m_conflictedFilesToRename.erase(static_cast<ConflictedFileRenameList::const_iterator>(fileIt));

            QByteArray parentPath = toFileInfo.path().toUtf8();

            if (QFileInfo::exists(parentPath))
                m_foldersToUpdate.insert(parentPath);

            // Adds renamed files for copying to other sync folders
            for (auto &otherFolder : profile().folders())
            {
                if (&otherFolder == this)
                    continue;

                otherFolder.addFileToCopy(newHash, newPath.toUtf8(), toFullPath.toUtf8(), toFileInfo.lastModified());
            }
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::renameFolders
===================
*/
void SyncFolder::renameFolders()
{
    for (auto folderIt = m_foldersToRename.begin(); folderIt != m_foldersToRename.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        QString fromFullPath(path());
        fromFullPath.append(folderIt->fromPath);

        // Removes from the list if the source file doesn't exist
        if (folderIt->fromPath.isEmpty() || folderIt->toPath.isEmpty() || !QFileInfo::exists(fromFullPath))
        {
            folderIt = m_foldersToRename.erase(static_cast<FolderRenameList::const_iterator>(folderIt));
            continue;
        }

        QString toFullPath(path());
        toFullPath.append(folderIt->toPath);

        if (QDir().rename(fromFullPath, toFullPath))
        {
            QString parentFrom = QFileInfo(toFullPath).path();
            QString parentTo = QFileInfo(fromFullPath).path();

            FileManager::setAttribute(toFullPath, folderIt->attributes);

            if (QFileInfo::exists(parentFrom))
                m_foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                m_foldersToUpdate.insert(parentTo.toUtf8());

            hash64_t hash = hash64(folderIt->toPath);
            m_files.remove(folderIt.key());
            auto it = m_files.emplace(hash, SyncFile(SyncFile::Folder, QFileInfo(toFullPath).lastModified()));
            it->lockedFlag = SyncFile::Locked;
            folderIt = m_foldersToRename.erase(static_cast<FolderRenameList::const_iterator>(folderIt));
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncFolder::moveFiles
===================
*/
void SyncFolder::moveFiles()
{
    for (auto fileIt = m_filesToMove.begin(); fileIt != m_filesToMove.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        QByteArray fromFullPath(path());
        fromFullPath.append(fileIt->fromPath);

        // Removes from the list if the source file doesn't exist
        if (fileIt->fromPath.isEmpty() || fileIt->toPath.isEmpty() || !QFileInfo::exists(fromFullPath))
        {
            fileIt = m_filesToMove.erase(static_cast<FileMoveList::const_iterator>(fileIt));
            continue;
        }

        QByteArray toFullPath(path());
        toFullPath.append(fileIt->toPath);

        // Removes from the list if the file already exists at the destination location
        if (QFileInfo::exists(toFullPath))
        {
            if (caseSensitive())
            {
                fileIt = m_filesToMove.erase(static_cast<FileMoveList::const_iterator>(fileIt));
                continue;
            }
            else
            {
                QByteArray currentToPath = FileManager::getCurrentFileInfo(toFullPath).absoluteFilePath().toUtf8();
                currentToPath.remove(0, path().size());

                QByteArray expectedToPath = QFileInfo(toFullPath).absoluteFilePath().toUtf8();
                expectedToPath.remove(0, path().size());

                // For case-insensitive systems, both paths should have the same case.
                // Also, comparing the current parent folder path with the expected parent folder path allows us
                // to postpone moving files until the parent folders have been renamed by case. If the parent folders
                // haven't been renamed to match the expected case, the full path will have a different hash in
                // the database compared to the expected hash. This could lead to false detection, where a moved file is considered as a new.
                if (fromFullPath.compare(toFullPath, Qt::CaseSensitive) == 0 || currentToPath.compare(expectedToPath, Qt::CaseSensitive) == 0)
                {
                    m_files.remove(hash64(fileIt->fromPath));
                    hash64_t hash = hash64(fileIt->toPath);
                    auto it = m_files.emplace(hash, SyncFile(SyncFile::File, QFileInfo(toFullPath).lastModified()));
                    it->size = QFileInfo(toFullPath).size();
                    it->lockedFlag = SyncFile::Locked;
                    fileIt = m_filesToMove.erase(static_cast<FileMoveList::const_iterator>(fileIt));
                    continue;
                }
            }
        }

        createParentFolders(QDir::cleanPath(toFullPath).toUtf8());

        if (QFile::rename(fromFullPath, toFullPath))
        {
            QString parentFrom = QFileInfo(toFullPath).path();
            QString parentTo = QFileInfo(fromFullPath).path();

            if (QFileInfo::exists(parentFrom))
                m_foldersToUpdate.insert(parentFrom.toUtf8());

            if (QFileInfo::exists(parentTo))
                m_foldersToUpdate.insert(parentTo.toUtf8());

            FileManager::setAttribute(toFullPath, fileIt->attributes);

            m_files.remove(hash64(fileIt->fromPath));
            hash64_t hash = hash64(fileIt->toPath);
            auto it = m_files.emplace(hash, SyncFile(SyncFile::File, QFileInfo(toFullPath).lastModified()));
            it->size = QFileInfo(toFullPath).size();
            it->lockedFlag = SyncFile::Locked;
            fileIt = m_filesToMove.erase(static_cast<FileMoveList::const_iterator>(fileIt));
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::removeFolders
===================
*/
void SyncFolder::removeFolders()
{
    // Sorts the folders for removal from the top to the bottom.
    // This ensures that the trash folder maintains the same folder structure as in the original destination.
    QVector<QPair<SyncHash, QByteArray>> sortedFoldersToRemove;
    sortedFoldersToRemove.reserve(m_foldersToRemove.size());

    for (auto it = m_foldersToRemove.begin(); it != m_foldersToRemove.end(); ++it)
        sortedFoldersToRemove.append({it.key(), it.value()});

    std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const auto &a, const auto &b) -> bool { return a.second.size() < b.second.size(); });

    for (auto folderIt = sortedFoldersToRemove.begin(); folderIt != sortedFoldersToRemove.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        // Prevents the deletion of the main sync folder in case of a false detection during synchronization
        if (folderIt->second.isEmpty())
        {
            m_foldersToRemove.remove(folderIt->first);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QPair<SyncHash, QByteArray>>::const_iterator>(folderIt));
            continue;
        }

        QString fullPath(path());
        fullPath.append(folderIt->second);

        // Folder is corrupted
        if (!QFileInfo::exists(fullPath))
        {
            m_foldersToRemove.remove(folderIt->first);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QPair<SyncHash, QByteArray>>::const_iterator>(folderIt));
            continue;
        }

        if (removeFile(folderIt->second, SyncFile::Folder) || !QDir().exists(fullPath))
        {
            hash64_t hash = hash64(folderIt->second);
            m_files.remove(hash);
            m_foldersToRemove.remove(hash);
            folderIt = sortedFoldersToRemove.erase(static_cast<QVector<QPair<SyncHash, QByteArray>>::const_iterator>(folderIt));

            QString parentPath = QFileInfo(fullPath).path();

            if (QFileInfo::exists(parentPath))
                m_foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncFolder::removeFiles
===================
*/
void SyncFolder::removeFiles()
{
    for (auto fileIt = m_filesToRemove.begin(); fileIt != m_filesToRemove.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        // Prevents the deletion of the main sync folder in case of a false detection during synchronization
        if (fileIt->isEmpty())
        {
            fileIt = m_filesToRemove.erase(static_cast<FileRemoveList::const_iterator>(fileIt));
            continue;
        }

        QString fullPath(path());
        fullPath.append(*fileIt);

        // File is corrupted
        if (!QFileInfo::exists(fullPath))
        {
            fileIt = m_filesToRemove.erase(static_cast<FileRemoveList::const_iterator>(fileIt));
            continue;
        }

        if (removeFile(*fileIt, SyncFile::File) || !QFile().exists(fullPath))
        {
            hash64_t hash = hash64(*fileIt);
            m_files.remove(hash);
            fileIt = m_filesToRemove.erase(static_cast<FileRemoveList::const_iterator>(fileIt));

            QString parentPath = QFileInfo(fullPath).path();

            if (QFileInfo::exists(parentPath))
                m_foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::createFolders
===================
*/
void SyncFolder::createFolders()
{
    for (auto folderIt = m_foldersToCreate.begin(); folderIt != m_foldersToCreate.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        if (folderIt->path.isEmpty())
        {
            folderIt = m_foldersToCreate.erase(static_cast<FolderCreateList::const_iterator>(folderIt));
            continue;
        }

        QString fullPath(path());
        fullPath.append(folderIt->path);
        QFileInfo fileInfo(fullPath);

        createParentFolders(QDir::cleanPath(fullPath).toUtf8());

        // Removes a file with the same filename first, if it exists
        if (fileInfo.exists() && fileInfo.isFile())
            removeFile(folderIt->path, SyncFile::File);

        if (QDir().mkdir(fullPath) || fileInfo.exists())
        {
            hash64_t hash = hash64(folderIt->path);
            auto newFolderIt = m_files.emplace(hash, SyncFile(SyncFile::Folder, fileInfo.lastModified()));
            newFolderIt->attributes = folderIt->attributes;
            profile().addFilePath(hash, folderIt->path);
            folderIt = m_foldersToCreate.erase(static_cast<FolderCreateList::const_iterator>(folderIt));
            FileManager::setAttribute(fullPath, newFolderIt->attributes);

            QString parentPath = QFileInfo(fullPath).path();

            if (QFileInfo::exists(parentPath))
                m_foldersToUpdate.insert(parentPath.toUtf8());
        }
        else
        {
            ++folderIt;
        }
    }
}

/*
===================
SyncFolder::copyFiles
===================
*/
void SyncFolder::copyFiles()
{
    for (auto fileIt = m_filesToCopy.begin(); fileIt != m_filesToCopy.end() && (!m_paused && active());)
    {
        if (syncApp->syncManager()->quitting())
            break;

        syncApp->throttleDown();

        // Removes from the "files to copy" list if the source file doesn't exist
        if (!QFileInfo::exists(fileIt->fromFullPath) || fileIt->toPath.isEmpty() || fileIt->fromFullPath.isEmpty())
        {
            fileIt = m_filesToCopy.erase(static_cast<FileCopyList::const_iterator>(fileIt));
            continue;
        }

        QString toFullPath(path());
        toFullPath.append(fileIt->toPath);
        hash64_t toHash = hash64(fileIt->toPath);
        const SyncFile &toFile = m_files.value(toHash);
        QFileInfo toFileInfo(toFullPath);

        if (!toFile.exists() && toFileInfo.exists())
        {
            QFileInfo fromFileInfo(fileIt->fromFullPath);

            // Aborts the copy operation if the source file is older than the destination file
            if (toFileInfo.lastModified() > fromFileInfo.lastModified())
            {
                fileIt = m_filesToCopy.erase(static_cast<FileCopyList::const_iterator>(fileIt));
                continue;
            }

            // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
            // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
            if (!caseSensitive())
            {
                QByteArray fromFileName = fileIt->fromFullPath;
                fromFileName.remove(0, fromFileName.lastIndexOf("/") + 1);

                QByteArray currentFilename = FileManager::getCurrentFileInfo(fileIt->fromFullPath).fileName().toUtf8();

                if (!currentFilename.isEmpty())
                {
                    // Aborts the copy operation if the current path and the path on a disk have different cases
                    if (currentFilename.compare(fromFileName, Qt::CaseSensitive) != 0)
                    {
                        fileIt = m_filesToCopy.erase(static_cast<FileCopyList::const_iterator>(fileIt));
                        continue;
                    }
                }
            }
        }

        createParentFolders(QDir::cleanPath(toFullPath).toUtf8());

        // Removes a file with the same filename first if exists
        if ((!profile().deltaCopying() || static_cast<quint64>(toFileInfo.size()) < profile().deltaCopyingMinSize()) && toFileInfo.exists())
            removeFile(fileIt->toPath, toFile.type);

        if (profile().copyFile(fileIt->fromFullPath, toFullPath))
        {
#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
            setFileModificationDate(toFullPath, fileIt->modifiedDate);
#endif

            // Do not reorder QQFileInfo fileInfo(fullPath) with setFileModificationDate(), as we want to get the latest modified date
            QFileInfo fileInfo(toFullPath);
            auto it = m_files.emplace(toHash, SyncFile(SyncFile::File, fileInfo.lastModified()));
            it->size = fileInfo.size();
            it->attributes = FileManager::getAttributes(toFullPath);
            profile().addFilePath(toHash, fileIt->toPath);
            fileIt = m_filesToCopy.erase(static_cast<FileCopyList::const_iterator>(fileIt));

            QByteArray parentPath = toFileInfo.path().toUtf8();

            if (QFileInfo::exists(parentPath))
                m_foldersToUpdate.insert(parentPath);
        }
        else
        {
            // Not enough disk space notification
            if (QStorageInfo(path()).bytesAvailable() < QFile(fileIt->fromFullPath).size())
            {
                QByteArray parentPath = toFileInfo.path().toUtf8();

                if (QFileInfo::exists(parentPath))
                    m_foldersToUpdate.insert(parentPath);

                QString rootPath = QStorageInfo(path()).rootPath();
                QString title(syncApp->translate("Not enough disk space on %1 (%2)").arg(QStorageInfo(path()).displayName(), rootPath));
                syncApp->tray()->notifyWithCooldown(rootPath, title, "", QSystemTrayIcon::Critical);
            }

            ++fileIt;
        }
    }
}

/*
===================
SyncFolder::updateFolderModifiedDates

Updates the modified date of parent folders as adding or removing m_files and folders changes their modified date.
This is needed for conflict resolution in cases where a file has been modified in one location and deleted in another.
SyncManager must synchronize the modified file, effectively ignoring the deletion.
===================
*/
void SyncFolder::updateFolderModifiedDates()
{
    for (auto folderIt = m_foldersToUpdate.begin(); folderIt != m_foldersToUpdate.end();)
    {
        hash64_t folderHash = hash64(QByteArray(*folderIt).remove(0, path().size()));

        auto it = m_files.find(SyncHash(folderHash));

        if (it != m_files.end())
            it->modifiedDate = QFileInfo(*folderIt).lastModified();

        folderIt = m_foldersToUpdate.erase(static_cast<FolderUpdateList::const_iterator>(folderIt));
    }
}

/*
===================
SyncFolder::setType
===================
*/
void SyncFolder::setType(Type type)
{
    if (type < TWO_WAY || type > ONE_WAY_UPDATE)
        type = TWO_WAY;

    m_type = type;
}

/*
===================
SyncFolder::setPaused
===================
*/
void SyncFolder::setPaused(bool paused)
{
    m_paused = paused;
    m_profile->updatePausedState();
}

/*
===================
SyncFolder::addConflictedFileToRename
===================
*/
void SyncFolder::addConflictedFileToRename(SyncHash hash, const QByteArray &path)
{
    auto it = m_conflictedFilesToRename.emplace(hash, path);
    it->path.squeeze();
}

/*
===================
SyncFolder::addFolderToRename
===================
*/
void SyncFolder::addFolderToRename(SyncHash hash, const QByteArray &toPath, const QByteArray &fromPath, attributes_t attributes)
{
    auto it = m_foldersToRename.emplace(hash, toPath, fromPath, attributes);
    it->toPath.squeeze();
    it->fromPath.squeeze();
}

/*
===================
SyncFolder::addFileToMove
===================
*/
void SyncFolder::addFileToMove(SyncHash hash, const QByteArray &toPath, const QByteArray &fromPath, attributes_t attributes)
{
    auto it = m_filesToMove.emplace(hash, toPath, fromPath, attributes);
    it->toPath.squeeze();
    it->fromPath.squeeze();
}

/*
===================
SyncFolder::addFolderToCreate
===================
*/
void SyncFolder::addFolderToCreate(SyncHash hash, const QByteArray &path, attributes_t attributes)
{
    auto it = m_foldersToCreate.emplace(hash, path, attributes);
    it->path.squeeze();
}

/*
===================
SyncFolder::addFileToCopy
===================
*/
void SyncFolder::addFileToCopy(SyncHash hash, const QByteArray &toPath, const QByteArray &fromFullPath, const QDateTime &modifiedDate)
{
    auto it = m_filesToCopy.emplace(hash, toPath, fromFullPath, modifiedDate);
    it->toPath.squeeze();
    it->fromFullPath.squeeze();
}

/*
===================
SyncFolder::addFolderToRemove
===================
*/
void SyncFolder::addFolderToRemove(SyncHash hash, const QByteArray &path)
{
    auto it = m_foldersToRemove.emplace(hash, path);
    it->squeeze();
}

/*
===================
SyncFolder::addFileToRemove
===================
*/
void SyncFolder::addFileToRemove(SyncHash hash, const QByteArray &path)
{
    auto it = m_filesToRemove.emplace(hash, path);
    it->squeeze();
}

/*
===================
SyncFolder::loadDatabase
===================
*/
void SyncFolder::loadDatabase(const QString &path)
{
    DEBUG_SET_TIMER();

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

    m_files.reserve(numOfFiles);

    // File data
    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(qint64) + sizeof(qint64) + sizeof(quint8) + sizeof(attributes_t);

        char buf[bufSize];

        if (stream.readRawData(&buf[0], bufSize) != bufSize)
            return;

        char *p = buf;
        hash64_t hash;
        qint64 msecs;
        qint64 size;
        SyncFile::Type type;
        bool corrupted;
        SyncFile::LockedFlag lockedFlag;
        attributes_t attributes;

        hash = *reinterpret_cast<hash64_t *>(p);
        p += sizeof(hash64_t);
        msecs = *reinterpret_cast<qint64 *>(p);
        p += sizeof(qint64);
        size = *reinterpret_cast<qint64 *>(p);
        p += sizeof(qint64);
        type = static_cast<SyncFile::Type>((*reinterpret_cast<quint8 *>(p) & 0x3));
        corrupted = static_cast<bool>(((*reinterpret_cast<quint8 *>(p) & 0xC) >> 2));
        lockedFlag = static_cast<SyncFile::LockedFlag>((*reinterpret_cast<quint8 *>(p) >> 4));
        p += sizeof(quint8);
        attributes = *reinterpret_cast<attributes_t *>(p);

        const auto it = m_files.emplace(hash, SyncFile(type, QDateTime::fromMSecsSinceEpoch(msecs)));
        it->size = size;
        it->setCorrupted(corrupted);
        it->lockedFlag = lockedFlag;
        it->attributes = attributes;
    }

    // Conflicted files to rename
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_conflictedFilesToRename.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        addConflictedFileToRename(hash64(path), path);
    }

    // Folders to rename
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_foldersToRename.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromPath;
        attributes_t attributes;

        stream >> toPath;
        stream >> fromPath;
        stream >> attributes;

        addFolderToRename(hash64(fromPath), toPath, fromPath, attributes);
    }

    // Files to move
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_filesToMove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromPath;
        attributes_t attributes;

        stream >> toPath;
        stream >> fromPath;
        stream >> attributes;

        addFileToMove(hash64(toPath), toPath, fromPath, attributes);
    }

    // Folders to create
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_foldersToCreate.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        attributes_t attributes;

        stream >> path;
        stream >> attributes;

        addFolderToCreate(hash64(path), path, attributes);
    }

    // Files to copy
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_filesToCopy.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        hash64_t hash;
        QByteArray toPath;
        QByteArray fromFullPath;
        QDateTime modifiedDate;

        stream >> hash;
        stream >> toPath;
        stream >> fromFullPath;
        stream >> modifiedDate;

        addFileToCopy(hash, toPath, fromFullPath, modifiedDate);
    }

    // Folders to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_foldersToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        addFolderToRemove(hash64(path), path);
    }

    // Files to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    m_filesToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        addFileToRemove(hash64(path), path);
    }

    optimizeMemoryUsage();
    checkForCorruptedFiles();
    checkForConflictedFiles();

    DEBUG_TIMESTAMP("Loaded from database: %s", qUtf8Printable(path));
}

/*
===================
SyncFolder::saveDatabase
===================
*/
void SyncFolder::saveDatabase(const QString &path) const
{
    DEBUG_SET_TIMER();

    QSaveFile data(path);
    if (!data.open(QIODevice::WriteOnly))
        return;

    QDataStream stream(&data);
    short version = DATABASE_VERSION;
    qsizetype size = m_files.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&version), sizeof(version)) != sizeof(version))
        return;

    // File data
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto fileIt = m_files.begin(); fileIt != m_files.end(); fileIt++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(qint64) + sizeof(qint64) + sizeof(quint8) + sizeof(attributes_t);

        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<hash64_t *>(p) = fileIt.key().data;
        p += sizeof(hash64_t);
        *reinterpret_cast<qint64 *>(p) = fileIt->modifiedDate.toMSecsSinceEpoch();
        p += sizeof(qint64);
        *reinterpret_cast<qint64 *>(p) = fileIt->size;
        p += sizeof(qint64);
        *reinterpret_cast<quint8 *>(p) = fileIt->type;
        *reinterpret_cast<quint8 *>(p) |= fileIt->corrupted() << 2;
        *reinterpret_cast<quint8 *>(p) |= fileIt->lockedFlag << 4;
        p += sizeof(quint8);
        *reinterpret_cast<attributes_t *>(p) = fileIt->attributes;

        if (stream.writeRawData(&buf[0], bufSize) != bufSize)
            return;
    }

    // Conflicted files to rename
    size = m_conflictedFilesToRename.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = m_conflictedFilesToRename.begin(); it != m_conflictedFilesToRename.end(); it++)
        stream << it.value().path;

    // Folders to rename
    size = m_foldersToRename.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = m_foldersToRename.begin(); it != m_foldersToRename.end(); it++)
    {
        stream << it.value().toPath;
        stream << it.value().fromPath;
        stream << it.value().attributes;
    }

    // Files to move
    size = m_filesToMove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = m_filesToMove.begin(); it != m_filesToMove.end(); it++)
    {
        stream << it.value().toPath;
        stream << it.value().fromPath;
        stream << it.value().attributes;
    }

    // Folders to create
    size = m_foldersToCreate.size();
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : m_foldersToCreate)
    {
        stream << fileIt.path;
        stream << fileIt.attributes;
    }

    // Files to copy
    size = m_filesToCopy.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = m_filesToCopy.begin(); it != m_filesToCopy.end(); it++)
    {
        stream << it.key().data;
        stream << it.value().toPath;
        stream << it.value().fromFullPath;
        stream << it.value().modifiedDate;
    }

    // Folders to remove
    size = m_foldersToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : m_foldersToRemove)
        stream << path;

    // Files to remove
    size = m_filesToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : m_filesToRemove)
        stream << path;

    data.commit();

    DEBUG_TIMESTAMP("Saved database to: %s", qUtf8Printable(path));
}
