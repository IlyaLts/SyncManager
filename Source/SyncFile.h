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

#ifndef SYNCFILE_H
#define SYNCFILE_H

#include "Common.h"
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

    enum Type : quint8
    {
        Unknown,
        File,
        Folder
    };

    enum Flag : quint8
    {
        Updated = 0x1,
        Exists = 0x2,
        NewlyAdded = 0x8,
        ReadOnly = 0x10,
        AttributesUpdated = 0x20,
        Scanned = 0x40
    };

    enum LockedFlag : quint8
    {
        Unlocked,           // Files can be copied or deleted.
        Locked,             // Files are scheduled for renaming or moving, and must not be copied or deleted.
        LockedInternal      // The same as Locked, but for internal subfolders in a case-insensitive renamed folder.
    };

    SyncFile(){}
    SyncFile(Type type, QDateTime modifiedDate) : modifiedDate(modifiedDate), type(type), flags(Exists){}

    bool isOlder(const SyncFile &otherFile) const;
    bool hasOlderAttributes(const SyncFile &otherFile) const;
    bool hasSameSizeAndDate(const SyncFile &otherFile) const;
    inline bool isFile() const { return type == File; }
    inline bool isFolder() const { return type == Folder; }
    inline bool isLocked() const { return lockedFlag != Unlocked; }

    inline void setUpdated(bool value) { setFlag(Updated, value); }
    inline void setExists(bool value) { setFlag(Exists, value); }
    inline void setNewlyAdded(bool value) { setFlag(NewlyAdded, value); }
    inline void setReadOnly(bool value) { setFlag(ReadOnly, value); }
    inline void setAttributesUpdated(bool value) { setFlag(AttributesUpdated, value); }
    inline void setScanned(bool value) { setFlag(Scanned, value); }
    void setFlag(Flag flag, bool value) { flags = value ? (flags | flag) : (flags & ~flag); }

    inline bool updated() const { return flag(Updated); }
    inline bool exists() const { return flag(Exists); }
    inline bool newlyAdded() const { return flag(NewlyAdded); }
    inline bool readOnly() const { return flag(ReadOnly); }
    inline bool attributesUpdated() const { return flag(AttributesUpdated); }
    inline bool scanned() const { return flag(Scanned); }
    inline bool flag(Flag flag) const { return flags & flag; }

    QDateTime modifiedDate;
    quint64 size = 0;
    Type type = Unknown;
    qint8 flags = 0;
    LockedFlag lockedFlag = Unlocked;
    Attributes attributes = 0;
};

#endif // SYNCFILE_H
