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

#include "SyncManager.h"
#include "SyncFolder.h"
#include "SyncProfile.h"
#include "Application.h"
#include "Common.h"
#include <QMutex>
#include <QStandardPaths>
#include <QSettings>
#include <QStack>
#include <QRandomGenerator>

/*
===================
SyncFolder::SyncFolder
===================
*/
SyncFolder::SyncFolder(SyncProfile *profile, const QByteArray &path)
{
    m_profile = profile;
    m_path = path;

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
    QString folderKey(m_profile->name + QLatin1String("_profile/") + m_path);

    m_exists = QFileInfo::exists(m_path);
    m_lastSyncDate = settings.value(folderKey + QLatin1String("_LastSyncDate")).toDateTime();
    m_paused = settings.value(folderKey + QLatin1String("_Paused"), false).toBool();
    setType(static_cast<SyncFolder::Type>(settings.value(folderKey + QLatin1String("_SyncType"), SyncFolder::TWO_WAY).toInt()));

    if (!m_paused)
        syncApp->manager()->setPaused(false);
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
    QString folderKey(m_profile->name + QLatin1String("_profile/") + m_path);

    settings.setValue(folderKey + QLatin1String("_LastSyncDate"), m_lastSyncDate);
    settings.setValue(folderKey + QLatin1String("_Paused"), m_paused);
    settings.setValue(folderKey + QLatin1String("_SyncType"), m_type);
}

/*
===================
SyncFolder::removeSettings
===================
*/
void SyncFolder::removeSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(m_profile->name + QLatin1String("_profile/") + m_path);
}

/*
===================
SyncFolder::createParentFolders

Creates all necessary parent directories for a given file path
===================
*/
void SyncFolder::createParentFolders(QByteArray path)
{
    QStack<QString> foldersToCreate;

    while (!QDir(path = QFileInfo(path).path().toUtf8()).exists())
        foldersToCreate.append(path);

    while (!foldersToCreate.isEmpty())
    {
        syncApp->throttleCpu();

        if (QDir().mkdir(foldersToCreate.top()))
        {
            Qt::CaseSensitivity cs;

            if (m_caseSensitive)
                cs = Qt::CaseSensitive;
            else
                cs = Qt::CaseInsensitive;

            if (foldersToCreate.top().startsWith(path, cs))
            {
                QByteArray relativePath(foldersToCreate.top().toUtf8());
                relativePath.remove(0, path.size());

                hash64_t hash = hash64(relativePath);

                files.insert(hash, SyncFile(SyncFile::Folder, QFileInfo(foldersToCreate.top()).lastModified()));
                foldersToCreate.remove(hash);
                foldersToUpdate.insert(foldersToCreate.top().toUtf8());
            }
        }

        foldersToCreate.pop();
    }
}

