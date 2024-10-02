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
#include <QTranslator>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>

#ifdef Q_OS_WIN
#include <fileapi.h>
#else
#include <sys/stat.h>
#include <utime.h>
#include <sys/time.h>
#endif

QTranslator currentTranslator;
QLocale currentLocale;

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

Removes duplicates from the list of files based on file size and modification time (Windows)
===================
*/
void removeSimilarFiles(QHash<hash64_t, SyncFile *> &files)
{
    for (QHash<hash64_t, SyncFile *>::iterator fileIt = files.begin(); fileIt != files.end();)
    {
        bool dup = false;

        for (QHash<hash64_t, SyncFile *>::iterator anotherFileIt = ++QHash<hash64_t, SyncFile *>::iterator(fileIt); anotherFileIt != files.end();)
        {
            if (fileIt.value()->size != anotherFileIt.value()->size)
            {
                ++anotherFileIt;
                continue;
            }

#ifdef Q_OS_WIN
            if (fileIt.value()->date != anotherFileIt.value()->date)
            {
                ++anotherFileIt;
                continue;
            }
#endif

            dup = true;
            anotherFileIt = files.erase(static_cast<QHash<hash64_t, SyncFile *>::const_iterator>(anotherFileIt));
            continue;
        }

        if (dup)
            fileIt = files.erase(static_cast<QHash<hash64_t, SyncFile *>::const_iterator>(fileIt));
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
QFileInfo getCurrentFileInfo(const QString &path, const QStringList &nameFilters, QDir::Filters filters)
{
    QDirIterator iterator(path, nameFilters, filters);

    if (iterator.hasNext())
    {
        iterator.next();
        return iterator.fileInfo();
    }

    return QFileInfo();
}

/*
===================
setTranslator
===================
*/
void setTranslator(QLocale::Language language)
{
    QCoreApplication::removeTranslator(&currentTranslator);
    bool result;

    if (language == QLocale::Chinese)
    {
        result = currentTranslator.load(":/i18n/zh_CN.qm");
        currentLocale = QLocale(QLocale::Chinese, QLocale::China);
    }
    else if (language == QLocale::French)
    {
        result = currentTranslator.load(":/i18n/fr_FR.qm");
        currentLocale = QLocale(QLocale::French, QLocale::France);
    }
    else if (language == QLocale::German)
    {
        result = currentTranslator.load(":/i18n/de_DE.qm");
        currentLocale = QLocale(QLocale::German, QLocale::Germany);
    }
    else if (language == QLocale::Hindi)
    {
        result = currentTranslator.load(":/i18n/hi_IN.qm");
        currentLocale = QLocale(QLocale::Hindi, QLocale::India);
    }
    else if (language == QLocale::Italian)
    {
        result = currentTranslator.load(":/i18n/it_IT.qm");
        currentLocale = QLocale(QLocale::Italian, QLocale::Italy);
    }
    else if (language == QLocale::Japanese)
    {
        result = currentTranslator.load(":/i18n/ja_JP.qm");
        currentLocale = QLocale(QLocale::Japanese, QLocale::Japan);
    }
    else if (language == QLocale::Portuguese)
    {
        result = currentTranslator.load(":/i18n/pt_PT.qm");
        currentLocale = QLocale(QLocale::Portuguese, QLocale::Portugal);
    }
    else if (language == QLocale::Russian)
    {
        result = currentTranslator.load(":/i18n/ru_RU.qm");
        currentLocale = QLocale(QLocale::Russian, QLocale::Russia);
    }
    else if (language == QLocale::Spanish)
    {
        result = currentTranslator.load(":/i18n/es_ES.qm");
        currentLocale = QLocale(QLocale::Spanish, QLocale::Spain);
    }
    else if (language == QLocale::Ukrainian)
    {
        result = currentTranslator.load(":/i18n/uk_UA.qm");
        currentLocale = QLocale(QLocale::Ukrainian, QLocale::Ukraine);
    }
    else
    {
        result = currentTranslator.load(":/i18n/en_US.qm");
        currentLocale = QLocale(QLocale::English, QLocale::UnitedStates);
    }

    if (!result)
    {
        qWarning("Unable to load %s language", qPrintable(QLocale::languageToString(language)));
        return;
    }

    QCoreApplication::installTranslator(&currentTranslator);
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
void setFileAttribute(const QString &path, Attributes attr)
{
#ifdef Q_OS_WIN
    SetFileAttributesW(path.toStdWString().c_str(), attr);
#else
    chmod(path.toLatin1(), attr);
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
}
#endif
