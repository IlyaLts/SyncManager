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
#include "Application.h"
#include "MainWindow.h"
#include "Common.h"
#include <QSettings>
#include <QSystemTrayIcon>
#include <QDirIterator>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>

/*
===================
SyncManager::SyncManager
===================
*/
SyncManager::SyncManager()
{
    loadSettings();
}

/*
===================
SyncManager::~SyncManager
===================
*/
SyncManager::~SyncManager()
{
    saveSettings();
}

/*
===================
SyncManager::loadSettings
===================
*/
void SyncManager::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    setPaused(settings.value("Paused", false).toBool());
}

/*
===================
SyncManager::saveSettings
===================
*/
void SyncManager::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.setValue("Paused", paused());

    settings.beginGroup("Profiles");

    for (auto &profile : m_profiles)
    {
        QStringList folderList;

        for (auto &folder : profile.folders())
            folderList.append(folder.path());

        settings.setValue(profile.name(), folderList);
    }

    settings.endGroup();
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

    // Adds the passed profile to the sync queue
    if (profile)
    {
        if (!profile->paused() && !profile->toBeRemoved())
            m_queue.enqueue(profile);
    }
    // If a profile is not passed, adds all remaining profiles to the sync queue
    else
    {
        for (auto &profile : profiles())
            if (!profile.paused() && !profile.toBeRemoved() && !m_queue.contains(&profile))
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
        emit profileStatusChanged(m_queue.head(), true);
        bool success = syncProfile(*m_queue.head());
        emit profileStatusChanged(m_queue.head(), false);

        if (!success)
        {
            m_busy = false;
            return;
        }

        m_queue.head()->setSyncHidden(false);
        m_queue.dequeue();
    }

    m_busy = false;
    emit finished();
}

/*
===================
SyncManager::updateStatus
===================
*/
void SyncManager::updateStatus()
{
    m_existingProfiles = 0;
    m_issue = false;
    m_warning = false;
    m_syncing = false;

    // Syncing status
    for (auto &profile : m_profiles)
    {
        profile.setSyncing(false);
        int existingFolders = 0;

        if (profile.toBeRemoved())
            continue;

        m_existingProfiles++;

        for (auto &folder : profile.folders())
        {
            folder.setSyncing(false);

            if (folder.toBeRemoved())
                continue;

            if (folder.exists())
                existingFolders++;
            else
                m_warning = true;

            if (m_busy && folder.active() && (folder.hasUnsyncedFiles() || folder.hasCorruptedFiles()))
            {
                m_syncing = true;
                profile.setSyncing(true);
                folder.setSyncing(true);
            }
        }
    }

    for (auto &profile : m_profiles)
    {
        m_issue = profile.hasInsufficientFolders();

        if (!m_issue)
            break;
    }

    // Number of files left to sync
    m_filesToSync = 0;

    if (m_busy)
    {
        for (const auto &folder : m_queue.head()->folders())
        {
            if (!folder.active())
                continue;

            m_filesToSync += folder.conflictedFilesToRenameSize();
            m_filesToSync += folder.foldersToRenameSize();
            m_filesToSync += folder.filesToMoveSize();
            m_filesToSync += folder.foldersToCreateSize();
            m_filesToSync += folder.filesToCopySize();
            m_filesToSync += folder.foldersToRemoveSize();
            m_filesToSync += folder.filesToRemoveSize();
        }
    }
}

/*
===================
SyncManager::purgeRemovedProfiles
===================
*/
void SyncManager::purgeRemovedProfiles()
{
    // Removes profiles/folders completely if we remove them during syncing
    for (auto profileIt = m_profiles.begin(); profileIt != m_profiles.end();)
    {
        // Profiles
        if (profileIt->toBeRemoved())
        {
            emit profileRemoved(&*profileIt);
            profileIt = m_profiles.erase(static_cast<std::list<SyncProfile>::const_iterator>(profileIt));
            continue;
        }

        // Folders
        for (auto folderIt = profileIt->folders().begin(); folderIt != profileIt->folders().end();)
        {
            if (folderIt->toBeRemoved())
                folderIt = profileIt->folders().erase(static_cast<std::list<SyncFolder>::const_iterator>(folderIt));
            else
                folderIt++;
        }

        profileIt++;
    }
}

