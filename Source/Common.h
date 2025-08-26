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

#ifndef COMMON_H
#define COMMON_H

#include <QHash>
#include <QDir>
#include <QMessageBox>

#define DISABLE_DOUBLE_HASHING
#define PRESERVE_MODIFICATION_DATE_ON_LINUX

class QTranslator;
class QByteArray;
class SyncFile;

using hash64_t = quint64;

#ifdef Q_OS_WIN
using Attributes = qint32;
#else
using Attributes = quint32;
#endif

struct Language
{
    QLocale::Language language;
    QLocale::Country country;
    const char *filePath;
    const char *flagPath;
    const char *name;
};

struct Hash
{
    Hash(){}
    Hash(hash64_t hash) { data = hash; }
    Hash(const Hash &other) { data = other.data; }

    bool operator ==(const Hash &other) const { return data == other.data; }

    hash64_t data;
};

Q_DECL_CONST_FUNCTION inline size_t qHash(const Hash &key, size_t seed = 0) noexcept
{
#ifdef DISABLE_DOUBLE_HASHING
    Q_UNUSED(seed);
    return key.data;
#else
    return qHash(key.data, seed);
#endif
}

#ifdef DEBUG
#include <chrono>

extern std::chrono::high_resolution_clock::time_point startTime;

void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime);
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...);

#define SET_TIME(t) debugSetTime(t);
#define TIMESTAMP(t, ...) debugTimestamp(t, __VA_ARGS__);
#else

#define SET_TIME(t)
#define TIMESTAMP(t, m, ...)

#endif // DEBUG

hash64_t hash64(const QByteArray &str);
void removeDuplicatesBySizeAndDate (QHash<Hash, SyncFile *> &files);
QFileInfo getCurrentFileInfo(const QString &path);
bool questionBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButton defaultButton, QWidget *parent = nullptr);
bool intInputDialog(QWidget *parent, const QString &title, const QString &label, int &returnValue, int value = 0, int minValue = -2147483647, int maxValue = 2147483647);
bool doubleInputDialog(QWidget *parent, const QString &title, const QString &label, double &returnValue, double value = 0, double minValue = -2147483647, double maxValue = 2147483647);
bool textInputDialog(QWidget *parent, const QString &title, const QString &label, QString &returnText, const QString &text = QString());

bool isPathCaseSensitive(const QString &path);
Attributes getFileAttributes(const QString &path);
bool setFileAttribute(const QString &path, Attributes attributes);
void setHiddenFileAttribute(const QString &path, bool hidden);
bool setFileModificationDate(const QString &path, const QDateTime &dateTime);

#endif // COMMON_H
