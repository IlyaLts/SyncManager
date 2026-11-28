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

    enum Flag : quint16
    {
        // The file is actually a folder
        FolderType = 0x1,
        // Exists on the disk
        Exists = 0x2,
        // Modified date or file size were changed
        Updated = 0x4,
        // Attributes were changed
        AttributesUpdated = 0x8,
        // A new file that never existed before
        NewlyAdded = 0x10,
        // Conflict with files in other folders because the files are different
        ConflictDetected = 0x20,
        // Cannot be deleted due to the read-only attribute
        ReadOnly = 0x40,
        // Scanned during synchronization to help detect hash collisions between files and to optimize memory usage
        Scanned = 0x80,
        // Not accessible or cannot be found on the disk due to corruption
        Corrupted = 0x100,
        // The folder content was changed, which prevents this folder from being deleted
        Precedence = 0x200,
        // Scheduled for renaming or moving, and must not be copied or deleted
        Locked = 0x400,
        // The same as Locked, but for internal subfolders in a case-insensitive renamed folder
        LockedInternally = 0x800
    };

    SyncFile(){}
    SyncFile(bool folder, QDateTime modifiedDate) : modifiedDate(modifiedDate), flags(Exists){ setFolder(folder); }

    bool olderThan(const SyncFile &other) const;
    bool hasOlderAttributesThan(const SyncFile &other) const;
    bool hasSameSizeAndDate(const SyncFile &other) const;

    inline void setFolder(bool value) { setFlag(FolderType, value); }
    inline void setExists(bool value) { setFlag(Exists, value); }
    inline void setUpdated(bool value) { setFlag(Updated, value); }
    inline void setAttributesUpdated(bool value) { setFlag(AttributesUpdated, value); }
    inline void setNewlyAdded(bool value) { setFlag(NewlyAdded, value); }
    inline void setConflictDetected(bool value) { setFlag(ConflictDetected, value); }
    inline void setReadOnly(bool value) { setFlag(ReadOnly, value); }
    inline void setScanned(bool value) { setFlag(Scanned, value); }
    inline void setCorrupted(bool value) { setFlag(Corrupted, value); }
    inline void setPrecedence(bool value) { setFlag(Precedence, value); }
    void setFlag(Flag flag, bool value) { flags = value ? (flags | flag) : (flags & ~flag); }

    inline void lock() { unlock(); flags |= Locked; }
    inline void lockInternally() { unlock(); flags |= LockedInternally; }
    inline void unlock() { flags &= ~(Locked | LockedInternally); }

    inline bool folder() const { return flag(FolderType); }
    inline bool file() const { return !folder(); }
    inline bool exists() const { return flag(Exists); }
    inline bool updated() const { return flag(Updated); }
    inline bool attributesUpdated() const { return flag(AttributesUpdated); }
    inline bool newlyAdded() const { return flag(NewlyAdded); }
    inline bool conflictDetected() const { return flag(ConflictDetected); }
    inline bool readOnly() const { return flag(ReadOnly); }
    inline bool scanned() const { return flag(Scanned); }
    inline bool corrupted() const { return flag(Corrupted); }
    inline bool precedence() const { return flag(Precedence); }
    inline bool locked() const { return !flag(Locked); }
    inline bool lockedInternally() const { return !flag(LockedInternally); }
    inline bool lockedFully() const { return locked() || lockedInternally(); }
    inline bool flag(Flag flag) const { return flags & flag; }

    QDateTime modifiedDate;
    quint64 size = 0;
    quint16 flags = 0;
    attributes_t attributes = 0;
};

#endif // SYNCFILE_H