/*
===================
SyncManager::hasManualSyncProfile
===================
*/
bool SyncManager::hasManualSyncProfile() const
{
    for (auto &profile : profiles())
    {
        if (!hasInQueue(&profile))
            continue;

        if (!profile.syncHidden())
            return true;
    }

    return false;
}

/*
===================
SyncManager::inPausedState
===================
*/
bool SyncManager::inPausedState() const
{
    if (profiles().empty())
        return m_paused;

    for (const auto &profile : profiles())
    {
        if (profile.toBeRemoved())
            continue;

        if (!profile.paused())
            return false;
    }

    return true;
}

/*
===================
SyncManager::maxInterval
===================
*/
quint64 SyncManager::maxInterval()
{
    quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentMSecsSinceEpoch();

    // Reduces the maximum value to prevent overflow when converting from milliseconds to nanoseconds
    max /= 1000000;

    return max;
}

/*
===================
SyncManager::syncProfile
===================
*/
bool SyncManager::syncProfile(SyncProfile &profile)
{
#ifdef DEBUG
    DEBUG_SET_TIMER();

    qDebug() << "=======================================";
    qDebug() << "Started syncing" << qUtf8Printable(profile.name());
    qDebug() << "=======================================";
#endif

    QElapsedTimer timer;
    timer.start();

    if (!profile.isActive())
    {
        profile.updateTimer();
        emit profileSynced(&profile);
        return true;
    }

    for (auto &folder : profile.folders())
    {
        bool existed = folder.exists();
        folder.checkExistence();

        if (!existed)
            folder.checkCaseSensitive();

        if (profile.databaseLocation() == SyncProfile::Decentralized)
            folder.loadDatebasesDecentralised();
        else
            folder.loadDatabasesLocally();
    }

    if (!scanFolders(profile))
    {
        profile.updateTimer();
        emit profileSynced(&profile);
        return false;
    }

    for (auto &folder : profile.folders())
    {
        folder.checkForCorruptedFiles();

        // Since we only synchronize mirroring folders in one direction,
        // we need to clear all file data because files that
        // no longer exist there don't get removed from the database.
        // This is just the easiest way to make mirroring work properly.
        if (folder.mirroring())
            folder.removeNonexistentFiles();

        folder.optimizeMemoryUsage();
    }

    profile.checkForChanges();
    profile.setSyncTime(profile.syncTime() + timer.elapsed());

    // Calculates average synchronization time
    if (profile.syncTime())
        profile.setSyncTime(profile.syncTime() / 2);

    printDebugInfo(profile);
    updateStatus();

    if (m_quit)
    {
        profile.updateTimer();
        emit profileSynced(&profile);
        return false;
    }

    profile.syncChanges();

    for (auto &folder : profile.folders())
    {
        folder.removeNonexistentFileData();

        // We need to check for conflicting files again after synchronization
        // to avoid incorrect partial synchronized status for folders.
        folder.checkForConflictedFiles();

        if (folder.databaseChanged())
        {
            folder.removeDatabase();

            if (profile.databaseLocation() == SyncProfile::Decentralized)
                folder.saveDatabasesDecentralised();
            else
                folder.saveDatabasesLocally();
        }

        folder.updateUnsyncedList();

        if (folder.hasCorruptedFiles())
        {
            QString deviceName(QStorageInfo(folder.path()).displayName());
            QString title(tr("Disk: %1 is corrupted. Please fix the errors.").arg(deviceName));
            syncApp->tray()->notifyWithCooldown(deviceName, title, "", QSystemTrayIcon::Critical);
        }

        folder.clearData();
        folder.optimizeMemoryUsage();

        if (folder.active())
            folder.setLastSyncDate(QDateTime::currentDateTime());
    }

    profile.clearFilePaths();
    profile.setLastSyncDate(QDateTime::currentDateTime());
    profile.updateNextSyncingTime();
    profile.updateTimer();
    updateStatus();
    emit profileSynced(&profile);

    DEBUG_TIMESTAMP("Syncing is complete.");
    return true;
}

