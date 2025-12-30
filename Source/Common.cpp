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

#include "Common.h"
#include "SyncFile.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QTranslator>
#include <QApplication>
#include <QMessageBox>
#include <QRandomGenerator>
#include <stdio.h>
#include <cstdarg>

#ifdef Q_OS_WIN
#include <fileapi.h>
#include <windows.h>

#define ATTRIBUTE_VALID_SET_FLAGS 0x000031a7
#else
#include <sys/stat.h>
#include <utime.h>
#include <sys/time.h>
#endif

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
    qDebug() << ml.count() << "ms -" << buffer;
}
#endif // DEBUG

/*
===================
formatSize
===================
*/
QString formatSize(quint64 size)
{
    quint64 bytes = size % 1024;
    quint64 kilobytes = (size / 1024) % 1024;
    quint64 megabytes = (size / 1024 / 1024) % 1024;
    quint64 gigabytes = (size / 1024 / 1024/ 1024) % 1024;

    if (gigabytes)
        return QString("%1 " + qApp->translate("MainWindow", "gigabytes")).arg(gigabytes);
    else if (megabytes)
        return QString("%1 " + qApp->translate("MainWindow", "megabytes")).arg(megabytes);
    else if (kilobytes)
        return QString("%1 " + qApp->translate("MainWindow", "Kilobytes")).arg(kilobytes);
    else
        return QString("%1 " + qApp->translate("MainWindow", "bytes")).arg(bytes);
}

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
removeDuplicatesBySizeAndDate

Removes duplicates from the list of files based on file size and modification time
===================
*/
void removeDuplicatesBySizeAndDate(FilePointerList &files)
{
    for (FilePointerList::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        bool dup = false;

        for (FilePointerList::iterator anotherFileIt = ++FilePointerList::iterator(fileIt); anotherFileIt != files.end();)
        {
            if (!fileIt.value()->hasSameSizeAndDate(*anotherFileIt.value()))
            {
                ++anotherFileIt;
                continue;
            }

            dup = true;
            anotherFileIt = files.erase(static_cast<FilePointerList::const_iterator>(anotherFileIt));
        }

        if (dup)
            fileIt = files.erase(static_cast<FilePointerList::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
getCurrentFileInfo
===================
*/
QFileInfo getCurrentFileInfo(const QString &path)
{
#ifdef Q_OS_WIN
    QVector<wchar_t> buffer(MAX_PATH);
    DWORD length = GetLongPathNameW(path.toStdWString().c_str(), buffer.data(), MAX_PATH);

    if (!length)
        return QFileInfo(path);

    // If the buffer is too small to contain the path, the return value is the size
    // of the buffer that is required to hold the path and the terminating null character
    if (length > MAX_PATH)
    {
        buffer.resize(length);
        length = GetLongPathNameW(path.toStdWString().c_str(), buffer.data(), length);

        if (!length)
            return QFileInfo(path);
    }

    return QFileInfo(QString(buffer.data()));
#else
    return QFileInfo(path);
#endif
}

/*
===================
isPathCaseSensitive
===================
*/
bool isPathCaseSensitive(const QString &path)
{
    QDir dir(path);

    if (!dir.exists())
        return false;

    QString uniqueFilename;
    QString letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int numberOfLetters = letters.length();

    for (int i = 0; i < 10; ++i)
        uniqueFilename.append(letters.at(QRandomGenerator::global()->bounded(numberOfLetters)));

    uniqueFilename.append(".tmp");

    QString lowerCaseFilename = uniqueFilename.toLower();
    QString upperCaseFilename = uniqueFilename.toUpper();
    QString fullPath = dir.absoluteFilePath(uniqueFilename);
    bool caseSensitive = true;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    file.close();

    if (uniqueFilename != lowerCaseFilename)
        if (QFile::exists(dir.absoluteFilePath(lowerCaseFilename)))
            caseSensitive = false;

    if (caseSensitive && uniqueFilename != upperCaseFilename)
        if (QFile::exists(dir.absoluteFilePath(upperCaseFilename)))
            caseSensitive = false;

    QFile::remove(fullPath);
    return caseSensitive;
}

/*
===================
getFileAttributes
===================
*/
Attributes getFileAttributes(const QString &path)
{
#ifdef Q_OS_WIN
    return GetFileAttributesW(path.toStdWString().c_str()) & ATTRIBUTE_VALID_SET_FLAGS;
#else
    struct stat buf;
    stat(path.toLatin1(), &buf);
    return buf.st_mode;
#endif
}

/*
===================
setFileAttribute
===================
*/
bool setFileAttribute(const QString &path, Attributes attributes)
{
#ifdef Q_OS_WIN
    return SetFileAttributesW(path.toStdWString().c_str(), attributes & ATTRIBUTE_VALID_SET_FLAGS);
#else
    return chmod(path.toLatin1(), attributes) == 0;
#endif
}

/*
===================
setHiddenFileAttribute
===================
*/
void setHiddenFileAttribute(const QString &path, bool hidden)
{
#ifdef Q_OS_WIN
    long attr = GetFileAttributesW(path.toStdWString().c_str());
    SetFileAttributesW(path.toStdWString().c_str(), hidden ? attr | FILE_ATTRIBUTE_HIDDEN : attr & ~FILE_ATTRIBUTE_HIDDEN);
#else
    Q_UNUSED(path)
    Q_UNUSED(hidden)
#endif
}

/*
===================
setFileModificationDate

Sets the modification date with a precision of 1 millisecond, which is the maximum precision of QDateTime
===================
*/
bool setFileModificationDate(const QString &path, const QDateTime &dateTime)
{
#if 1
    QFile file(path);
    if (!file.open(QFile::Append))
        return false;

    if (!file.setFileTime(dateTime, QFileDevice::FileModificationTime))
        return false;

    file.close();
    return true;
#else
    struct stat statbuf;
    timeval times[2];

    if (stat(path.toStdString().c_str(), &statbuf) == -1)
        return;

    // New access time:
    times[0].tv_sec = statbuf.st_atime;
    times[0].tv_usec = 0;

    // New modification time:
    times[1].tv_sec = dateTime.toSecsSinceEpoch();
    times[1].tv_usec = dateTime.toMSecsSinceEpoch() % dateTime.toSecsSinceEpoch() * 1000;

    utimes(path.toStdString().c_str(), reinterpret_cast<struct timeval *>(&times));
#endif
}
