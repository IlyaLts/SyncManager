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

#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include "Common.h"
#include <QTimer>

#define TEMP_EXTENSION "sm_temp"

static constexpr quint64 CopyBufferSize = 4096;

/*
===========================================================

    FileManager

===========================================================
*/
class FileManager : public QObject
{
    Q_OBJECT

public:

    FileManager();
    ~FileManager();

    void loadSettings();
    void saveSettings() const;

    bool copyFileNative(const QString &fileName, const QString &newName);
    bool copyFileDelta(const QString &fileName, const QString &newName);
    bool copyFileManual(const QString &fileName, const QString &newName);

    QAtomicInteger<quint64> *deviceRead(hash64_t deviceHash);
    inline int diskUsageResetRemainingTime() const { return m_diskUsageResetTimer.remainingTime(); }

    inline void setMaxDiskTransferRate(quint64 rate) { m_maxDiskTransferRate = rate; }
    inline quint64 maxDiskTransferRate() const { return m_maxDiskTransferRate; }

    static QFileInfo getCurrentFileInfo(const QString &path);
    static attributes_t getFileAttributes(const QString &path);
    static bool setFileAttribute(const QString &path, attributes_t attributes);
    static void setHiddenFileAttribute(const QString &path, bool hidden);
    static bool setFileModificationDate(const QString &path, const QDateTime &dateTime);
    static bool isSystemFile(const QString &path);

private Q_SLOTS:

    void resetUsedDevices();

private:

    quint64 m_maxDiskTransferRate = 0;
    QTimer m_diskUsageResetTimer;
    QHash<hash64_t, QAtomicInteger<quint64> *> m_deviceReads;
};

#endif // FILEMANAGER_H
