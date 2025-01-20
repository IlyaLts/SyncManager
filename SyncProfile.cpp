/*
===============================================================================
    Copyright (C) 2022-2024 Ilya Lyakhovets

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

#include <QMutex>
#include "SyncProfile.h"
#include "SyncFolder.h"

/*
===================
SyncProfile::SyncProfile
===================
*/
SyncProfile::SyncProfile()
{
    syncTimer.setSingleShot(true);
    syncTimer.setTimerType(Qt::VeryCoarseTimer);
}

/*
===================
SyncProfile::operator =
===================
*/
void SyncProfile::operator =(const SyncProfile &other)
{
    folders = other.folders;
    excludeList = other.excludeList;

    syncing = other.syncing;
    paused = other.paused;
    toBeRemoved = other.toBeRemoved;
    syncEvery = other.syncEvery;
    syncTime = other.syncTime;
    lastSyncDate = other.lastSyncDate;
    name = other.name;
}

/*
===================
SyncProfile::resetLocks
===================
*/
bool SyncProfile::resetLocks()
{
    for (auto &folder : folders)
        if (!folder.filesToMove.empty() || !folder.foldersToRename.empty())
            return false;

    for (auto &folder : folders)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folder.files.begin(); fileIt != folder.files.end(); ++fileIt)
        {
            if (fileIt->lockedFlag == SyncFile::Unlocked)
                continue;

            for (auto &folder : folders)
            {
                if (folder.files.contains(fileIt.key()))
                    folder.files[fileIt.key()].lockedFlag = SyncFile::Unlocked;
            }
        }
    }

    return true;

    /*
    bool databaseChanged = false;

    for (auto &folder : folders)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folder.files.begin(); fileIt != folder.files.end(); ++fileIt)
        {
            if (fileIt->lockedFlag == SyncFile::Unlocked)
                continue;

            bool shouldReset = true;

            for (auto &otherFolder : folders)
            {
                if ((fileIt->type == SyncFile::File && otherFolder.filesToMove.contains(fileIt.key())) ||
                    (fileIt->type == SyncFile::Folder && otherFolder.foldersToRename.contains(fileIt.key())))
                {
                    shouldReset = false;
                    break;
                }
            }

            if (shouldReset)
            {
                databaseChanged = true;

                for (auto &folder : folders)
                {
                    if (folder.files.contains(fileIt.key()))
                        folder.files[fileIt.key()].lockedFlag = SyncFile::Unlocked;
                }
            }
        }
    }

    return databaseChanged;*/
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    mutex.lock();

    if (!filePaths.contains(Hash(hash)))
    {
        auto it = filePaths.insert(Hash(hash), path);
        it->squeeze();
    }

    mutex.unlock();
}

/*
===================
SyncProfile::isActive
===================
*/
bool SyncProfile::isActive() const
{
    int activeFolders = 0;

    for (auto &folder : folders)
        if (folder.isActive())
            activeFolders++;

    return !paused && !toBeRemoved && activeFolders >= 2;
}

/*
===================
SyncProfile::hasFolders
===================
*/
bool SyncProfile::hasFolders() const
{
    for (const auto &folder : folders)
        if (folder.exists)
            return true;

    return false;
}

/*
===================
SyncProfile::hasMissingFolders
===================
*/
bool SyncProfile::hasMissingFolders() const
{
    for (const auto &folder : folders)
        if (!folder.exists)
            return true;

    return false;
}
