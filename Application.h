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

#ifndef APPLICATION_H
#define APPLICATION_H

#include "Common.h"
#include <QApplication>
#include <QTranslator>

#define syncApp (static_cast<Application *>(QCoreApplication::instance()))

#define SYNCMANAGER_VERSION     "1.9.9"
#define SETTINGS_FILENAME       "Settings.ini"
#define PROFILES_FILENAME       "Profiles.ini"
#define UPDATE_DELAY            40

extern Language defaultLanguage;
extern Language languages[];

class Application : public QApplication
{
public:

    Application(int &argc, char **argv);

    void setLaunchOnStartup(bool enable);
    void setTranslator(QLocale::Language language);

    inline QString toLocalizedDateTime(const QDateTime &dateTime, const QString &format) { return locale.toString(dateTime, format); }

    static int languageCount();

private:

    QTranslator translator;
    QLocale locale;
};

#endif // APPLICATION_H
