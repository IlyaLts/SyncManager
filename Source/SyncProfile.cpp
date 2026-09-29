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
#include "FileManager.h"
#include "SyncProfile.h"
#include "SyncFolder.h"
#include "ProfileMenu.h"
#include <QMutex>
#include <QStandardPaths>
#include <QModelIndex>
#include <QSettings>
#include <QThread>
#include <QTemporaryFile>
#include <QDirIterator>

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

    setSyncingMode(static_cast<SyncingMode>(settings.value(keyName + "SyncingMode", AutomaticAdaptive).toInt()));
    setSyncTimeMultiplier(settings.value(keyName + "SyncTimeMultiplier", 1).toUInt());
    setSyncIntervalFixed(settings.value(keyName + "FixedSyncTime", defaultFixedInterval).toULongLong());
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setDeltaCopying(settings.value(keyName + "DeltaCopying", false).toBool());
    setDeletionMode(static_cast<DeletionMode>(settings.value(keyName + "DeletionMode", MoveToTrash).toInt()));
    setVersioningFormat(static_cast<VersioningFormat>(settings.value(keyName + "VersioningFormat", FolderTimestamp).toInt()));
    setVersioningLocation(static_cast<VersioningLocation>(settings.value(keyName + "VersioningLocation", LocallyNextToFolder).toInt()));
    setVersioningPath(settings.value(keyName + "VersioningPath", "").toString());
    setDatabaseLocation(static_cast<DatabaseLocation>(settings.value(keyName + "DatabaseLocation", Decentralized).toInt()));
    setConflictResolution(static_cast<ConflictResolution>(settings.value(keyName + "ConflictResolution", Automatically).toInt()));
    setIgnoreSystemFiles(settings.value(keyName + "IgnoreSystemFiles", true).toBool());
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
    updateTimer();
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
    settings.setValue(profileKey + "ConflictResolution", conflictResolution());
    settings.setValue(profileKey + "IgnoreSystemFiles", ignoreSystemFiles());
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

    if (mode == SyncProfile::Manual)
    {
        m_syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        updateNextSyncingTime();
        updateTimer();
    }

    emit syncingModeChanged();
    emit syncingTimeChanged();

    if (syncApp->initiated())
        saveSettings();
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
    updateTimer();
    emit syncingTimeChanged();

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setSyncIntervalFixed
===================
*/
void SyncProfile::setSyncIntervalFixed(quint64 interval)
{
    m_syncIntervalFixed = interval;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setDetectMovedFiles
===================
*/
void SyncProfile::setDetectMovedFiles(bool enable)
{
    m_detectMovedFiles = enable;

    if (syncApp->initiated())
        saveSettings();
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

    if (syncApp->initiated())
        saveSettings();
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

    if (syncApp->initiated())
        saveSettings();
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

    if (syncApp->initiated())
        saveSettings();
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

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setConflictResolution
===================
*/
void SyncProfile::setConflictResolution(ConflictResolution mode)
{
    if (mode < Automatically || mode > RenameBoth)
        mode = Automatically;

    m_conflictResolution = mode;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningPath
===================
*/
void SyncProfile::setVersioningPath(const QString &path)
{
    m_versioningPath = path;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningFolder
===================
*/
void SyncProfile::setVersioningFolder(const QString &name)
{
    m_versioningFolder = name;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningPattern
===================
*/
void SyncProfile::setVersioningPattern(const QString &pattern)
{
    m_versioningPattern = pattern;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setFileMinSize
===================
*/
void SyncProfile::setFileMinSize(quint64 size)
{
    m_fileMinSize = size;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setFileMaxSize
===================
*/
void SyncProfile::setFileMaxSize(quint64 size)
{
    m_fileMaxSize = size;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setMovedFileMinSize
===================
*/
void SyncProfile::setMovedFileMinSize(quint64 size)
{
    m_movedFileMinSize = size;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setDeltaCopyingMinSize
===================
*/
void SyncProfile::setDeltaCopyingMinSize(quint64 size)
{
    m_deltaCopyingMinSize = size;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setIncludeList
===================
*/
void SyncProfile::setIncludeList(const QStringList &list)
{
    m_includeList = list;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setExcludeList
===================
*/
void SyncProfile::setExcludeList(const QStringList &list)
{
    m_excludeList = list;

    if (syncApp->initiated())
        saveSettings();
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
SyncProfile::setIgnoreSystemFiles
===================
*/
void SyncProfile::setIgnoreSystemFiles(bool enable)
{
    m_ignoreSystemFiles = enable;

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setIgnoreHiddenFiles
===================
*/
void SyncProfile::setIgnoreHiddenFiles(bool enable)
{
    m_ignoreHiddenFiles = enable;

    if (syncApp->initiated())
        saveSettings();
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

    if (!syncApp->syncManager()->busy() && profileActive)
        startTimer = true;

    if (!profileActive || (duration<qint64, milli>(syncTime) < m_syncTimer.remainingTime()))
        startTimer = true;

    if (startTimer)
    {
        quint64 interval = qMin(syncTime, SyncManager::maxInterval());

        m_syncTimer.setInterval(duration_cast<duration<qint64, nano>>(duration<quint64, milli>(interval)));
        QMetaObject::invokeMethod(&m_syncTimer, &QChronoTimer::start, Qt::QueuedConnection);
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

    m_paused = folders().size() >= 1 && !unpausedFolders;

    if (m_paused)
        m_syncTimer.stop();
    else
        updateTimer();
}

/*
===================
SyncProfile::resetLocks

Used for resetting file locks at the end of synchronization
for files() that were moved or renamed. There might be a better way
to do that, but I couldn't figure it out for now.
===================
*/
void SyncProfile::resetLocks()
{
    QSet<hash64_t> fileHashes;
    QSet<hash64_t> folderHashes;

    for (auto &folder : folders())
    {
        for (FileMoveList::const_iterator it = folder.filesToMove().begin(); it != folder.filesToMove().end(); ++it)
        {
            fileHashes.insert(hash64(it.value().fromPath));
            fileHashes.insert(it.key().data);
        }

        for (FolderRenameList::const_iterator it = folder.foldersToRename().begin(); it != folder.foldersToRename().end(); ++it)
        {
            folderHashes.insert(it.key().data);
            folderHashes.insert(hash64(it.value().toPath));
        }
    }

    for (auto &folder : m_folders)
    {
        bool databaseChanged = false;

        for (Files::iterator fileIt = folder.files().begin(); fileIt != folder.files().end(); ++fileIt)
        {
            if (fileIt->lockedFlag == SyncFile::Unlocked)
                continue;

            if (fileIt->type == SyncFile::File && fileHashes.contains(fileIt.key().data))
                continue;

            if (fileIt->type == SyncFile::Folder && folderHashes.contains(fileIt.key().data))
                continue;

            fileIt->lockedFlag = SyncFile::Unlocked;
            databaseChanged = true;
        }

        if (databaseChanged)
            folder.setDatabaseDirty();
    }
}

/*
===================
SyncProfile::checkForChanges
===================
*/
void SyncProfile::checkForChanges()
{
    if (!isActive())
        return;

    checkForRenamedFolders();

    if (detectMovedFiles())
        checkForMovedFiles();

    checkForAddedFiles();
    checkForRemovedFiles();

    for (auto &folder : folders())
        folder.checkForConflictedFiles();

    for (auto &folder : folders())
    {
        if (folder.conflictedFilesToRenameSize() || folder.foldersToRenameSize() || folder.filesToMoveSize() ||
            folder.foldersToCreateSize() || folder.filesToCopySize() || folder.foldersToRemoveSize() || folder.filesToRemoveSize())
        {
            folder.setDatabaseDirty();
        }
    }
}

/*
===================
SyncProfile::syncChanges
===================
*/
void SyncProfile::syncChanges()
{
    synchronizeFileAttributes();

    for (auto &folder : folders())
    {
        if (!folder.active())
            continue;

        folder.renameConflictedFiles();
    }

    for (auto &folder : folders())
    {
        if (!folder.active())
            continue;

        if (deletionMode() == SyncProfile::Versioning)
            folder.updateVersioningPath();

        folder.renameFolders();
        folder.moveFiles();

        // In case we add a timestamp to files or keep the last version in the versioning folder,
        // we need to remove the files first. This is mostly because we can't move or delete a folder first
        // if it contains files and already eaxists in the versioning folder. As a result, at the end of synchronization,
        // we still have that empty folder remaining. Also, in case if we use file timestamp format
        // we want to avoid adding timestamps to each file individually after placing the parent folder
        // in the versioning folder, as it would impact performance.
        if (deletionMode() == SyncProfile::Versioning && versioningFormat() != SyncProfile::FolderTimestamp)
        {
            folder.removeFiles();
            folder.removeFolders();
        }
        else
        {
            folder.removeFolders();
            folder.removeFiles();
        }

        folder.createFolders();
        folder.copyFiles();

        // We don't want files in mirroring folders that don't exist in other folders
        if (folder.mirroring())
            folder.cleanup();

        folder.updateFolderModifiedDates();
    }
}

/*
===================
SyncProfile::copyFile

If the maximum disk transfer rate is set, then the custom implementation is used,
which allows control of the disk transfer rate. Currently, it is ~5% slower than QFile::copy.

Otherwise, we use QFile::copy, which has two implementations inside: a native one and a custom one.
It first tries to copy a file using the native, platform-dependent function. If that fails,
it then tries to copy the file using the custom implementation, which reads from the file and
writes to a new file with a new name using a buffer of 4096 bytes. If the custom implementation
is used, it does not copy the modified date, and we need to account for that. However, if we copy read-only files,
we cannot change their modification date after copying. Therefore, we will need a workaround to achieve this,
or avoid using QFile::copy altogether.
===================
*/
bool SyncProfile::copyFile(const QString &fileName, const QString &newName)
{
    if (!syncApp->fileManager()->maxDiskTransferRate() && (!deltaCopying() || static_cast<quint64>(QFileInfo(newName).size()) < deltaCopyingMinSize()))
    {
        return syncApp->fileManager()->copyNative(fileName, newName);
    }
    else
    {
        if (deltaCopying() && static_cast<quint64>(QFileInfo(newName).size()) >= deltaCopyingMinSize() && QFile::exists(newName))
            return syncApp->fileManager()->copyDelta(fileName, newName);
        else
            return syncApp->fileManager()->copyManual(fileName, newName);
    }
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    QMutexLocker locker(&m_filePathsMutex);

    if (!m_filePaths.contains(SyncHash(hash)))
    {
        auto it = m_filePaths.emplace(SyncHash(hash), path);
        it->squeeze();
    }
}

/*
===================
SyncProfile::getFilePath
===================
*/
QByteArray SyncProfile::getFilePath(SyncHash hash) const
{
    QMutexLocker locker(&m_filePathsMutex);
    return m_filePaths.value(hash);
}

/*
===================
SyncProfile::hasFilePath
===================
*/
bool SyncProfile::hasFilePath(SyncHash hash) const
{
    QMutexLocker locker(&m_filePathsMutex);
    return m_filePaths.contains(hash);
}

/*
===================
SyncProfile::clearFilePaths
===================
*/
void SyncProfile::clearFilePaths()
{
    QMutexLocker locker(&m_filePathsMutex);
    m_filePaths.clear();
}

/*
===================
SyncProfile::removeUnneededFilePath

Used for memory optimization during synchronization.

When scanning folders for files, we load all file data into memory,
which can consume a large amount of memory. Therefore, if we know that
certain files have not changed their modified data or attributes, we can unload
their data from memory earlier. This can reduce memory consumption by as much as 50%.
===================
*/
void SyncProfile::removeUnneededFilePath(hash64_t hash)
{
    std::optional<QDateTime> dateTime;

    for (auto &folder : folders())
    {
        const SyncFile &file = folder.files().value(hash);

        if (file.type == SyncFile::Unknown)
            return;

        if (!file.scanned() || !file.exists())
            return;

        if (file.updated() || file.attributesUpdated())
            return;

        if (file.conflictDetected())
            return;

        if (file.newlyAdded() || file.corrupted())
            return;

        if (dateTime && dateTime != file.modifiedDate)
            return;
        else
            dateTime = file.modifiedDate;
    }

    QMutexLocker locker(&m_filePathsMutex);
    m_filePaths.remove(hash);

    // I don't know exactly what squeeze() does internally,
    // but calling it repeatedly significantly impacts performance.
    // So, since QHash capacity is always doubled, we should just check
    // if we really need to squeeze it. That, itself, brings performance back.
    // Also, the minimum capacity for QHash is 64 bytes.
    if (m_filePaths.size() < m_filePaths.capacity() / 2 && m_filePaths.size() > 64)
        m_filePaths.squeeze();
}

/*
===================
SyncProfile::active
===================
*/
bool SyncProfile::isActive() const
{
    int activeFolders = 0;

    for (const auto &folder : folders())
        if (folder.active())
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
    QByteArray path = getFilePath(hash);
    const SyncFile &file = folder.files().value(hash64(QByteArray(path).remove(path.indexOf('/'), path.size())));

    return file.updated() && file.precedence();
}

/*
===================
SyncProfile::isAnyFolderCaseSensitive
===================
*/
bool SyncProfile::isAnyFolderCaseSensitive() const
{
    for (const auto &folder : m_folders)
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

    for (const auto &folder : m_folders)
        if (folder.exists())
            n++;

    return n;
}

/*
===================
SyncProfile::hasInsufficientFolders
===================
*/
bool SyncProfile::hasInsufficientFolders() const
{
    if (m_folders.size() < 2)
        return false;

    return countExistingFolders() < 2;
}

/*
===================
SyncProfile::hasMissingFolders
===================
*/
bool SyncProfile::hasMissingFolders() const
{
    for (const auto &folder : m_folders)
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
    for (const auto &folder : m_folders)
        if (folder.partiallySynchronized())
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
    if (!index.isValid())
        return nullptr;

    for (auto &folder : m_folders)
        if (folder.path() == index.data(Qt::DisplayRole).toString().toUtf8())
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
    for (auto &folder : m_folders)
        if (folder.path().compare(path.toUtf8(), folder.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
            return &folder;

    return nullptr;
}

/*
===================
SyncProfile::checkForRenamedFolders

Detects only changes in the case of folder names
===================
*/
void SyncProfile::checkForRenamedFolders()
{
    if (isAnyFolderCaseSensitive())
        return;

    DEBUG_SET_TIMER();

    for (auto folderIt = folders().begin(); folderIt != folders().end(); ++folderIt)
    {
        if (!folderIt->active() || !folderIt->bidirectional())
            continue;

        for (Files::iterator renamedFolderIt = folderIt->files().begin(); renamedFolderIt != folderIt->files().end(); ++renamedFolderIt)
        {
            syncApp->throttleDown();

            // Only a newly added folder can indicate that the case of folder name was changed
            if (renamedFolderIt->type != SyncFile::Folder || !renamedFolderIt->newlyAdded() || !renamedFolderIt->exists() || renamedFolderIt->corrupted())
                continue;

            // Skips if the folder is already scheduled to be moved, especially when there are three or more sync folders
            if (renamedFolderIt->lockedFlag == SyncFile::Locked)
                continue;

            QByteArray renamedFolderPath(getFilePath(renamedFolderIt.key()));
            bool abort = false;

            // Aborts if the folder doesn't exist in any other sync folder
            for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->active())
                    continue;

                QByteArray otherFolderFullPath(otherFolderIt->path());
                otherFolderFullPath.append(renamedFolderPath);

                if (!QFileInfo::exists(otherFolderFullPath))
                {
                    abort = true;
                    break;
                }
            }

            if (abort)
                continue;

            // Adds folders from other sync folders for renaming
            for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
            {
                syncApp->throttleDown();

                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->active())
                    continue;

                QByteArray otherFolderFullPath(otherFolderIt->path());
                otherFolderFullPath.append(renamedFolderPath);
                QByteArray otherCurrentFolderName;
                QByteArray otherCurrentFolderPath;

                QByteArray newFolderName(renamedFolderPath);
                newFolderName.remove(0, newFolderName.lastIndexOf("/") + 1);

                QFileInfo otherFolder = FileManager::getCurrentFileInfo(otherFolderFullPath);

                if (otherFolder.exists())
                {
                    otherCurrentFolderName = otherFolder.fileName().toUtf8();
                    otherCurrentFolderPath = otherFolder.filePath().toUtf8();
                    otherCurrentFolderPath.remove(0, otherFolderIt->path().size());
                }

                // Both folder names should differ in case
                if (otherCurrentFolderName.compare(newFolderName, Qt::CaseSensitive) == 0)
                    continue;

                hash64_t otherFolderHash = hash64(otherCurrentFolderPath);

                // Skips if the folder in another sync folder is already in the renaming list
                if (otherFolderIt->hasFolderToRename(otherFolderHash))
                    continue;

                QByteArray folderFullPath(folderIt->path());
                folderFullPath.append(renamedFolderPath);

                // Finally, adds the folder from other sync folder to the renaming list
                otherFolderIt->addFolderToRename(otherFolderHash, renamedFolderPath, otherCurrentFolderPath, FileManager::getAttributes(folderFullPath));

                // Do not reorder these, as it could lead to a crash because sometimes
                // renamedFolderIt and otherFolderHash both lead to the same sync file
                renamedFolderIt->lockedFlag = SyncFile::Locked;
                folderIt->files().remove(otherFolderHash);

                otherFolderIt->files()[otherFolderHash].lockedFlag = SyncFile::Locked;

                // Marks all subdirectories of the renamed folder in our sync folder as locked
                QDirIterator dirIterator(folderFullPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (dirIterator.hasNext())
                {
                    dirIterator.next();

                    QByteArray path(dirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path().size());
                    hash64_t hash = hash64(path);

                    if (folderIt->files().contains(hash))
                        folderIt->files()[hash].lockedFlag = SyncFile::LockedInternal;
                }

                // Marks all subdirectories of the folder that doesn't exist anymore in our sync folder as to be removed using the path() from other sync folder
                QByteArray oldFullPath(folderIt->path());
                oldFullPath.append(otherCurrentFolderPath);
                QDirIterator oldDirIterator(oldFullPath, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (oldDirIterator.hasNext())
                {
                    oldDirIterator.next();

                    QByteArray path(oldDirIterator.filePath().toUtf8());
                    path.remove(0, folderIt->path().size());
                    folderIt->files().remove(hash64(path));
                }

                // Marks all subdirectories of the current folder in other sync folder as to be locked
                QByteArray otherFullPathToRename(otherFolderIt->path());
                otherFullPathToRename.append(otherCurrentFolderPath);
                QDirIterator otherDirIterator(otherFullPathToRename, QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

                if (otherDirIterator.hasNext())
                {
                    otherDirIterator.next();

                    QByteArray path(otherDirIterator.filePath().toUtf8());
                    path.remove(0, otherFolderIt->path().size());
                    hash64_t hash = hash64(path);

                    if (otherFolderIt->files().contains(hash))
                        otherFolderIt->files()[hash].lockedFlag = SyncFile::LockedInternal;
                }
            }
        }
    }

    DEBUG_TIMESTAMP("Checked for changed case of folders().");
}

/*
===================
SyncProfile::checkForMovedFiles

Detects moved & renamed files()
===================
*/
void SyncProfile::checkForMovedFiles()
{
    DEBUG_SET_TIMER();

    for (auto folderIt = folders().begin(); folderIt != folders().end(); ++folderIt)
    {
        syncApp->throttleDown();

        if (!folderIt->active() || !folderIt->bidirectional())
            continue;

        FilePointerList missingFiles;
        FilePointerList newFiles;

        // Finds files that no longer exist in our sync folder
        for (Files::iterator missingFileIt = folderIt->files().begin(); missingFileIt != folderIt->files().end(); ++missingFileIt)
            if (missingFileIt->isFile() && !missingFileIt->exists() && missingFileIt->size >= movedFileMinSize())
                missingFiles.emplace(missingFileIt.key(), &missingFileIt.value());

        removeDuplicatesBySizeAndDate(missingFiles);

        if (missingFiles.isEmpty())
            continue;

        // Finds files that are new in our sync folder
        for (Files::iterator newFileIt = folderIt->files().begin(); newFileIt != folderIt->files().end(); ++newFileIt)
            if (newFileIt->isFile() && newFileIt->newlyAdded() && newFileIt->exists() && !newFileIt->corrupted() && newFileIt->size >= movedFileMinSize())
                newFiles.emplace(newFileIt.key(), &newFileIt.value());

        removeDuplicatesBySizeAndDate(newFiles);

        for (FilePointerList::iterator newFileIt = newFiles.begin(); newFileIt != newFiles.end(); ++newFileIt)
        {
            bool abort = false;
            const SyncFile *movedFile = nullptr;
            hash64_t movedFileHash;
            hash64_t newFileHash;
            QByteArray movedFilePath;

            // Searches for a match between a missed file and a newly added file
            for (FilePointerList::iterator missingFileIt = missingFiles.begin(); missingFileIt != missingFiles.end(); ++missingFileIt)
            {
                syncApp->throttleDown();

                if (!missingFileIt.value()->hasSameSizeAndDate(*newFileIt.value()))
                    continue;

                movedFile = &folderIt->files()[missingFileIt.key()];
                movedFileHash = missingFileIt.key().data;
                newFileHash = newFileIt.key().data;
                movedFilePath = getFilePath(movedFileHash);
                break;
            }

            if (!movedFile)
                continue;

            // Additional checks for other sync folders
            for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->active())
                    continue;

                if (!otherFolderIt->files().contains(movedFileHash))
                    continue;

                abort = true;
                const SyncFile &fileToMove = otherFolderIt->files().value(movedFileHash);

                // If the file that needs to move does not exist
                if (!fileToMove.exists())
                    break;

                // If the file that needs to move is corrupted
                if (fileToMove.corrupted())
                    break;

                if (fileToMove.readOnly())
                    break;

                // If a file already exists at the destination location in the database
                if (otherFolderIt->files().contains(newFileIt.key()))
                    break;

                QByteArray newFullPath(otherFolderIt->path());
                newFullPath.append(getFilePath(newFileIt.key()));

                // If a file already exists at the destination location on disk
                if (QFileInfo::exists(newFullPath))
                {
                    // Both paths should differ, as in the case of changing case of parent folder name, the file still exists in the destination path
                    if (folderIt->caseSensitive() || getFilePath(newFileIt.key()).compare(movedFilePath, Qt::CaseInsensitive) != 0)
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
            folderIt->files().remove(movedFileHash);

            QByteArray newPathToFile = getFilePath(newFileIt.key());
            QByteArray fullNewPathToFile = folderIt->path();
            fullNewPathToFile.append(newPathToFile);

            // Adds files for moving
            for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt)
                    continue;

                if (!otherFolderIt->active())
                    continue;

                QByteArray pathToMove(otherFolderIt->path());
                pathToMove.append(movedFilePath);

                otherFolderIt->files()[movedFileHash].lockedFlag = SyncFile::Locked;

                QByteArray fromPath = movedFilePath;

                // Removes the old file to move operation in cases where the file was not moved or
                // renamed in other sync folders, but was moved or renamed in the main sync folder again
                if (otherFolderIt->hasFileToMove(movedFileHash))
                {
                    fromPath = otherFolderIt->filesToMove()[movedFileHash].fromPath;
                    otherFolderIt->removeFileToMove(movedFileHash);
                }

                otherFolderIt->addFileToMove(newFileHash, newPathToFile, fromPath, FileManager::getAttributes(fullNewPathToFile));

#if !defined(Q_OS_WIN) && defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
                const SyncFile &fileToMove = otherFolderIt->files.value(movedFileHash);
                setFileModificationDate(pathToMove, QFileInfo(fullNewPathToFile).lastModified());
#endif
            }
        }
    }

    DEBUG_TIMESTAMP("Checked for moved/renamed files.");
}

/*
===================
SyncProfile::checkForAddedFiles

Checks for added/modified files() and folders
===================
*/
void SyncProfile::checkForAddedFiles()
{
    DEBUG_SET_TIMER();

    for (auto folderIt = folders().begin(); folderIt != folders().end(); ++folderIt)
    {
        for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists() || !otherFolderIt->bidirectional())
                continue;

            if (!folderIt->active())
                break;

            for (Files::iterator otherFileIt = otherFolderIt->files().begin(); otherFileIt != otherFolderIt->files().end(); ++otherFileIt)
            {
                syncApp->throttleDown();

                if (!otherFolderIt->active())
                    break;

                if (!otherFileIt.value().exists())
                    continue;

                const SyncFile &file = folderIt->files().value(otherFileIt.key());
                const SyncFile &otherFile = otherFileIt.value();

                if (file.corrupted() || otherFile.corrupted())
                    continue;

                if (file.isLocked() || otherFile.isLocked())
                    continue;

                bool alreadyAdded = folderIt->hasFileToCopy(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToCopyModifiedDate(otherFileIt.key()) < otherFile.modifiedDate;

                // Removes a file path from the "to remove" list if the file was updated
                if (otherFile.isFile())
                {
                    if (otherFile.updated())
                        otherFolderIt->removeFileToRemove(otherFileIt.key());

                    if (file.updated() || (otherFile.exists() && folderIt->hasFileToRemove(otherFileIt.key()) && !otherFolderIt->hasFileToRemove(otherFileIt.key())))
                        folderIt->removeFileToRemove(otherFileIt.key());
                }
                else if (otherFile.isFolder())
                {
                    if (otherFile.updated())
                        otherFolderIt->removeFolderToRemove(otherFileIt.key());

                    if (file.updated() || (otherFile.exists() && folderIt->hasFolderToRemove(otherFileIt.key()) && !otherFolderIt->hasFolderToRemove(otherFileIt.key())))
                        folderIt->removeFolderToRemove(otherFileIt.key());
                }

                // Checks for the newest version of a file in case if we have three folders or more
                if (alreadyAdded && !hasNewer)
                    continue;

                if (conflictResolution() != SyncProfile::Automatically)
                {
                    if (folderIt->files().contains(otherFileIt.key()) && file.isOlder(otherFile))
                    {
                        QByteArray path(getFilePath(otherFileIt.key()));

                        if (conflictResolution() == SyncProfile::RenameBoth)
                        {
                            folderIt->addConflictedFileToRename(otherFileIt.key(), path);
                            folderIt->removeFileToRemove(otherFileIt.key());

                            otherFolderIt->addConflictedFileToRename(otherFileIt.key(), path);
                            otherFolderIt->removeFileToRemove(otherFileIt.key());
                        }
                        // A conflict has detected notification
                        else
                        {
                            QString type("profile_" + name());
                            QString title(tr("A conflict has detected in %1 profile (%2)").arg(name(), path));
                            syncApp->tray()->notifyWithCooldown(type, title, "", QSystemTrayIcon::Warning);
                        }

                        folderIt->files()[otherFileIt.key()].setConflictDetected(true);
                        otherFolderIt->files()[otherFileIt.key()].setConflictDetected(true);
                        continue;
                    }
                }

                if ((!folderIt->files().contains(otherFileIt.key()) || file.isOlder(otherFile) ||
                     // Or if other folders has a new version of a file and our file was removed
                     (!file.exists() && (otherFile.updated() || isTopFolderUpdated(*otherFolderIt, otherFileIt.key().data)))))
                {
                    if (otherFile.isFile())
                    {
                        if (otherFolderIt->hasFileToRemove(otherFileIt.key()))
                            continue;

                        QByteArray to(getFilePath(otherFileIt.key()));
                        QByteArray from(otherFolderIt->path());
                        from.append(getFilePath(otherFileIt.key()));

                        folderIt->addFileToCopy(otherFileIt.key(), to, from, otherFile.modifiedDate);
                        folderIt->removeFileToRemove(otherFileIt.key());
                    }
                    else if (otherFile.isFolder())
                    {
                        if (otherFolderIt->hasFolderToRemove(otherFileIt.key()))
                            continue;

                        QByteArray path = getFilePath(otherFileIt.key());

                        folderIt->addFolderToCreate(otherFileIt.key(), path, otherFile.attributes);
                        folderIt->removeFolderToRemove(otherFileIt.key());
                    }
                }
            }
        }
    }

    DEBUG_TIMESTAMP("Checked for added/modified files and folders().");
}

/*
===================
SyncProfile::checkForRemovedFiles
===================
*/
void SyncProfile::checkForRemovedFiles()
{
    DEBUG_SET_TIMER();

    for (auto folderIt = folders().begin(); folderIt != folders().end(); ++folderIt)
    {
        if (!folderIt->bidirectional())
            continue;

        for (Files::iterator fileIt = folderIt->files().begin() ; fileIt != folderIt->files().end();)
        {
            syncApp->throttleDown();

            if (!folderIt->active())
                break;

            if (fileIt->exists() || fileIt->corrupted() || fileIt->isLocked())
            {
                ++fileIt;
                continue;
            }

            if (fileIt->isFile())
            {
                if (folderIt->hasFileToMove(fileIt.key()) ||
                    folderIt->hasFileToCopy(fileIt.key()) ||
                    folderIt->hasFileToRemove(fileIt.key()))
                {
                    ++fileIt;
                    continue;
                }
            }
            else if (fileIt->isFolder())
            {
                if (folderIt->hasFolderToRename(fileIt.key()) ||
                    folderIt->hasFolderToCreate(fileIt.key()) ||
                    folderIt->hasFolderToRemove(fileIt.key()))
                {
                    ++fileIt;
                    continue;
                }
            }

            // Aborts if a removed file still exists, but with a different case
            if (!folderIt->caseSensitive())
            {
                if (hasFilePath(fileIt.key()))
                {
                    QString path(folderIt->path());
                    path.append(getFilePath(fileIt.key()));

                    if (QFileInfo::exists(path))
                    {
                        ++fileIt;
                        continue;
                    }
                }
            }

            // Prevents the removal of folders that do not have a file path in any sync folders().
            // This fixes the issue where, after renaming the case of a folder containing nested folders,
            // the nested folders from their previous location would incorrectly be detected as removed.
            // This was caused by the database retaining the old hashes of the renamed nested folders().
            // This could also lead to the deletion of the sync folder itself, as the profile does not have paths under these old hashes.
            if (!hasFilePath(fileIt.key()))
            {
                ++fileIt;
                continue;
            }

            // Adds files from other folders for removal
            for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
            {
                if (folderIt == otherFolderIt || !otherFolderIt->active())
                    continue;

                if (otherFolderIt->contributing())
                    continue;

                const SyncFile &fileToRemove = otherFolderIt->files().value(fileIt.key());

                if (fileToRemove.exists())
                {
                    if (fileToRemove.readOnly())
                        continue;

                    QByteArray path = getFilePath(fileIt.key());

                    if (fileIt.value().isFolder())
                        otherFolderIt->addFolderToRemove(fileIt.key(), path);
                    else
                        otherFolderIt->addFileToRemove(fileIt.key(), path);
                }
                else
                {
                    otherFolderIt->files().remove(fileIt.key());
                }
            }

            fileIt = folderIt->files().erase(static_cast<Files::const_iterator>(fileIt));
        }
    }

    DEBUG_TIMESTAMP("Checked for removed files.");
}

/*
===================
SyncProfile::synchronizeFileAttributes
===================
*/
void SyncProfile::synchronizeFileAttributes()
{
    DEBUG_SET_TIMER();

    for (auto folderIt = folders().begin(); folderIt != folders().end(); ++folderIt)
    {
        if (!folderIt->exists())
            continue;

        for (auto otherFolderIt = folders().begin(); otherFolderIt != folders().end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists())
                continue;

            if (!folderIt->active())
                break;

            for (Files::iterator otherFileIt = otherFolderIt->files().begin(); otherFileIt != otherFolderIt->files().end(); ++otherFileIt)
            {
                if (!otherFolderIt->active())
                    break;

                if (!otherFileIt.value().exists())
                    continue;

                if (otherFileIt.value().corrupted())
                    continue;

                syncApp->throttleDown();

                const SyncFile &file = folderIt->files().value(otherFileIt.key());
                const SyncFile &otherFile = otherFileIt.value();

                if (file.isLocked() || otherFile.isLocked())
                    continue;

                if (!file.exists() || !otherFile.exists())
                    continue;

                if (file.corrupted() || otherFile.corrupted())
                    continue;

                if (file.hasOlderAttributes(otherFile))
                {
                    QByteArray filePath(getFilePath(otherFileIt.key()));

                    QByteArray from(otherFolderIt->path());
                    from.append(filePath);

                    QByteArray to(folderIt->path());
                    to.append(filePath);

                    attributes_t newAttributes = FileManager::getAttributes(from);

                    if (FileManager::setAttribute(to, newAttributes))
                    {
                        SyncFile &file = folderIt->files()[otherFileIt.key()];
                        file.attributes = newAttributes;
                        folderIt->setDatabaseDirty();
                    }
                }
            }
        }
    }

    DEBUG_TIMESTAMP("Synchronized file attributes.");
}
