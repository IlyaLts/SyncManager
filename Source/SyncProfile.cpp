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

#include "Application.h"
#include "SyncManager.h"
#include "SyncProfile.h"
#include "SyncFolder.h"
#include "ProfileMenu.h"
#include <QMutex>
#include <QStandardPaths>
#include <QModelIndex>
#include <QSettings>

/*
===================
SyncProfile::SyncProfile
===================
*/
SyncProfile::SyncProfile(const QString &name, const QModelIndex &index)
{
    this->m_index = index;
    this->m_name = name;
    m_versioningFolder = "[Deletions]";
    m_versioningPattern = "yyyy_M_d_h_m_s_z";

    m_syncTimer.setSingleShot(true);
    m_syncTimer.setTimerType(Qt::VeryCoarseTimer);

    loadSettings();
}

/*
===================
SyncProfile::~SyncProfile
===================
*/
SyncProfile::~SyncProfile()
{
    if (!m_toBeRemoved)
        saveSettings();
}

/*
===================
SyncProfile::loadSettings
===================
*/
void SyncProfile::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString keyName(m_name + QLatin1String("_profile/"));

    setSyncTimeMultiplier(settings.value(keyName + "SyncTimeMultiplier", 1).toUInt());
    setSyncIntervalFixed(settings.value(keyName + "FixedSyncTime", defaultFixedInterval).toULongLong());
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setDeltaCopying(settings.value(keyName + "DeltaCopying", false).toBool());
    setVersioningPath(settings.value(keyName + "VersioningPath", "").toString());
    setDatabaseLocation(static_cast<SyncProfile::DatabaseLocation>(settings.value(keyName + "DatabaseLocation", SyncProfile::Decentralized).toInt()));
    setIgnoreHiddenFiles(settings.value(keyName + "IgnoreHiddenFiles", false).toBool());
    setFileMinSize(settings.value(keyName + "FileMinSize", 0).toULongLong());
    setFileMaxSize(settings.value(keyName + "FileMaxSize", 0).toULongLong());
    setMovedFileMinSize(settings.value(keyName + "MovedFileMinSize", MovedFilesMinSize).toULongLong());
    setDeltaCopyingMinSize(settings.value(keyName + "DeltaCopyingMinSize", DeltaCopyingMinSize).toULongLong());
    setIncludeList(settings.value(keyName + "IncludeList").toStringList());
    setExcludeList(settings.value(keyName + "ExcludeList").toStringList());
    setVersioningFolder(settings.value(keyName + "VersionFolder", "[Deletions]").toString());
    setVersioningPattern(settings.value(keyName + "VersionPattern", "yyyy_M_d_h_m_s_z").toString());

    m_lastSyncDate = settings.value(keyName + "LastSyncDate").toDateTime();
    m_paused = settings.value(keyName + "Paused", false).toBool();
    m_syncTime = settings.value(keyName + "SyncTime", 0).toULongLong();

    updateNextSyncingTime();
}

/*
===================
SyncProfile::saveSettings
===================
*/
void SyncProfile::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString profileKey(m_name + QLatin1String("_profile/"));

    settings.setValue(profileKey + "SyncingMode", syncingMode());
    settings.setValue(profileKey + "SyncTimeMultiplier", syncTimeMultiplier());
    settings.setValue(profileKey + "FixedSyncTime", syncIntervalFixed());
    settings.setValue(profileKey + "DetectMovedFiles", detectMovedFiles());
    settings.setValue(profileKey + "DeltaCopying", deltaCopying());
    settings.setValue(profileKey + "DeletionMode", deletionMode());
    settings.setValue(profileKey + "VersioningFormat", versioningFormat());
    settings.setValue(profileKey + "VersioningLocation", versioningLocation());
    settings.setValue(profileKey + "VersioningPath", versioningPath());
    settings.setValue(profileKey + "DatabaseLocation", databaseLocation());
    settings.setValue(profileKey + "IgnoreHiddenFiles", ignoreHiddenFiles());
    settings.setValue(profileKey + "FileMinSize", fileMinSize());
    settings.setValue(profileKey + "FileMaxSize", fileMaxSize());
    settings.setValue(profileKey + "MovedFileMinSize", movedFileMinSize());
    settings.setValue(profileKey + "DeltaCopyingMinSize", deltaCopyingMinSize());
    settings.setValue(profileKey + "IncludeList", includeList());
    settings.setValue(profileKey + "ExcludeList", excludeList());
    settings.setValue(profileKey + "VersionFolder", versioningFolder());
    settings.setValue(profileKey + "VersionPattern", versioningPattern());

    settings.setValue(profileKey + QLatin1String("LastSyncDate"), m_lastSyncDate);
    settings.setValue(profileKey + QLatin1String("Paused"), m_paused);
    settings.setValue(profileKey + QLatin1String("SyncTime"), m_syncTime);

    for (const auto &folder : folders())
        folder.saveSettings();
}