/*
===================
SyncFolder::removeFile
===================
*/
bool SyncFolder::removeFile(const QString &path, SyncFile::Type type)
{
    QString fullPath(this->m_path);
    fullPath.append(path);

    // Prevents the deletion of a sync folder itself in case something bad happens
    if (path.isEmpty())
        return true;

    if (profile().deletionMode() == SyncProfile::MoveToTrash)
    {
        // Used to make sure that moveToTrash function really moved a file/folder
        // to the trash as it can return true even though it failed to do so
        QString pathInTrash;

        return QFile::moveToTrash(fullPath, &pathInTrash) && !pathInTrash.isEmpty();
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
                int nameEndIndex = newLocation.lastIndexOf('.');
                int slashIndex = newLocation.lastIndexOf('/');
                int backlashIndex = newLocation.lastIndexOf('\\');

                if (nameEndIndex == -1 || slashIndex >= nameEndIndex || backlashIndex >= nameEndIndex)
                    nameEndIndex = newLocation.length();

                newLocation.insert(nameEndIndex, "_" + QDateTime::currentDateTime().toString(profile().versioningPattern()));
            }
            // Adds a timestamp to a deleted file before the extension
            else if (profile().versioningFormat() == SyncProfile::FileTimestampAfter)
            {
                newLocation.append("_" + QDateTime::currentDateTime().toString(profile().versioningPattern()));

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
            if (profile().versioningFormat() == SyncProfile::LastVersion)
            {
                if (!QFile(fullPath).exists())
                    return true;

                QFile::remove(newLocation);
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
SyncFolder::cleanup

Used by one-way synchronization folders.
Removes files from a synchronization folder that do not exist in other two-way synchronization folders
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
        for (Files::iterator fileIt = files.begin(); fileIt != files.end();)
        {
            bool exists = false;
            bool hasTwoWay = false;

            if (fileIt->exists() && fileIt->type != types[i])
            {
                ++fileIt;
                continue;
            }

            for (auto otherFolderIt = profile().folders.begin(); otherFolderIt != profile().folders.end(); ++otherFolderIt)
            {
                if (&(*otherFolderIt) == this || !otherFolderIt->m_exists || !otherFolderIt->isActive())
                    continue;

                // Prevents files from being removed if there are no folders to mirror from
                if (otherFolderIt->type() == TWO_WAY)
                    hasTwoWay = true;
                else
                    continue;

                if (otherFolderIt->files.contains(fileIt.key()))
                {
                    exists = true;
                    break;
                }

                // In case there is a file, but with a different case name
                if (!otherFolderIt->m_caseSensitive && QFile(profile().filePath(fileIt.key())).exists())
                {
                    exists = true;
                    break;
                }
            }

            if (!exists && hasTwoWay)
            {
                removeFile(profile().filePath(fileIt.key()), fileIt->type);
                fileIt = files.erase(static_cast<Files::const_iterator>(fileIt));
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
    files.clear();
    foldersToRename.clear();
    filesToMove.clear();
    foldersToCreate.clear();
    filesToCopy.clear();
    foldersToRemove.clear();
    filesToRemove.clear();
    foldersToUpdate.clear();
}

/*
===================
SyncFolder::optimizeMemoryUsage
===================
*/
void SyncFolder::optimizeMemoryUsage()
{
    files.squeeze();
    filesToMove.squeeze();
    foldersToCreate.squeeze();
    filesToCopy.squeeze();
    foldersToRemove.squeeze();
    filesToRemove.squeeze();
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
        m_versioningPath.append(m_profile->name + "/");

    if (m_profile->versioningFormat() == SyncProfile::FolderTimestamp)
        m_versioningPath.append(QDateTime::currentDateTime().toString(m_profile->versioningPattern()) + "/");
}

/*
===================
SyncFolder::checkCaseSensitive
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

    QFile::remove(fullPath);
    this->m_caseSensitive = caseSensitive;
}

/*
===================
SyncFolder::saveToDatabase
===================
*/
void SyncFolder::saveToDatabase(const QString &path) const
{
    SET_TIME(startTime);

    QFile data(path);
    if (!data.open(QIODevice::WriteOnly))
        return;

    QDataStream stream(&data);
    short version = DATABASE_VERSION;
    qsizetype size = files.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&version), sizeof(version)) != sizeof(version))
        return;

    // File data
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto fileIt = files.begin(); fileIt != files.end(); fileIt++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(quint8) + sizeof(Attributes);

        char buf[bufSize];
        char *p = buf;

        *reinterpret_cast<hash64_t *>(p) = fileIt.key().data;
        p += sizeof(hash64_t);
        memcpy(p, &fileIt->modifiedDate, sizeof(QDateTime));
        p += sizeof(QDateTime);
        *reinterpret_cast<qint64 *>(p) = fileIt->size;
        p += sizeof(qint64);
        *reinterpret_cast<quint8 *>(p) = fileIt->type;
        *reinterpret_cast<quint8 *>(p) |= fileIt->lockedFlag << 4;
        p += sizeof(quint8);
        *reinterpret_cast<Attributes *>(p) = fileIt->attributes;

        if (stream.writeRawData(&buf[0], bufSize) != bufSize)
            return;
    }

    // Folders to rename
    size = foldersToRename.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = foldersToRename.begin(); it != foldersToRename.end(); it++)
    {
        stream << it.value().toPath;
        stream << it.value().fromPath;
        stream << it.value().attributes;
    }

    // Files to move
    size = filesToMove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = filesToMove.begin(); it != filesToMove.end(); it++)
    {
        stream << it.value().toPath;
        stream << it.value().fromPath;
        stream << it.value().attributes;
    }

    // Folders to create
    size = foldersToCreate.size();
    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &fileIt : foldersToCreate)
    {
        stream << fileIt.path;
        stream << fileIt.attributes;
    }

    // Files to copy
    size = filesToCopy.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (auto it = filesToCopy.begin(); it != filesToCopy.end(); it++)
    {
        stream << it.key().data;
        stream << it.value().toPath;
        stream << it.value().fromFullPath;
        stream << it.value().modifiedDate;
    }

    // Folders to remove
    size = foldersToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : foldersToRemove)
        stream << path;

    // Files to remove
    size = filesToRemove.size();

    if (stream.writeRawData(reinterpret_cast<char *>(&size), sizeof(size)) != sizeof(size))
        return;

    for (const auto &path : filesToRemove)
        stream << path;

    TIMESTAMP(startTime, "Saved database to: %s", qUtf8Printable(path));
}

