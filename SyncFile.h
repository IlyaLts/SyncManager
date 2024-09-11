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

    enum Flags
    {
        Updated = 0x1,
        Exists = 0x2,
        OnRestore = 0x4,
        NewlyAdded = 0x8,
        ToBeRemoved = 0x10,
        AttrUpdated = 0x20
    };

    SyncFile(){}
    SyncFile(QByteArray path, Type type, QDateTime time, qint8 flags = Exists) : path(path), date(time), type(type), flags(flags){}

    bool isOlder(const SyncFile &otherFile) const;

    void setUpdated(bool value) { flags = value ? (flags | Updated) : (flags & ~Updated); }
    void setExists(bool value) { flags = value ? (flags | Exists) : (flags & ~Exists); }
    void setOnRestore(bool value) { flags = value ? (flags | OnRestore) : (flags & ~OnRestore); }
    void setNewlyAdded(bool value) { flags = value ? (flags | NewlyAdded) : (flags & ~NewlyAdded); }
    void setToBeRemoved(bool value) { flags = value ? (flags | ToBeRemoved) : (flags & ~ToBeRemoved); }
    void setAttrUpdated(bool value) { flags = value ? (flags | AttrUpdated) : (flags & ~AttrUpdated); }

    inline bool updated() const { return flags & Updated; }
    inline bool exists() const { return flags & Exists; }
    inline bool onRestore() const { return flags & OnRestore; }
    inline bool newlyAdded() const { return flags & NewlyAdded; }
    inline bool toBeRemoved() const { return flags & ToBeRemoved; }
    inline bool attrUpdated() const { return flags & AttrUpdated; }

    QByteArray path;
    QDateTime date;
    qint64 size = 0;
    Type type = Unknown;
    LockedFlag lockedFlag = Unlocked;
    qint8 flags = 0;

#ifdef Q_OS_WIN
    qint32 attr = 0;
#else
    quint32 attr = 0;
#endif
};

#endif // SYNCFILE_H
