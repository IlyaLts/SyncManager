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

#include "FileManager.h"
#include "Application.h"
#include "Common.h"
#include <QThread>
#include <QTemporaryFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <fileapi.h>
#include <windows.h>

#define ATTRIBUTE_VALID_SET_FLAGS 0x000031a7
#else
#include <sys/stat.h>
#include <utime.h>
#include <sys/time.h>
#endif

/*
===================
FileManager::FileManager
===================
*/
FileManager::FileManager()
{
    connect(&m_diskUsageResetTimer, &QTimer::timeout, this, &FileManager::resetUsedDevices);
    m_diskUsageResetTimer.start(1000);
    loadSettings();
}

/*
===================
FileManager::~FileManager
===================
*/
FileManager::~FileManager()
{
    qDeleteAll(m_deviceReads);
    saveSettings();
}

/*
===================
FileManager::loadSettings
===================
*/
void FileManager::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    setMaxDiskTransferRate(settings.value("MaximumDiskUsage", 0).toULongLong());
}

/*
===================
FileManager::saveSettings
===================
*/
void FileManager::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.setValue("MaximumDiskUsage", maxDiskTransferRate());
}

/*
===================
FileManager::copyNative
===================
*/
bool FileManager::copyNative(const QString &path, const QString &newPath)
{
    QFile from(path);

    if(!from.open(QFile::ReadOnly))
        return false;

    if (QFile(newPath).exists())
        return false;

    QString tempName = newPath + "." + TEMP_EXTENSION;

    if (!QFile::copy(path, tempName))
        return false;

    setModificationDate(tempName, from.fileTime(QFileDevice::FileModificationTime));
    return QFile::rename(tempName, newPath);
}

/*
===================
FileManager::copyDelta
===================
*/
bool FileManager::copyDelta(const QString &path, const QString &newPath)
{
    auto deviceRead = *FileManager::deviceRead(hash64(QStorageInfo(newPath).device()));
    QFile from(path);

    if(!from.open(QFile::ReadOnly))
        return false;

    QFile to(newPath);

    if (!to.open(QFile::ReadWrite))
        return false;

    qint64 nFrom;
    qint64 nTo;
    qint64 fromPos = 0;
    qint64 toPos = 0;
    char fromChunk[CopyBufferSize];
    char toChunk[CopyBufferSize];

    while (!from.atEnd())
    {
        if (syncApp->syncManager()->quitting())
            return false;

        nFrom = from.read(fromChunk, sizeof(fromChunk));
        nTo = to.read(toChunk, sizeof(toChunk));

        if (nFrom <= 0)
            break;

        deviceRead += nFrom + nTo;

        syncApp->throttleDown();

        while (m_maxDiskTransferRate && deviceRead >= m_maxDiskTransferRate && !syncApp->syncManager()->quitting())
        {
            int sleep = diskUsageResetRemainingTime();

            if (sleep < 0)
                sleep = 0;

            QThread::msleep(sleep);
        }

        if (nFrom != nTo || memcmp(fromChunk, toChunk, CopyBufferSize) != 0)
        {
            to.seek(fromPos);
            to.write(fromChunk, nFrom);
        }

        fromPos += nFrom;
        toPos += nTo;
    }

    // Trims trailing data from the destination if the source file has less data
    if (from.size() != to.size())
        to.resize(from.size());

    to.setFileTime(from.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);

    if (!to.setPermissions(from.permissions()))
        return false;

    return true;
}

/*
===================
FileManager::copyManual
===================
*/
bool FileManager::copyManual(const QString &path, const QString &newPath)
{
    auto deviceRead = *FileManager::deviceRead(hash64(QStorageInfo(newPath).device()));
    QFile from(path);

    if(!from.open(QFile::ReadOnly))
        return false;

    if (QFile(newPath).exists())
        return false;

    QString fileTemplate = QString("%1/.XXXXXX.") + TEMP_EXTENSION;
    QTemporaryFile tempFile(fileTemplate.arg(QFileInfo(newPath).path()));

    if (!tempFile.open())
    {
        tempFile.setFileTemplate(fileTemplate.arg(QDir::tempPath()));

        if (!tempFile.open())
            return false;
    }

    char chunkSize[CopyBufferSize];
    qint64 totalRead = 0;

    while (!from.atEnd())
    {
        if (syncApp->syncManager()->quitting())
            return false;

        qint64 in = from.read(chunkSize, sizeof(chunkSize));

        if (in <= 0)
            break;

        totalRead += in;
        deviceRead += in;

        syncApp->throttleDown();

        while (m_maxDiskTransferRate && deviceRead >= m_maxDiskTransferRate && !syncApp->syncManager()->quitting())
        {
            int sleep = diskUsageResetRemainingTime();

            if (sleep < 0)
                sleep = 0;

            QThread::msleep(sleep);
        }

        if (in != tempFile.write(chunkSize, in))
            return false;
    }

    if (totalRead != from.size())
        return false;

    // It must be done before renaming, otherwise it won't work.
    tempFile.setFileTime(from.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);

    if (!tempFile.rename(newPath))
        return false;

    if (!tempFile.setPermissions(from.permissions()))
        return false;

    tempFile.setAutoRemove(false);
    return true;
}

