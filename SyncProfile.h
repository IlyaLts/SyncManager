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

#ifndef SYNCPROFILE_H
#define SYNCPROFILE_H

#include <QMutex>
#include <QList>
#include "Common.h"

class SyncFolder;

/*
===========================================================

    SyncProfile

===========================================================
*/
class SyncProfile
{
public:

    explicit SyncProfile(bool paused) : paused(paused){}
    explicit SyncProfile(const SyncProfile &other) { *this = other; }
    explicit SyncProfile(SyncProfile &&other) { *this = other; }

    void operator =(const SyncProfile &other);

    bool resetLocks();
    void addFilePath(hash64_t hash, const QByteArray &path);
    inline void clearFilePaths() { filePaths.clear(); }

    inline QByteArray getFilePath(Hash hash) { return filePaths.value(hash); }
    inline bool hasFilePath(Hash hash) { return filePaths.contains(hash); }
    bool isActive() const;

    QList<SyncFolder> folders;
    QList<QByteArray> excludeList;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
    quint64 syncTime = 0;
    QDateTime lastSyncDate;
    QString name;

private:

    QHash<Hash, QByteArray> filePaths;
    QMutex mutex;
};

#endif // SYNCPROFILE_H