/*
===================
SyncManager::scanFolders
===================
*/
bool SyncManager::scanFolders(SyncProfile &profile)
{
    DEBUG_SET_TIMER();

    int files = 0;
    bool result = true;
    QEventLoop scanLoop;
    QHash<SyncFolder *, QSharedPointer<QFutureWatcher<int>>> scanList;

    for (auto &folder : profile.folders())
        if (!folder.paused())
            scanList.emplace(&folder, QSharedPointer<QFutureWatcher<int>>::create());

    m_usedDevices.clear();

    while (!scanList.isEmpty())
    {
        for (auto scanListIt = scanList.begin(); scanListIt != scanList.end();)
        {
            hash64_t requiredDevice = hash64(QStorageInfo(scanListIt.key()->path()).device());

            if (!m_usedDevices.contains(requiredDevice))
            {
                m_usedDevices.insert(requiredDevice);
                SyncFolder &folder = *scanListIt.key();
                QObject::connect(scanListIt->data(), &QFutureWatcher<int>::finished, &scanLoop, &QEventLoop::quit);

                // To avoid a race condition, it is important to call this function after doing the connections
                scanListIt->data()->setFuture(QFuture(QtConcurrent::run([&]()
                {
                    int result = folder.scanFiles();
                    m_usedDevices.remove(hash64(QStorageInfo(folder.path()).device()));
                    return result;
                })));
            }

            scanListIt++;
        }

        scanLoop.exec();

        for (auto scanListIt = scanList.begin(); scanListIt != scanList.end();)
        {
            if (scanListIt->data()->future().isValid() && scanListIt->data()->isFinished())
            {
                m_usedDevices.remove(hash64(QStorageInfo(scanListIt.key()->path()).device()));

                files += scanListIt->data()->result();
                scanListIt = scanList.erase(static_cast<QHash<SyncFolder *, QSharedPointer<QFutureWatcher<int>>>::const_iterator>(scanListIt));
            }
            else
            {
                scanListIt++;
            }
        }

        if (m_quit)
        {
            result = false;
            break;
        }
    }

    DEBUG_TIMESTAMP("Found %d files in %s.", files, qUtf8Printable(profile.name()));

    return result;
}

/*
===================
SyncManager::printDebugInfo
===================
*/
void SyncManager::printDebugInfo(const SyncProfile &profile)
{
#ifdef DEBUG
    int conflictedFilesToRename = 0;
    int foldersToRename = 0;
    int filesToMove = 0;
    int foldersToCreate = 0;
    int filesToCopy = 0;
    int foldersToRemove = 0;
    int filesToRemove = 0;

    for (const auto &folder : profile.folders())
    {
        if (!folder.active())
            continue;

        conflictedFilesToRename += folder.conflictedFilesToRenameSize();
        foldersToRename += folder.foldersToRenameSize();
        filesToMove += folder.filesToMoveSize();
        foldersToCreate += folder.foldersToCreateSize();
        filesToCopy += folder.filesToCopySize();
        foldersToRemove += folder.foldersToRemoveSize();
        filesToRemove += folder.filesToRemoveSize();
    }

    if (conflictedFilesToRename || foldersToRename || filesToMove ||
        foldersToCreate || filesToCopy || foldersToRemove || filesToRemove)
    {
        qDebug() << "---------------------------------------";
        if (conflictedFilesToRename)
            qDebug() << "Conflicted files to rename:" << conflictedFilesToRename;
        if (foldersToRename)
            qDebug() << "Folders to rename:" << foldersToRename;
        if (filesToMove)
            qDebug() << "Files to move:" << filesToMove;
        if (foldersToCreate)
            qDebug() << "Folders to create:" << foldersToCreate;
        if (filesToCopy)
            qDebug() << "Files to copy:" << filesToCopy;
        if (foldersToRemove)
            qDebug() << "Folders to remove:" << foldersToRemove;
        if (filesToRemove)
            qDebug() << "Files to remove:" << filesToRemove;
        qDebug() << "---------------------------------------";
    }
#else
    Q_UNUSED(profile);
#endif
}
