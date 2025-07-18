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

#ifndef SYNCPROFILE_H
#define SYNCPROFILE_H

#include "Common.h"
#include <QList>
#include <QChronoTimer>
#include <QMutex>
#include <QModelIndex>

class SyncFolder;

/*
===========================================================

    SyncProfile

===========================================================
*/
class SyncProfile
{
public:

    SyncProfile();
    explicit SyncProfile(const QModelIndex &index) : index(index){}
    explicit SyncProfile(const SyncProfile &other) : SyncProfile() { *this = other; }
    explicit SyncProfile(SyncProfile &&other) : SyncProfile() { *this = other; }

    void operator =(const SyncProfile &other);
    inline bool operator ==(const SyncProfile &other) { return name == other.name; }

    bool resetLocks();
    void removeInvalidFileData();
    void saveDatabasesLocally() const;
    void saveDatabasesDecentralised() const;
    void loadDatabasesLocally();
    void loadDatebasesDecentralised();
    void addFilePath(hash64_t hash, const QByteArray &path);
    inline void clearFilePaths() { filePaths.clear(); }

    inline QByteArray filePath(Hash hash) const { return filePaths.value(hash); }
    inline bool hasFilePath(Hash hash) const { return filePaths.contains(hash); }
    bool isActive() const;
    bool isTopFolderUpdated(const SyncFolder &folder, hash64_t hash) const;
    bool hasExistingFolders() const;
    bool hasMissingFolders() const;
    SyncFolder *folderByIndex(QModelIndex index);

    std::list<SyncFolder> folders;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
    bool syncHidden = false;
    quint64 syncEvery = 0;
    quint64 syncTime = 0;
    QChronoTimer syncTimer;
    QDateTime lastSyncDate;
    QString name;
    QModelIndex index;

private:

    QHash<Hash, QByteArray> filePaths;
    QMutex mutex;
};

#endif // SYNCPROFILE_H
