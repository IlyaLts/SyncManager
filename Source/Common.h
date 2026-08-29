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

#ifndef COMMON_H
#define COMMON_H

#include <QHash>
#include <QDir>
#include <QMessageBox>

using hash64_t = quint64;
using attributes_t = quint32;

struct Language
{
    QLocale::Language language;
    QLocale::Country country;
    const char *filePath;
    const char *flagPath;
    const char *name;
};

struct SyncHash
{
    SyncHash(){}
    SyncHash(hash64_t hash) { data = hash; }
    SyncHash(const SyncHash &other) { data = other.data; }

    bool operator ==(const SyncHash &other) const { return data == other.data; }

    hash64_t data;
};

class SyncFile;
using FilePointerList = QHash<SyncHash, SyncFile *>;

Q_DECL_CONST_FUNCTION inline size_t qHash(const SyncHash &key, size_t seed = 0) noexcept
{
    Q_UNUSED(seed);
    return key.data;
}

#ifdef DEBUG
#include <chrono>

void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime);
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...);

#define DEBUG_SET_TIMER() std::chrono::high_resolution_clock::time_point startTime; \
                          debugSetTime(startTime);

#define DEBUG_TIMESTAMP(...) debugTimestamp(startTime, __VA_ARGS__);
#else

#define DEBUG_SET_TIMER()
#define DEBUG_TIMESTAMP(...)

#endif // DEBUG

hash64_t hash64(const QByteArray &str);
QString formatSize(quint64 size);
QString formatTime(quint64 time);
void removeDuplicatesBySizeAndDate (FilePointerList &files);
bool hasMatch(const QStringList &list, const QString &path, bool caseSensitive);
void addTimestampBeforeExt(QString &string, const QString &pattern, const QString &separator);
void addTimestampAfterExt(QString &string, const QString &pattern, const QString &separator);

#endif // COMMON_H
