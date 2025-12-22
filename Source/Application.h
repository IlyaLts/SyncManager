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

/*
===========================================================

    Application

===========================================================
*/
class Application : public QApplication
{
    Q_OBJECT

public:

    Application(int &argc, char **argv);
    ~Application();

    void init();
    void setLanguage(QLocale::Language language);
    void setTrayVisible(bool visible);
    void setLaunchOnStartup(bool enable);
    void setCheckForUpdates(bool enable);
    void setMaxCpuUsage(float percentage);
    void checkForUpdates();

    int exec();

    inline MainWindow *window() const { return m_window; }
    inline SystemTray *tray() const { return m_tray; }
    inline SyncManager *manager() const { return m_manager; }
    inline QThread *syncThread() const { return m_syncThread; }

    inline bool initiated() const { return m_initiated; }
    inline QLocale::Language language() const { return m_language; }
    inline bool trayVisible() const { return m_trayVisible; }
    inline bool updateAvailable() const { return m_updateAvailable; };
    inline QString toLocalizedDateTime(const QDateTime &dateTime, const QString &format) { return m_locale.toString(dateTime, format); }
    inline float processUsage() const { return m_processUsage; }
    inline float systemUsage() const { return m_systemUsage; }
    inline float maxCpuUsage() const { return m_maxCpuUsage; }
    inline bool checkForUpdatesEnabled() const { return m_checkForUpdates; }

    static int languageCount();
    static void textDialog(const QString &title, const QString &text);

    static bool questionBox(QMessageBox::Icon icon, const QString &title, const QString &text,
                            QMessageBox::StandardButton defaultButton, QWidget *parent = nullptr);

    static bool intInputDialog(QWidget *parent, const QString &title, const QString &label, int &returnValue,
                               int value = 0, int minValue = -2147483647, int maxValue = 2147483647);

    static bool doubleInputDialog(QWidget *parent, const QString &title, const QString &label, double &returnValue,
                                  double value = 0, double minValue = -2147483647, double maxValue = 2147483647);

    static bool textInputDialog(QWidget *parent, const QString &title, const QString &label,
                                QString &returnText, const QString &text = QString());

Q_SIGNALS:

    void updateFound();
    void languageChanged();

public Q_SLOTS:

    void quit();
    void saveSettings() const;

private Q_SLOTS:

    void onUpdateReply(QNetworkReply *reply);
    void updateCpuUsage(float appPercentage, float systemPercentage);

private:

    MainWindow *m_window = nullptr;
    SystemTray *m_tray = nullptr;
    SyncManager *m_manager = nullptr;
    QThread *m_syncThread = nullptr;
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
    QLocale::Language m_language;
    bool m_checkForUpdates;
};

#endif // APPLICATION_H
