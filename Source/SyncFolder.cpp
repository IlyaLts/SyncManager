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
#include "SyncFolder.h"
#include "SyncProfile.h"
#include "Application.h"
#include "Common.h"
#include <QMutex>
#include <QStandardPaths>
#include <QSettings>

/*
===================
SyncFolder::loadSettings
===================
*/
void SyncFolder::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString folderKey(m_profile->name + QLatin1String("_profile/") + path);

    exists = QFileInfo::exists(path);
    lastSyncDate = settings.value(folderKey + QLatin1String("_LastSyncDate")).toDateTime();
    paused = settings.value(folderKey + QLatin1String("_Paused"), false).toBool();
    syncType = static_cast<SyncFolder::SyncType>(settings.value(folderKey + QLatin1String("_SyncType"), SyncFolder::TWO_WAY).toInt());
    PartiallySynchronized = settings.value(folderKey + QLatin1String("_PartiallySynchronized")).toBool();

    if (!paused)
        syncApp->manager()->setPaused(false);
}

/*
===================
SyncFolder::saveSettings
===================
*/
void SyncFolder::saveSettings() const
{
    if (toBeRemoved)
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString folderKey(m_profile->name + QLatin1String("_profile/") + path);

    settings.setValue(folderKey + QLatin1String("_LastSyncDate"), lastSyncDate);
    settings.setValue(folderKey + QLatin1String("_Paused"), paused);
    settings.setValue(folderKey + QLatin1String("_SyncType"), syncType);
    settings.setValue(folderKey + QLatin1String("_PartiallySynchronized"), PartiallySynchronized);
}

/*
===================
SyncFolder::removeSettings
===================
*/
void SyncFolder::removeSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(m_profile->name + QLatin1String("_profile/") + path);
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
void SyncFolder::updateVersioningPath(const SyncProfile &profile)
{
    if (profile.versioningLocation() == SyncProfile::CustomLocation)
    {
        versioningPath.assign(profile.versioningPath());
    }
    else
    {
        versioningPath.assign(this->path);
        versioningPath.remove(versioningPath.lastIndexOf("/", 1), versioningPath.size());
        versioningPath.append("_");
        versioningPath.append(profile.versioningFolder());
    }

    versioningPath.append("/");

    if (profile.versioningLocation() == SyncProfile::CustomLocation)
        versioningPath.append(profile.name + "/");

    if (profile.versioningFormat() == SyncProfile::FolderTimestamp)
        versioningPath.append(QDateTime::currentDateTime().toString(profile.versioningPattern()) + "/");
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
    QByteArray filename = QByteArray::number(hash64(path));
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename + ".db");
    QDir(path + DATA_FOLDER_PATH).removeRecursively();
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
    return !paused && !toBeRemoved && exists;
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

