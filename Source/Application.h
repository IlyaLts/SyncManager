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
#include "MainWindow.h"
#include "SystemTray.h"
#include "CpuUsage.h"
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
    ~Application();

    void init();
    void setTrayVisible(bool visible);
    void setLaunchOnStartup(bool enable);
    void setTranslator(QLocale::Language language);
    void setCheckForUpdates(bool enable);
    void setMaxCpuUsage(float percentage);
    void checkForUpdates();

    int exec();

    inline MainWindow *window() const { return m_window; }
    inline SystemTray *tray() const { return m_tray; }
    inline SyncManager *manager() const { return m_manager; }
    inline QThread *syncThread() const { return m_syncThread; }
    inline SyncWorker *syncWorker() const { return m_syncWorker; }

    inline bool initiated() const { return m_initiated; }
    inline bool trayVisible() const { return m_trayVisible; }
    inline bool updateAvailable() const { return m_updateAvailable; };
    inline QString toLocalizedDateTime(const QDateTime &dateTime, const QString &format) { return m_locale.toString(dateTime, format); }
    inline float processUsage() const { return m_processUsage; }
    inline float systemUsage() const { return m_systemUsage; }
    inline float maxCpuUsage() const { return m_maxCpuUsage; }

    static int languageCount();

Q_SIGNALS:

    void updateFound();

public Q_SLOTS:

    void quit();

private Q_SLOTS:

    void onUpdateReply(QNetworkReply *reply);
    void updateCpuUsage(float appPercentage, float systemPercentage);

private:

    MainWindow *m_window = nullptr;
    SystemTray *m_tray = nullptr;
    SyncManager *m_manager = nullptr;
    QThread *m_syncThread = nullptr;
    SyncWorker *m_syncWorker = nullptr;
    QTranslator m_translator;
    QLocale m_locale;

    bool m_initiated = false;
    bool m_trayVisible = true;
    bool m_updateAvailable = false;
    QTimer m_updateTimer;
    QNetworkAccessManager *m_netManager;
    CpuUsage *m_cpuUsage;
    float m_processUsage = 0.0;
    float m_systemUsage = 0.0;
    float m_maxCpuUsage = 100.0;
};

#endif // APPLICATION_H
