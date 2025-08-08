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
        NewlyAdded = 0x8,
        ReadOnly = 0x10,
        AttributesUpdated = 0x20,
        Scanned = 0x40
    };

    SyncFile(){}
    SyncFile(Type type, QDateTime modifiedDate) : modifiedDate(modifiedDate), type(type), flags(Exists){}

    bool isOlder(const SyncFile &otherFile) const;
    bool hasOlderAttributes(const SyncFile &otherFile) const;
    bool hasSameSizeAndDate(const SyncFile &otherFile) const;

    void setUpdated(bool value) { flags = value ? (flags | Updated) : (flags & ~Updated); }
    void setExists(bool value) { flags = value ? (flags | Exists) : (flags & ~Exists); }
    void setNewlyAdded(bool value) { flags = value ? (flags | NewlyAdded) : (flags & ~NewlyAdded); }
    void setReadOnly(bool value) { flags = value ? (flags | ReadOnly) : (flags & ~ReadOnly); }
    void setAttributesUpdated(bool value) { flags = value ? (flags | AttributesUpdated) : (flags & ~AttributesUpdated); }
    void setScanned(bool value) { flags = value ? (flags | Scanned) : (flags & ~Scanned); }

    inline bool updated() const { return flags & Updated; }
    inline bool exists() const { return flags & Exists; }
    inline bool newlyAdded() const { return flags & NewlyAdded; }
    inline bool readOnly() const { return flags & ReadOnly; }
    inline bool attributesUpdated() const { return flags & AttributesUpdated; }
    inline bool scanned() const { return flags & Scanned; }

    QDateTime modifiedDate;
    quint64 size = 0;
    Type type = Unknown;
    LockedFlag lockedFlag = Unlocked;
    qint8 flags = 0;
    Attributes attributes = 0;
};

#endif // SYNCFILE_H