/*
===================
SyncProfile::removeSettings
===================
*/
void SyncProfile::removeSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(m_name + QLatin1String("_profile/"));
}

/*
===================
SyncProfile::setSyncingMode
===================
*/
void SyncProfile::setSyncingMode(SyncingMode mode)
{
    if (mode < Manual || mode > AutomaticFixed)
        mode = AutomaticAdaptive;

    m_syncingMode = mode;
}

/*
===================
SyncProfile::setSyncTimeMultiplier
===================
*/
void SyncProfile::setSyncTimeMultiplier(quint32 multiplier)
{
    m_syncTimeMultiplier = qMax(1U, multiplier);
    updateNextSyncingTime();
}

/*
===================
SyncProfile::setDeletionMode
===================
*/
void SyncProfile::setDeletionMode(DeletionMode mode)
{
    if (mode < MoveToTrash || mode > DeletePermanently)
        mode = MoveToTrash;

    m_deletionMode = mode;
}

/*
===================
SyncProfile::setDatabaseLocation
===================
*/
void SyncProfile::setDatabaseLocation(SyncProfile::DatabaseLocation location)
{
    if (location < Locally || location > Decentralized)
        location = Decentralized;

    m_databaseLocation = location;
}

/*
===================
SyncProfile::setVersioningFormat
===================
*/
void SyncProfile::setVersioningFormat(VersioningFormat format)
{
    if (format < FileTimestampBefore || format > LastVersion)
        format = FileTimestampAfter;

    m_versioningFormat = format;
}

/*
===================
SyncProfile::setVersioningLocation
===================
*/
void SyncProfile::setVersioningLocation(VersioningLocation location)
{
    if (location < LocallyNextToFolder || location > CustomLocation)
        location = LocallyNextToFolder;

    m_versioningLocation = location;
}

/*
===================
SyncProfile::setPaused
===================
*/
void SyncProfile::setPaused(bool paused)
{
    m_paused = paused;

    for (auto &folder : folders())
        folder.setPaused(paused);

    if (paused)
        m_syncTimer.stop();
    else
        updateTimer();
}

/*
===================
SyncProfile::remove
===================
*/
void SyncProfile::remove()
{
    setPaused(true);

    m_toBeRemoved = true;
    removeSettings();

    for (auto &folder : folders())
        folder.remove();
}

/*
===================
SyncProfile::updateTimer
===================
*/
void SyncProfile::updateTimer()
{
    using namespace std;
    using namespace std::chrono;

    if (!isAutomatic())
        return;

    QDateTime dateToSync(m_lastSyncDate);

    if (syncingMode() == AutomaticAdaptive)
        dateToSync = dateToSync.addMSecs(m_syncEvery);
    else if (syncingMode() == AutomaticFixed)
        dateToSync = dateToSync.addMSecs(syncIntervalFixed());

    quint64 syncTime = 0;

    if (dateToSync >= QDateTime::currentDateTime())
        syncTime = QDateTime::currentDateTime().msecsTo(dateToSync);

    if (!isActive())
        syncTime = qMax(syncTime, SyncMinDelay);

    bool profileActive = m_syncTimer.isActive();
    bool startTimer = false;

    if (!syncApp->manager()->busy() && profileActive)
        startTimer = true;

    if (!profileActive || (duration<qint64, milli>(syncTime) < m_syncTimer.remainingTime()))
        startTimer = true;

    if (startTimer)
    {
        quint64 interval = qMin(syncTime, SyncManager::maxInterval());

        m_syncTimer.setInterval(duration_cast<duration<qint64, nano>>(duration<quint64, milli>(interval)));
        m_syncTimer.start();
    }
}

