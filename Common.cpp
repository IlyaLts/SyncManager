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
#include <QPushButton>
#include <stdio.h>
#include <cstdarg>

#ifdef Q_OS_WIN
#include <fileapi.h>
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
removeSimilarFiles

Removes duplicates from the list of files based on file size and modification time
===================
*/
void removeSimilarFiles(QHash<Hash, SyncFile *> &files)
{
    for (QHash<Hash, SyncFile *>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        bool dup = false;

        for (QHash<Hash, SyncFile *>::iterator anotherFileIt = ++QHash<Hash, SyncFile *>::iterator(fileIt); anotherFileIt != files.end();)
        {
            if (fileIt.value()->size != anotherFileIt.value()->size)
            {
                ++anotherFileIt;
                continue;
            }

#if defined(Q_OS_WIN) || defined(PRESERVE_MODIFICATION_DATE_ON_LINUX)
            if (fileIt.value()->modifiedDate != anotherFileIt.value()->modifiedDate)
            {
                ++anotherFileIt;
                continue;
            }
#endif

            dup = true;
            anotherFileIt = files.erase(static_cast<QHash<Hash, SyncFile *>::const_iterator>(anotherFileIt));
        }

        if (dup)
            fileIt = files.erase(static_cast<QHash<Hash, SyncFile *>::const_iterator>(fileIt));
        else
            ++fileIt;
    }
}

/*
===================
getCurrentFileInfo

Gets the current file information for a file with the correct case in a synchronized folder.

QFileInfo and QFile return a predetermined filename based on the argument provided during construction,
instead of the actual current filename on the disk. So, the only way to get the current filename is to use QDirIterator.
===================
*/
QFileInfo getCurrentFileInfo(const QString &path, const QString &name, QDir::Filters filters)
{
    QDirIterator it(path, QStringList(name), filters | QDir::Hidden | QDir::NoDotAndDotDot);

    while (it.hasNext())
    {
        it.next();
        return it.fileInfo();
    }

    // Sometimes, a file's name may resemble a wildcard (?*[]), causing QDirIterator to miss it.
    // The best current workaround is search all directories within the specified path and
    // perform a case-insensitive match against the exact filename.
    QDirIterator it2(path, {}, filters | QDir::Hidden | QDir::NoDotAndDotDot);

    while (it2.hasNext())
    {
        it2.next();

        if (it2.fileInfo().fileName().compare(name, Qt::CaseInsensitive) != 0)
            continue;

        return it2.fileInfo();
    }

    return QFileInfo();
}

/*
===================
questionBox
===================
*/
bool questionBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButton defaultButton, QWidget *parent)
{
    QMessageBox messageBox(icon, title, text, QMessageBox::NoButton, parent);

    QPushButton *yes = new QPushButton(qApp->translate("MainWindow", "&Yes"), parent);
    QPushButton *no = new QPushButton(qApp->translate("MainWindow", "&No"), parent);

    messageBox.addButton(yes, QMessageBox::YesRole);
    messageBox.addButton(no, QMessageBox::NoRole);
    messageBox.setDefaultButton(defaultButton == QMessageBox::Yes ? yes : no);

    messageBox.exec();
    return messageBox.clickedButton() == yes;
}

/*
===================
getFileAttributes
===================
*/

Attributes getFileAttributes(const QString &path)
{
#ifdef Q_OS_WIN
    return GetFileAttributesW(path.toStdWString().c_str());
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
    return SetFileAttributesW(path.toStdWString().c_str(), attributes);
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

#ifndef Q_OS_WIN
/*
===================
setFileModificationDate

Sets the modification date with a precision of 1 millisecond, which is the maximum precision of QDateTime
===================
*/
void setFileModificationDate(const QString &path, const QDateTime &dateTime)
{
#if 1
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    file.setFileTime(dateTime, QFileDevice::FileModificationTime);
    file.close();
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
#endif