/*
===================
FileManager::moveToTrash
===================
*/
bool FileManager::moveToTrash(const QString &path)
{
    // Used to make sure that moveToTrash function really moved a file/folder
    // to the trash as it can return true even though it failed to do so
    QString pathInTrash;

    return QFile::moveToTrash(path, &pathInTrash) && !pathInTrash.isEmpty();
}

/*
===================
FileManager::remove
===================
*/
bool FileManager::remove(const QString &path)
{
    QFileInfo fileInfo(path);

    if (!fileInfo.exists())
        return true;

    if (fileInfo.isDir())
        return QDir(path).removeRecursively();
    else
        return QFile::remove(path);
}

/*
===================
FileManager::deviceRead
===================
*/
QAtomicInteger<quint64> *FileManager::deviceRead(hash64_t deviceHash)
{
    if (!m_deviceReads.contains(deviceHash))
        return m_deviceReads.insert(deviceHash, new QAtomicInteger<quint64>(0)).value();

    return m_deviceReads.value(deviceHash);
}

/*
===================
FileManager::getCurrentFileInfo

Gets the fileinfo with the current filepath on the disk

If we just use QFileInfo with our provided path, QFileInfo will return the filepath
with the same filepath case as the provided path, even though it actually differs on the disk.
So, this is a workaround for this, where we get the correct file path with proper case first,
and only then use it with QFileInfo.

The question is should we get the current filename instead of the whole file path.
If so, then we can get it using FILE_NAME_OPENED instead of FILE_NAME_NORMALIZED argument

Or use the following way:

WIN32_FIND_DATAW findData;

HANDLE hFind = FindFirstFileW(path.toStdWString().c_str(), &findData);
if (hFind == INVALID_HANDLE_VALUE)
    return QFileInfo(path);

FindClose(hFind);

QString filename(findData.cFileName);

===================
*/
QFileInfo FileManager::getCurrentFileInfo(const QString &path)
{
#ifdef Q_OS_WIN
    QVector<wchar_t> buffer(MAX_PATH);
    DWORD length;

    HANDLE handle = CreateFileW(path.toStdWString().c_str(),
                                0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS,
                                NULL);

    if (handle == INVALID_HANDLE_VALUE)
        return QFileInfo(path);

    length = GetFinalPathNameByHandleW(handle, buffer.data(), MAX_PATH, FILE_NAME_NORMALIZED);

    // If the buffer is too small to contain the path, the return value is the size
    // of the buffer that is required to hold the path and the terminating null character
    if (length > MAX_PATH)
    {
        buffer.resize(length);
        length = GetFinalPathNameByHandleW(handle, buffer.data(), MAX_PATH, FILE_NAME_NORMALIZED);
    }

    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);

    QString curPath = QString::fromWCharArray(buffer.data(), length);

    // Removes long path prefix
    if (curPath.startsWith("\\\\?\\"))
        curPath.remove(0, 4);

    return QFileInfo(curPath);
#else
    return QFileInfo(path);
#endif
}

/*
===================
FileManager::getAttributes
===================
*/
attributes_t FileManager::getAttributes(const QString &path)
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
FileManager::setAttribute
===================
*/
bool FileManager::setAttribute(const QString &path, attributes_t attributes)
{
#ifdef Q_OS_WIN
    return SetFileAttributesW(path.toStdWString().c_str(), attributes & ATTRIBUTE_VALID_SET_FLAGS);
#else
    return chmod(path.toLatin1(), attributes) == 0;
#endif
}

/*
===================
FileManager::setHiddenAttribute
===================
*/
void FileManager::setHiddenAttribute(const QString &path, bool hidden)
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
FileManager::setModificationDate

Sets the modification date with a precision of 1 millisecond, which is the maximum precision of QDateTime
===================
*/
bool FileManager::setModificationDate(const QString &path, const QDateTime &dateTime)
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

/*
===================
FileManager::isSystem
===================
*/
bool FileManager::isSystem(const QString &path)
{
#ifdef Q_OS_WIN
    DWORD attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));

    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_SYSTEM))
        return true;
#else
#endif

    return false;
}

/*
===================
FileManager::resetUsedDevices
===================
*/
void FileManager::resetUsedDevices()
{
    for (auto &device : m_deviceReads)
        device = 0;
}
