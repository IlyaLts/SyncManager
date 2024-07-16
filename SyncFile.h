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

#ifndef SYNCFILE_H
#define SYNCFILE_H

#include <QtTypes>
#include <QByteArray>
#include <QDateTime>

/*
===========================================================

    SyncFile

===========================================================
*/
class SyncFile
{
public:

    enum Type : qint8
    {
        Unknown,
        File,
        Folder
    };

    enum LockedFlag : qint8
    {
        Unlocked,           // Files can be copied or deleted.
        Locked,             // Files are scheduled for renaming or moving, and must not be copied or deleted.
        LockedInternal      // The same as Locked, but for internal subfolders in a case-insensitive renamed folder.
    };

    SyncFile(){}
    SyncFile(QByteArray path, Type type, QDateTime time, bool updated = false, bool exists = true, bool onRestore = false) : path(path),
                                                                                                                             date(time),
                                                                                                                             type(type),
                                                                                                                             updated(updated),
                                                                                                                             exists(exists),
                                                                                                                             onRestore(onRestore){}

    bool isOlder(const SyncFile &otherFile) const;

    QByteArray path;
    QDateTime date;
    qint64 size = 0;
    Type type = Unknown;
    LockedFlag lockedFlag = Unlocked;
    bool updated = false;
    bool exists = false;
    bool onRestore = false;
    bool newlyAdded = false;
    bool toBeRemoved = false;
};

#endif // SYNCFILE_H
