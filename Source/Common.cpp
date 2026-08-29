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

#include "Common.h"
#include "SyncFile.h"
#include "Application.h"
#include <QDebug>
#include <QTranslator>
#include <QApplication>
#include <QMessageBox>
#include "xxHash/xxh3.h"

#ifdef DEBUG

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
hash64
===================
*/
hash64_t hash64(const QByteArray &str)
{
    return static_cast<hash64_t>(XXH3_64bits(str.constData(), str.size()));
}

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
    quint64 gigabytes = (size / 1024 / 1024 / 1024);

    if (gigabytes)
        return QString("%1 " + syncApp->translate("gigabytes")).arg(gigabytes);
    else if (megabytes)
        return QString("%1 " + syncApp->translate("megabytes")).arg(megabytes);
    else if (kilobytes)
        return QString("%1 " + syncApp->translate("Kilobytes")).arg(kilobytes);
    else
        return QString("%1 " + syncApp->translate("bytes")).arg(bytes);
}

/*
===================
formatTime
===================
*/
QString formatTime(quint64 time)
{
    quint64 seconds = (time / 1000) % 60;
    quint64 minutes = (time / 1000 / 60) % 60;
    quint64 hours = (time / 1000 / 60 / 60) % 24;
    quint64 days = (time / 1000 / 60 / 60 / 24);

    if (days)
    {
        float time = static_cast<float>(days) + static_cast<float>(hours) / 24.0f;
        return QString(syncApp->translate("%1 days").arg(QString::number(time, 'f', 1)));
    }
    else if (hours)
    {
        float time = static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f;
        return QString(syncApp->translate("%1 hours").arg(QString::number(time, 'f', 1)));
    }
    else if (minutes)
    {
        float time = static_cast<float>(minutes) + static_cast<float>(seconds) / 60.0f;
        return QString(syncApp->translate("%1 minutes").arg(QString::number(time, 'f', 1)));
    }
    else if (seconds)
    {
        return QString(syncApp->translate("%1 seconds").arg(seconds));
    }

    return QString("0 seconds");
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
hasMatch
===================
*/
bool hasMatch(const QStringList &list, const QString &path, bool caseSensitive)
{
    for (const QString &exclude : list)
    {
        QRegularExpression::PatternOption option = caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(exclude), option);
        re.setPattern(QRegularExpression::anchoredPattern(re.pattern()));

        if (re.match(path).hasMatch())
            return true;
    }

    return false;
}

/*
===================
addTimestampBeforeExt
===================
*/
void addTimestampBeforeExt(QString &string, const QString &pattern, const QString &separator)
{
    int nameEndIndex = string.lastIndexOf('.');
    int slashIndex = string.lastIndexOf('/');
    int backlashIndex = string.lastIndexOf('\\');

    if (nameEndIndex == -1 || slashIndex >= nameEndIndex || backlashIndex >= nameEndIndex)
        nameEndIndex = string.length();

    string.insert(nameEndIndex, separator + QDateTime::currentDateTime().toString(pattern));
}

/*
===================
addTimestampAfterExt
===================
*/
void addTimestampAfterExt(QString &string, const QString &pattern, const QString &separator)
{
    QString temp(string);

    // Adds a file extension after the timestamp
    int dotIndex = temp.lastIndexOf('.');
    int slashIndex = temp.lastIndexOf('/');
    int backlashIndex = temp.lastIndexOf('\\');

    string.append(separator + QDateTime::currentDateTime().toString(pattern));

    if (dotIndex != -1 && slashIndex < dotIndex && backlashIndex < dotIndex)
        string.append(temp.mid(dotIndex));
}
