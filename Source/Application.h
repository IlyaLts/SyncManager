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
#include <QNetworkAccessManager>
#include <QTimer>

#define syncApp (static_cast<Application *>(QCoreApplication::instance()))

#define SYNCMANAGER_VERSION     "2.2"
#define SETTINGS_FILENAME       "Settings.ini"
#define PROFILES_FILENAME       "Profiles.ini"
#define USER_MANUAL_PATH        "SyncManagerUserManual.pdf"
#define BUG_TRACKER_URL         "https://github.com/IlyaLts/SyncManager/issues"
#define LATEST_RELEASE_API_URL  "https://api.github.com/repos/IlyaLts/SyncManager/releases/latest"
#define LATEST_RELEASE_URL      "https://github.com/IlyaLts/SyncManager/releases/latest"
#define CHECK_FOR_UPDATE_TIME   1000 * 60 * 60 * 24
#define UPDATE_TIME             40

#define DISABLE_DOUBLE_HASHING
#define PRESERVE_MODIFICATION_DATE_ON_LINUX

extern Language defaultLanguage;
extern Language languages[];

class Application : public QApplication
{
    Q_OBJECT

public:

    Application(int &argc, char **argv);

    void setLaunchOnStartup(bool enable);
    void setTranslator(QLocale::Language language);
    void setCheckForUpdates(bool enable);
    void checkForUpdates();

    inline bool updateAvailable() const { return m_updateAvailable; };
    inline QString toLocalizedDateTime(const QDateTime &dateTime, const QString &format) { return m_locale.toString(dateTime, format); }

    static int languageCount();

Q_SIGNALS:

    void updateFound();

private Q_SLOTS:

    void onUpdateReply(QNetworkReply *reply);

private:

    QTranslator m_translator;
    QLocale m_locale;

    bool m_updateAvailable = false;
    QTimer m_updateTimer;
    QNetworkAccessManager *m_netManager;
};

#endif // APPLICATION_H
