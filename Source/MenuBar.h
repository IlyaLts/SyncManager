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

#ifndef MENUBAR_H
#define MENUBAR_H

#include <QMenuBar>

class UnhidableMenu;
class QPushButton;
class SystemTray;

class MenuBar : public QMenuBar
{
    Q_OBJECT

public:

    explicit MenuBar(QWidget *parent = nullptr);

    void retranslate();
    void updateStates();
    void updateSyncState();
    void togglePauseButton(bool toggle);
    void exportMenus(SystemTray &tray);

Q_SIGNALS:

    void syncNowTriggered();
    void pauseTriggered();

private Q_SLOTS:

    void updateMenuMaxDiskTransferRate();
    void setMaximumTransferRateUsage();
    void setMaximumCpuUsage();
    void toggleShowInTray();
    void toggleNotification();
    void toggleLaunchOnStartup();
    void toggleCheckForUpdates();
    void triggerAboutDialog();
    void updateLaunchOnStartupState();
    void updateLanguageMenu();
    void updateAvailable();
    void openManual() const;
    void openProjectPage();

private:

    QIcon iconSync;
    QIcon iconPause;
    QIcon iconResume;
    QIcon iconSettings;

    QAction *syncNowAction;
    QAction *pauseSyncingAction;
    QAction *maximumDiskTransferRateAction;
    QAction *maximumCpuUsageAction;
    QList<QAction *> languageActions;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *checkForUpdatesAction;
    QAction *userManualAction;
    QAction *reportBugAction;
    QAction *aboutAction;

    UnhidableMenu *settingsMenu;
    UnhidableMenu *performanceMenu;
    UnhidableMenu *languageMenu;

    QPushButton *updateAvailableButton;
};

#endif // MENUBAR_H