/*
===================
SyncProfile::updateNextSyncingTime
===================
*/
void SyncProfile::updateNextSyncingTime()
{
    quint64 time = 0;

    if (syncingMode() == AutomaticAdaptive)
    {
        time = m_syncTime;

        // Multiplies sync time by 2
        for (quint32 i = 0; i < syncTimeMultiplier() - 1; i++)
        {
            quint64 maxInterval = SyncManager::maxInterval();
            time <<= 1;

            // If exceeds the maximum value of an qint64
            if (time > maxInterval)
            {
                time = maxInterval;
                break;
            }
        }
    }
    else if (syncingMode() == AutomaticFixed)
    {
        time = syncIntervalFixed();
    }

    m_syncEvery = qMax(time, SyncMinDelay);
}

/*
===================
SyncProfile::updatePausedState
===================
*/
void SyncProfile::updatePausedState()
{
    int unpausedFolders = 0;

    for (auto &folder : folders())
        if (!folder.paused())
            unpausedFolders++;

    m_paused = unpausedFolders < 2;

    if (m_paused)
        m_syncTimer.stop();
    else
        updateTimer();
}

/*
===================
SyncProfile::resetLocks
===================
*/
bool SyncProfile::resetLocks()
{
    QSet<hash64_t> fileHashes;
    QSet<hash64_t> folderHashes;

    for (auto &folder : folders())
    {
        for (FileMoveList::iterator it = folder.filesToMove.begin(); it != folder.filesToMove.end(); ++it)
        {
            fileHashes.insert(hash64(it.value().fromPath));
            fileHashes.insert(it.key().data);
        }

        for (FolderRenameList::iterator it = folder.foldersToRename.begin(); it != folder.foldersToRename.end(); ++it)
        {
            folderHashes.insert(it.key().data);
            folderHashes.insert(hash64(it.value().toPath));
        }
    }

    bool databaseChanged = false;

    for (auto &folder : folders())
    {
        for (Files::iterator fileIt = folder.files.begin(); fileIt != folder.files.end(); ++fileIt)
        {
            if (fileIt->lockedFlag == SyncFile::Unlocked)
                continue;

            if (fileIt->type == SyncFile::File && fileHashes.contains(fileIt.key().data))
                continue;

            if (fileIt->type == SyncFile::Folder && folderHashes.contains(fileIt.key().data))
                continue;

            if (folder.files.contains(fileIt.key()))
            {
                databaseChanged = true;
                folder.files[fileIt.key()].lockedFlag = SyncFile::Unlocked;
            }
        }
    }

    return databaseChanged;
}

/*
===================
SyncProfile::removeNonexistentFileData
===================
*/
void SyncProfile::removeNonexistentFileData()
{
    for (auto &folder : folders())
    {
        for (Files::iterator fileIt = folder.files.begin(); fileIt != folder.files.end();)
        {
            if (!fileIt->exists() || fileIt->type == SyncFile::Unknown)
                fileIt = folder.files.erase(static_cast<Files::const_iterator>(fileIt));
            else
                ++fileIt;
        }
    }
}

/*
===================
SyncProfile::saveDatabasesLocally
===================
*/
void SyncProfile::saveDatabasesLocally() const
{
    for (const auto &folder : folders())
    {
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path())) + ".db");
        folder.saveToDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncProfile::saveDatabasesDecentralised
