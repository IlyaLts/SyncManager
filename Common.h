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

#ifndef COMMON_H
#define COMMON_H

#include <QHash>
#include <QDir>
#include <QMessageBox>

class QTranslator;
class QByteArray;
class SyncFile;

using hash64_t = quint64;

extern QTranslator currentTranslator;
extern QLocale currentLocale;

#ifdef DEBUG
#include <chrono>

void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime);
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...);

#define SET_TIME(t) debugSetTime(t);
#define TIMESTAMP(t, ...) debugTimestamp(t, __VA_ARGS__);

extern std::chrono::high_resolution_clock::time_point startTime;
#else

#define SET_TIME(t)
#define TIMESTAMP(t, m, ...)

#endif // DEBUG

hash64_t hash64(const QByteArray &str);
void removeDuplicateFiles(QHash<hash64_t, SyncFile *> &files);
QFileInfo getCurrentFileInfo(const QString &path, const QStringList &nameFilters, QDir::Filters filters = QDir::NoFilter);
void setTranslator(QLocale::Language language);
bool questionBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButton defaultButton, QWidget *parent = nullptr);

#ifdef Q_OS_WIN
qint32 getFileAttributes(const QString &path);
void setFileAttribute(const QString &path, qint32 attr);
#else
quint32 getFileAttributes(const QString &path);
void setFileAttribute(const QString &path, quint32 attr);
#endif

void setHiddenFileAttribute(const QString &path, bool hidden);

#endif // COMMON_H