/*
===================
SyncFolder::loadFromDatabase
===================
*/
void SyncFolder::loadFromDatabase(const QString &path)
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

    files.reserve(numOfFiles);

    // File data
    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        const size_t bufSize = sizeof(hash64_t) + sizeof(QDateTime) + sizeof(qint64) + sizeof(quint8) + sizeof(Attributes);

        char buf[bufSize];

        if (stream.readRawData(&buf[0], bufSize) != bufSize)
            return;

        char *p = buf;
        hash64_t hash;
        QDateTime modifiedDate;
        qint64 size;
        SyncFile::Type type;
        SyncFile::LockedFlag lockedFlag;
        Attributes attributes;

        hash = *reinterpret_cast<hash64_t *>(p);
        p += sizeof(hash64_t);
        modifiedDate = *reinterpret_cast<QDateTime *>(p);
        p += sizeof(QDateTime);
        size = *reinterpret_cast<qint64 *>(p);
        p += sizeof(qint64);
        type = static_cast<SyncFile::Type>((*reinterpret_cast<quint8 *>(p) & 0xf));
        lockedFlag = static_cast<SyncFile::LockedFlag>((*reinterpret_cast<quint8 *>(p) >> 4));
        p += sizeof(quint8);
        attributes = *reinterpret_cast<Attributes *>(p);

        const auto it = files.insert(hash, SyncFile(type, modifiedDate));
        it->size = size;
        it->lockedFlag = lockedFlag;
        it->attributes = attributes;
    }

    // Folders to rename
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    foldersToRename.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromPath;
        Attributes attributes;

        stream >> toPath;
        stream >> fromPath;
        stream >> attributes;

        const auto it = foldersToRename.insert(hash64(fromPath), {toPath, fromPath, attributes});
        it->toPath.squeeze();
        it->fromPath.squeeze();
    }

    // Files to move
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    filesToMove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray toPath;
        QByteArray fromPath;
        Attributes attributes;

        stream >> toPath;
        stream >> fromPath;
        stream >> attributes;

        const auto it = filesToMove.insert(hash64(toPath), {toPath, fromPath, attributes});
        it->toPath.squeeze();
        it->fromPath.squeeze();
    }

    // Folders to create
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    foldersToCreate.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        Attributes attributes;

        stream >> path;
        stream >> attributes;

        const auto it = foldersToCreate.insert(hash64(path), {path, attributes});
        it->path.squeeze();
    }

    // Files to copy
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    filesToCopy.reserve(numOfFiles);

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

        const auto it = filesToCopy.insert(hash, {toPath, fromFullPath, modifiedDate});
        it->toPath.squeeze();
        it->fromFullPath.squeeze();
    }

    // Folders to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    foldersToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        const auto it = foldersToRemove.insert(hash64(path), path);
        it->squeeze();
    }

    // Files to remove
    if (stream.readRawData(reinterpret_cast<char *>(&numOfFiles), sizeof(numOfFiles)) != sizeof(numOfFiles))
        return;

    filesToRemove.reserve(numOfFiles);

    for (qsizetype i = 0; i < numOfFiles; i++)
    {
        QByteArray path;
        stream >> path;

        const auto it = filesToRemove.insert(hash64(path), path);
        it->squeeze();
    }

    optimizeMemoryUsage();

    TIMESTAMP(startTime, "Loaded from database: %s", qUtf8Printable(path));
}

/*
===================
SyncFolder::removeDatabase
===================
*/
void SyncFolder::removeDatabase() const
{
    QByteArray filename = QByteArray::number(hash64(m_path));
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename + ".db");
    QDir(m_path + DATA_FOLDER_PATH).removeRecursively();
}

/*
===================
SyncFolder::removeNonExistentFiles
===================
*/
void SyncFolder::removeNonExistentFiles()
{
    for (Files::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        if (!fileIt->exists())
            fileIt = files.erase(static_cast<Files::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
SyncFolder::isActive
===================
*/
bool SyncFolder::isActive() const
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
    return !foldersToRename.isEmpty() ||
           !filesToMove.isEmpty() ||
           !foldersToCreate.isEmpty() ||
           !filesToCopy.isEmpty() ||
           !foldersToRemove.isEmpty() ||
           !filesToRemove.isEmpty();
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
        for (auto &path : foldersToRename)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : filesToMove)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : foldersToCreate)
            m_unsyncedList.append(path.path + "\n");

        for (auto &path : filesToCopy)
            m_unsyncedList.append(path.toPath + "\n");

        for (auto &path : foldersToRemove)
            m_unsyncedList.append(path + "\n");

        for (auto &path : filesToRemove)
            m_unsyncedList.append(path + "\n");
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
    removeDatabase();
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