===================
*/
void SyncProfile::saveDatabasesDecentralised() const
{
    for (auto &folder : folders())
    {
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QDir().mkdir(folder.path() + DATA_FOLDER_PATH);

        if (!QDir(folder.path() + DATA_FOLDER_PATH).exists())
            continue;

        folder.saveToDatabase(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);

#ifdef Q_OS_WIN
        setHiddenFileAttribute(QString(folder.path() + DATA_FOLDER_PATH), true);
        setHiddenFileAttribute(QString(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
#endif
    }
}

/*
===================
SyncProfile::loadDatabasesLocally
===================
*/
void SyncProfile::loadDatabasesLocally()
{
    for (auto &folder : folders())
    {
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path())) + ".db");
        folder.loadFromDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncProfile::loadDatebasesDecentralised
===================
*/
void SyncProfile::loadDatebasesDecentralised()
{
    for (auto &folder : folders())
    {
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        folder.loadFromDatabase(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
    }
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    m_mutex.lock();

    if (!m_filePaths.contains(Hash(hash)))
    {
        auto it = m_filePaths.insert(Hash(hash), path);
        it->squeeze();
    }

    m_mutex.unlock();
}

/*
===================
SyncProfile::removeUnneededFilePath
===================
*/
void SyncProfile::removeUnneededFilePath(hash64_t hash)
{
    for (auto &folder : folders())
    {
        const SyncFile &file = folder.files.value(hash);

        if (file.type == SyncFile::Unknown)
            return;

        if (!file.scanned() || !file.exists())
            return;

        if (file.updated() || file.attributesUpdated())
            return;

        if (file.newlyAdded())
            return;
    }

    m_mutex.lock();
    m_filePaths.remove(hash);

    // I don't know exactly what squeeze() does internally,
    // but calling it repeatedly significantly impacts performance.
    // So, since QHash capacity is always doubled, we should just check
    // if we really need to squeeze it. That, itself, brings performance back.
    // Also, the minimum capacity for QHash is 64 bytes.
    if (m_filePaths.size() < m_filePaths.capacity() / 2 && m_filePaths.size() > 64)
        m_filePaths.squeeze();

    m_mutex.unlock();
}

/*
===================
SyncProfile::isActive
===================
*/
bool SyncProfile::isActive() const
{
    int activeFolders = 0;

    for (const auto &folder : folders())
        if (folder.isActive())
            activeFolders++;

    return !m_paused && !m_toBeRemoved && activeFolders >= 2;
}

/*
===================
SyncProfile::isAutomatic
===================
*/
bool SyncProfile::isAutomatic() const
{
    return m_syncingMode == AutomaticAdaptive || m_syncingMode == AutomaticFixed;
}

/*
===================
SyncProfile::isTopFolderUpdated
===================
*/
bool SyncProfile::isTopFolderUpdated(const SyncFolder &folder, hash64_t hash) const
{
    QByteArray path = filePath(hash);
    return folder.files.value(hash64(QByteArray(path).remove(path.indexOf('/'), path.size()))).updated();
}

/*
===================
SyncProfile::isAnyFolderCaseSensitive
===================
*/
bool SyncProfile::isAnyFolderCaseSensitive() const
{
    for (const auto &folder : folders())
        if (folder.caseSensitive())
            return true;

    return false;
}

/*
===================
SyncProfile::countExistingFolders
===================
*/
int SyncProfile::countExistingFolders() const
{
    int n = 0;

    for (const auto &folder : folders())
        if (folder.exists())
            n++;

    return n;
}

/*
===================
SyncProfile::hasMissingFolders
===================
*/
bool SyncProfile::hasMissingFolders() const
{
    for (const auto &folder : folders())
        if (!folder.exists())
            return true;

    return false;
}

/*
===================
SyncProfile::m_partiallySynchronized
===================
*/
bool SyncProfile::partiallySynchronized() const
{
    for (const auto &folder : folders())
        if (folder.hasUnsyncedFiles())
            return true;

    return false;
}

/*
===================
SyncProfile::folderByIndex
===================
*/
SyncFolder *SyncProfile::folderByIndex(QModelIndex index)
{
    for (auto &folder : folders())
        if (folder.path() == index.data(Qt::DisplayRole).toString())
            return &folder;

    return nullptr;
}

/*
===================
SyncProfile::folderByPath
===================
*/
SyncFolder *SyncProfile::folderByPath(const QString &path)
{
    for (auto &folder : folders())
        if (folder.path().compare(path.toUtf8(), folder.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
            return &folder;

    return nullptr;
}
