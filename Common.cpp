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

#include <stdio.h>
#include <cstdarg>
#include <QDebug>
#include <QCryptographicHash>
#include <QDirIterator>
#include "Common.h"
#include "SyncFile.h"

#ifdef DEBUG
std::chrono::high_resolution_clock::time_point startTime;

/*
===================
debugSetTime
===================
*/
void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime)
{
    startTime = std::chrono::high_resolution_clock::now();
}

/*
===================
debugTimestamp
===================
*/
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...)
{
    char buffer[256];

    va_list ap;
    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
    qDebug("%lld ms - %s", ml.count(), buffer);
}
#endif // DEBUG

/*
===================
hash64
===================
*/
hash64_t hash64(const QByteArray &str)
{
    QByteArray hash = QCryptographicHash::hash(str, QCryptographicHash::Md5);
    QDataStream stream(hash);
    quint64 a, b;
    stream >> a >> b;
    return a ^ b;
}

/*
===================
removeDuplicateFiles

Removes duplicates from the list of files by file size and modified date
===================
*/
void removeDuplicateFiles(QHash<hash64_t, SyncFile *> &files)
{
    for (QHash<hash64_t, SyncFile *>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        bool dup = false;

        for (QHash<hash64_t, SyncFile *>::iterator anotherFileIt = files.begin(); anotherFileIt != files.end();)
        {
            bool anotherDup = true;

            if (fileIt.key() == anotherFileIt.key())
            {
                ++anotherFileIt;
                continue;
            }

            if (fileIt.value()->size != anotherFileIt.value()->size)
                anotherDup = false;

#ifndef Q_OS_LINUX
            if (fileIt.value()->date != anotherFileIt.value()->date)
                anotherDup = false;
#endif

            if (anotherDup)
            {
                dup = true;
                anotherFileIt = files.erase(static_cast<QHash<hash64_t, SyncFile *>::const_iterator>(anotherFileIt));
                continue;
            }

            ++anotherFileIt;
        }

        if (dup)
            fileIt = files.erase(static_cast<QHash<hash64_t, SyncFile *>::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
GetCurrentFileInfo

Gets the current file info of a file in a sync folder on a disk.
The only way to find out this is to use QDirIterator.
===================
*/
QFileInfo GetCurrentFileInfo(const QString &path, const QStringList &nameFilters, QDir::Filters filters)
{
    QDirIterator iterator(path, nameFilters, filters);

    if (iterator.hasNext())
    {
        iterator.next();
        return iterator.fileInfo();
    }

    return QFileInfo();
}
