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

#include "MenuBar.h"
#include "Application.h"
#include "UnhidableMenu.h"
#include "AboutDialog.h"
#include <QPushButton>
#include <QDesktopServices>
#include <QStandardPaths>

/*
===================
MenuBar::MenuBar
===================
*/
MenuBar::MenuBar(QWidget *parent) : QMenuBar(parent)
{
    iconSync.addFile(":/Images/IconSync.png");
    iconPause.addFile(":/Images/IconPause.png");
    iconResume.addFile(":/Images/IconResume.png");
    iconSettings.addFile(":/Images/IconSettings.png");

    syncNowAction = new QAction(iconSync, "&" + tr("Sync Now"), this);
    pauseSyncingAction = new QAction(iconPause, "&" + tr("Pause Syncing"), this);
    maximumDiskTransferRateAction = new QAction("&" + tr("Maximum Disk Transfer Rate") + QString(": %1").arg(syncApp->manager()->maxDiskTransferRate()), this);
    maximumCpuUsageAction = new QAction("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(syncApp->maxCpuUsage()), this);

    for (int i = 0; i < Application::languageCount(); i++)
    {
        languageActions.append(new QAction(tr(languages[i].name), this));
        languageActions[i]->setIcon(QIcon(languages[i].flagPath));
    }

    launchOnStartupAction = new QAction("&" + tr("Launch on Startup"), this);
    showInTrayAction = new QAction("&" + tr("Show in System Tray"), this);
    disableNotificationAction = new QAction("&" + tr("Disable Notifications"), this);
    checkForUpdatesAction = new QAction("&" + tr("Check for Updates"), this);
    userManualAction = new QAction("&" + tr("User Manual"), this);
    reportBugAction = new QAction("&" + tr("Report a Bug"), this);
    aboutAction = new QAction("&" + tr("About"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setCheckable(true);

    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    checkForUpdatesAction->setCheckable(true);

    languageMenu = new UnhidableMenu("&" + tr("Language"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageMenu->addAction(languageActions[i]);

    performanceMenu = new UnhidableMenu("&" + tr("Performance"), this);
    performanceMenu->addAction(maximumDiskTransferRateAction);
    performanceMenu->addAction(maximumCpuUsageAction);

    settingsMenu = new UnhidableMenu("&" + tr("Settings"), this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(performanceMenu);
    settingsMenu->addMenu(languageMenu);
    settingsMenu->addAction(launchOnStartupAction);
    settingsMenu->addAction(showInTrayAction);
    settingsMenu->addAction(disableNotificationAction);
    settingsMenu->addAction(checkForUpdatesAction);
    settingsMenu->addSeparator();
    settingsMenu->addAction(userManualAction);
    settingsMenu->addAction(reportBugAction);
    settingsMenu->addAction(aboutAction);

    updateAvailableButton = new QPushButton(tr("New Update Available"));
    updateAvailableButton->setStyleSheet("QPushButton { margin: 2px 5px 0px 0px; padding: 5px 8px }");
    updateAvailableButton->setVisible(false);

    addAction(syncNowAction);
    addAction(pauseSyncingAction);
    addMenu(settingsMenu);

    setCornerWidget(updateAvailableButton);

    connect(syncNowAction, &QAction::triggered, this, &MenuBar::syncNowTriggered);
    connect(pauseSyncingAction, &QAction::triggered, this, &MenuBar::pauseTriggered);

    for (int i = 0; i < Application::languageCount(); i++)
        connect(languageActions[i], &QAction::triggered, this, [i](){ syncApp->setLanguage(languages[i].language); });

    connect(syncApp, &Application::languageChanged, this, &MenuBar::updateLanguageMenu);
    connect(launchOnStartupAction, &QAction::triggered, this, &MenuBar::toggleLaunchOnStartup);
    connect(showInTrayAction, &QAction::triggered, this, &MenuBar::toggleShowInTray);
    connect(disableNotificationAction, &QAction::triggered, this, &MenuBar::toggleNotification);
    connect(checkForUpdatesAction, &QAction::triggered, this, &MenuBar::toggleCheckForUpdates);

    connect(maximumDiskTransferRateAction, &QAction::triggered, this, &MenuBar::setMaximumTransferRateUsage);
    connect(maximumCpuUsageAction, &QAction::triggered, this, &MenuBar::setMaximumCpuUsage);
    connect(updateAvailableButton, &QPushButton::clicked, this, [](){ QDesktopServices::openUrl(QUrl(LATEST_RELEASE_URL)); });
    connect(syncApp, &Application::updateFound, this, &MenuBar::updateAvailable);
    connect(userManualAction, &QAction::triggered, this, [](){ QDesktopServices::openUrl(QUrl::fromLocalFile(USER_MANUAL_PATH)); });
    connect(reportBugAction, &QAction::triggered, this, [](){ QDesktopServices::openUrl(QUrl(BUG_TRACKER_URL)); });
    connect(aboutAction, &QAction::triggered, this, &MenuBar::triggerAboutDialog);

    updateStates();
}

/*
===================
MenuBar::retranslate
===================
*/
void MenuBar::retranslate()
{
    syncNowAction->setText("&" + tr("Sync Now"));
    pauseSyncingAction->setText("&" + tr("Pause Syncing"));
    maximumDiskTransferRateAction->setText("&" + tr("Maximum Disk Transfer Rate") + QString(": %1").arg(syncApp->manager()->maxDiskTransferRate()));
    maximumCpuUsageAction->setText("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(syncApp->maxCpuUsage()));

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setText(tr(languages[i].name));

    launchOnStartupAction->setText("&" + tr("Launch on Startup"));
    showInTrayAction->setText("&" + tr("Show in System Tray"));
    disableNotificationAction->setText("&" + tr("Disable Notifications"));
    checkForUpdatesAction->setText("&" + tr("Check for Updates"));
    userManualAction->setText("&" + tr("User Manual"));
    reportBugAction->setText("&" + tr("Report a Bug"));
    aboutAction->setText("&" + tr("About"));

    performanceMenu->setTitle("&" + tr("Performance"));
    languageMenu->setTitle("&" + tr("Language"));
    settingsMenu->setTitle("&" + tr("Settings"));

    syncNowAction->setToolTip("&" + tr("Sync Now"));
    pauseSyncingAction->setToolTip("&" + tr("Pause Syncing"));

    updateAvailableButton->setText(tr("New Update Available"));
    updateAvailableButton->adjustSize();

    updateMenuMaxDiskTransferRate();
    adjustSize();
}

/*
===================
MenuBar::updateStates
===================
*/
void MenuBar::updateStates()
{
    showInTrayAction->setChecked(syncApp->trayVisible());
    disableNotificationAction->setChecked(!syncApp->manager()->notificationsEnabled());
    checkForUpdatesAction->setChecked(syncApp->checkForUpdatesEnabled());

    updateSyncState();
    updateMenuMaxDiskTransferRate();
    updateLanguageMenu();
    updateLaunchOnStartupState();
}

/*
===================
MenuBar::updateSyncState
===================
*/
void MenuBar::updateSyncState()
{
    syncNowAction->setEnabled(syncApp->manager()->queue().size() != syncApp->manager()->existingProfiles());
}

/*
===================
MenuBar::togglePauseButton
===================
*/
void MenuBar::togglePauseButton(bool toggle)
{
    QIcon *icon;

    if (toggle)
        icon = &iconResume;
    else
        icon = &iconPause;

    // Fixes flickering menu bar
    if (pauseSyncingAction->icon().cacheKey() != icon->cacheKey())
        pauseSyncingAction->setIcon(*icon);

    pauseSyncingAction->setText("&" + tr(toggle ? "Resume Syncing" : "Pause Syncing"));
}

/*
===================
MenuBar::exportMenus
===================
*/
void MenuBar::exportMenus(SystemTray &tray)
{
    tray.addMenu(settingsMenu);
    tray.addSeparator();
    tray.addAction(pauseSyncingAction);
    tray.addAction(syncNowAction);
}

/*
===================
MenuBar::updateMenuMaxDiskTransferRate
===================
*/
void MenuBar::updateMenuMaxDiskTransferRate()
{
    QString text;

    if (syncApp->manager()->maxDiskTransferRate())
    {
        quint64 bytes = syncApp->manager()->maxDiskTransferRate() % 1024;
        quint64 kilobytes = (syncApp->manager()->maxDiskTransferRate() / 1024) % 1024;
        quint64 megabytes = (syncApp->manager()->maxDiskTransferRate() / 1024 / 1024) % 1024;
        quint64 gigabytes = (syncApp->manager()->maxDiskTransferRate() / 1024 / 1024/ 1024);

        if (gigabytes)
            text.append(tr("%1 GB/s").arg(QString::number(static_cast<float>(gigabytes) + static_cast<float>(megabytes) / 1024.0f, 'f', 1)));
        else if (megabytes)
            text.append(tr("%1 MB/s").arg(QString::number(static_cast<float>(megabytes) + static_cast<float>(kilobytes) / 1024.0f, 'f', 1)));
        else if (kilobytes)
            text.append(tr("%1 KB/s").arg(QString::number(static_cast<float>(kilobytes) + static_cast<float>(bytes) / 1024.0f, 'f', 1)));
        else if (bytes)
            text.append(tr("%1 B/s").arg(bytes));
    }

    if (text.isEmpty())
        text.assign(tr("Disabled"));

    maximumDiskTransferRateAction->setText("&" + tr("Maximum Disk Transfer Rate") + QString(": ") + text);
}

/*
===================
MenuBar::setMaximumTransferRateUsage
===================
*/
void MenuBar::setMaximumTransferRateUsage()
{
    QString title(tr("Maximum Disk Transfer Rate"));
    QString text(tr("Please enter the maximum disk transfer rate in bytes per second:"));
    int usage;

    if (!syncApp->intInputDialog(this, title, text, usage, syncApp->manager()->maxDiskTransferRate(), 0, std::numeric_limits<int>::max()))
        return;

    syncApp->manager()->setMaxDiskTransferRate(usage);
    updateMenuMaxDiskTransferRate();
    syncApp->saveSettings();
}

/*
===================
MenuBar::setMaximumCpuUsage
===================
*/
void MenuBar::setMaximumCpuUsage()
{
    QString title(tr("Maximum CPU Usage"));
    QString text(tr("Please enter the maximum CPU usage in percentage:"));
    double usage;

    if (!syncApp->doubleInputDialog(this, title, text, usage, syncApp->maxCpuUsage(), 0.01, 100.0))
        return;

    syncApp->setMaxCpuUsage(static_cast<float>(usage));
    maximumCpuUsageAction->setText("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(syncApp->maxCpuUsage()));
    syncApp->saveSettings();
}

/*
===================
MenuBar::disableNotification
===================
*/
void MenuBar::toggleNotification()
{
    syncApp->manager()->enableNotifications(!syncApp->manager()->notificationsEnabled());

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MenuBar::toggleShowInTray
===================
*/
void MenuBar::toggleShowInTray()
{
    syncApp->setTrayVisible(!syncApp->trayVisible());

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        showInTrayAction->setChecked(false);

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MenuBar::launchOnStartup
===================
*/
void MenuBar::toggleLaunchOnStartup()
{
    syncApp->setLaunchOnStartup(launchOnStartupAction->isChecked());
    updateLaunchOnStartupState();

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MenuBar::toggleCheckForUpdates
===================
*/
void MenuBar::toggleCheckForUpdates()
{
    syncApp->setCheckForUpdates(!syncApp->checkForUpdatesEnabled());
    updateAvailableButton->setVisible(syncApp->checkForUpdatesEnabled() && syncApp->updateAvailable());

    if (syncApp->checkForUpdatesEnabled())
        syncApp->checkForUpdate();
}

/*
===================
MenuBar::triggerAboutDialog
===================
*/
void MenuBar::triggerAboutDialog()
{
    AboutDialog dlg(this);
    dlg.exec();
}

/*
===================
MenuBar::updateLaunchOnStartupState
===================
*/
void MenuBar::updateLaunchOnStartupState()
{
#ifdef Q_OS_WIN
    QString path(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk");
    launchOnStartupAction->setChecked(QFile::exists(path));
#else
    QString path(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop");
    launchOnStartupAction->setChecked(QFile::exists(path));
#endif
}

/*
===================
MenuBar::updateLanguageMenu
===================
*/
void MenuBar::updateLanguageMenu()
{
    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setChecked(syncApp->language() == languages[i].language);
}

/*
===================
MenuBar::updateAvailable
===================
*/
void MenuBar::updateAvailable()
{
    updateAvailableButton->setVisible(true);
    syncApp->tray()->notify("Sync Manager", "New Update Available", QSystemTrayIcon::Information);
}
