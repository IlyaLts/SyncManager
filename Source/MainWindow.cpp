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

#include "Application.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "DecoratedStringListModel.h"
#include "UnhidableMenu.h"
#include "FolderListView.h"
#include "MenuProxyStyle.h"
#include "Common.h"
#include "FolderStyleDelegate.h"
#include "ProfileStyleDelegate.h"
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QDesktopServices>
#include <QMimeData>
#include <QPushButton>
#include <QThread>

/*
===================
MainWindow::MainWindow
===================
*/
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    connect(&updateTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    connect(syncApp->manager(), &SyncManager::finished, this, [this](){ syncDone(); });

    ui->setupUi(this);
    ui->centralWidget->setLayout(ui->mainLayout);
    setWindowTitle("Sync Manager");
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    profileModel = new DecoratedStringListModel;
    folderModel = new DecoratedStringListModel;
    ui->syncProfilesView->setModel(profileModel);
    ui->syncProfilesView->setItemDelegate(new ProfileStyleDelegate(ui->syncProfilesView));
    ui->folderListView->setModel(folderModel);
    ui->folderListView->setItemDelegate(new FolderStyleDelegate(ui->folderListView));

    if (QApplication::style()->name() == "windows11")
    {
        ui->syncProfilesView->setStyleSheet("QListView::item { height: 30px; }");
        ui->folderListView->setStyleSheet("QListView::item { height: 30px; }");
    }

    // Loads synchronization profiles
    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    QStringList profileNames = profilesData.allKeys();
    profileNames.sort();
    profileModel->setStringList(profileNames);

    for (auto &name : profileNames)
    {
        syncApp->manager()->profiles().emplace_back(name, profileIndexByName(name));
        SyncProfile &profile = syncApp->manager()->profiles().back();
        profile.paused = syncApp->manager()->paused();

        QStringList paths = profilesData.value(name).toStringList();
        paths.sort();

        for (auto &path : paths)
        {
            profile.folders.emplace_back(&profile);
            profile.folders.back().paused = syncApp->manager()->paused();
            profile.folders.back().path = path.toUtf8();
        }
    }

    connect(ui->syncProfilesView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(profileClicked(QItemSelection,QItemSelection)));
    connect(ui->syncProfilesView->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)), SLOT(profileNameChanged(QModelIndex,QModelIndex,QList<int>)));
    connect(ui->syncProfilesView, SIGNAL(deletePressed()), SLOT(removeProfile()));
    connect(ui->folderListView, &FolderListView::drop, this, &MainWindow::addFolder);
    connect(ui->folderListView, SIGNAL(deletePressed()), SLOT(removeFolder()));
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(syncApp->manager(), &SyncManager::message, this, [this](const QString &title, const QString &message){ syncApp->tray()->notify(title, message, QSystemTrayIcon::Critical); });
    connect(syncApp->manager(), &SyncManager::profileSynced, this, &MainWindow::profileSynced);

    setupMenus();
    loadSettings();
    retranslate();

    for (auto &profile : syncApp->manager()->profiles())
    {
        connect(&profile.syncTimer, &QChronoTimer::timeout, this, [&profile, this](){ sync(&const_cast<SyncProfile &>(profile), true); });
        switchDatabaseLocation(profile, profile.databaseLocation());
        updateMenuSyncTime(profile);
    }

    updateStatus();

    for (const auto &profile : syncApp->manager()->profiles())
        for (const auto &folder : profile.folders)
            if (!folder.exists)
                syncApp->tray()->notify(tr("Couldn't find folder"), folder.path, QSystemTrayIcon::Warning);
}

/*
===================
MainWindow::~MainWindow
===================
*/
MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

/*
===================
MainWindow::retranslate
===================
*/
void MainWindow::retranslate()
{
    syncNowAction->setText("&" + tr("Sync Now"));
    pauseSyncingAction->setText("&" + tr("Pause Syncing"));
    deltaCopyingAction->setText("&" + tr("File Delta Copying"));
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
    versionAction->setText(tr("Version: %1").arg(SYNCMANAGER_VERSION));

    performanceMenu->setTitle("&" + tr("Performance"));
    languageMenu->setTitle("&" + tr("Language"));
    settingsMenu->setTitle("&" + tr("Settings"));

    syncNowAction->setToolTip("&" + tr("Sync Now"));
    pauseSyncingAction->setToolTip("&" + tr("Pause Syncing"));
    ui->SyncLabel->setText(tr("Synchronization profiles:"));
    ui->foldersLabel->setText(tr("Folders to synchronize:"));

    updateAvailableButton->setText(tr("New Update Available"));
    updateAvailableButton->adjustSize();
    this->menuBar()->adjustSize();

    syncApp->tray()->retranslate();
    updateStatus();
    updateMenuMaxDiskTransferRate();

    for (auto &profile : syncApp->manager()->profiles())
    {
        profile.retranslate();
        updateMenuSyncTime(profile);
        updateProfileTooltip(profile);
    }
}

/*
===================
MainWindow::loadSettings
===================
*/
void MainWindow::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    resize(QSize(settings.value("Width", 500).toInt(), settings.value("Height", 300).toInt()));
    setWindowState(settings.value("Fullscreen", false).toBool() ? Qt::WindowMaximized : Qt::WindowActive);

    QList<int> hSizes;
    QVariantList hListDefault({ui->syncProfilesLayout->minimumSize().width(), 999999});
    QVariantList hList = settings.value("HorizontalSplitter", hListDefault).value<QVariantList>();
    for (auto &variant : hList) hSizes.append(variant.toInt());
    ui->horizontalSplitter->setSizes(hSizes);
    ui->horizontalSplitter->setStretchFactor(0, 0);
    ui->horizontalSplitter->setStretchFactor(1, 1);

    syncApp->manager()->setDeltaCopying(settings.value("DeltaCopying", false).toBool());
    deltaCopyingAction->setChecked(syncApp->manager()->deltaCopying());
    syncApp->setMaxCpuUsage(settings.value("MaximumCpuUsage", 100).toUInt());
    syncApp->setLanguage(static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt()));
    syncApp->setTrayVisible(settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool());
    syncApp->setCheckForUpdates(settings.value("CheckForUpdates", true).toBool());

    for (int i = 0; i < profileModel->rowCount(); i++)
    {
        QModelIndex index = profileModel->indexByRow(i);
        QString profileKeyPath(index.data(Qt::DisplayRole).toString() + QLatin1String("_profile/"));
        SyncProfile *profile = profileByIndex(index);

        if (!profile)
            continue;

        QString profileKeyname(profile->name + QLatin1String("_profile/"));

        switchSyncingMode(*profile, static_cast<SyncProfile::SyncingMode>(settings.value(profileKeyname + "SyncingMode", SyncProfile::AutomaticAdaptive).toInt()));
        switchDeletionMode(*profile, static_cast<SyncProfile::DeletionMode>(settings.value(profileKeyname + "DeletionMode", SyncProfile::MoveToTrash).toInt()));
        switchVersioningFormat(*profile, static_cast<SyncProfile::VersioningFormat>(settings.value(profileKeyname + "VersioningFormat", SyncProfile::FolderTimestamp).toInt()));
        switchVersioningLocation(*profile, static_cast<SyncProfile::VersioningLocation>(settings.value(profileKeyname + "VersioningLocation", SyncProfile::LocallyNextToFolder).toInt()));
        switchDatabaseLocation(*profile, static_cast<SyncProfile::DatabaseLocation>(settings.value(profileKeyname + "DatabaseLocation", SyncProfile::Decentralized).toInt()));

        // Loads saved pause states and checks if synchronization folders exist
        profile->paused = settings.value(profileKeyPath + QLatin1String("Paused"), false).toBool();

        if (!profile->paused)
            syncApp->manager()->setPaused(false);

        for (auto &folder : profile->folders)
        {
            folder.paused = settings.value(profileKeyPath + folder.path + QLatin1String("_Paused"), false).toBool();
            folder.syncType = static_cast<SyncFolder::SyncType>(settings.value(profileKeyPath + folder.path + QLatin1String("_SyncType"), SyncFolder::TWO_WAY).toInt());

            if (!folder.paused)
                syncApp->manager()->setPaused(false);

            folder.exists = QFileInfo::exists(folder.path);
            folder.lastSyncDate = settings.value(profileKeyPath + folder.path + QLatin1String("_LastSyncDate")).toDateTime();
            folder.PartiallySynchronized = settings.value(profileKeyPath + folder.path + QLatin1String("_PartiallySynchronized")).toBool();
        }

        updateProfileTooltip(*profile);
    }

    showInTrayAction->setChecked(syncApp->trayVisible());
    disableNotificationAction->setChecked(!syncApp->manager()->notificationsEnabled());
    checkForUpdatesAction->setChecked(syncApp->checkForUpdatesEnabled());

    updateMenuMaxDiskTransferRate();
}

/*
===================
MainWindow::saveSettings
===================
*/
void MainWindow::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QVariantList hSizes;

    for (auto &size : ui->horizontalSplitter->sizes())
        hSizes.append(size);

    settings.setValue("DeltaCopying", syncApp->manager()->deltaCopying());
    settings.setValue("Fullscreen", isMaximized());
    settings.setValue("HorizontalSplitter", hSizes);

    if (!isMaximized())
    {
        settings.setValue("Width", size().width());
        settings.setValue("Height", size().height());
    }
}

/*
===================
MainWindow::show
===================
*/
void MainWindow::show()
{
    if (QSystemTrayIcon::isSystemTrayAvailable() && syncApp->trayVisible())
    {
#ifdef Q_OS_LINUX
        // Fixes wrong window position after hiding the window.
        if (isHidden())
        {
            move(pos().x() + (frameSize().width() - size().width()),
                 pos().y() + (frameSize().height() - size().height()));
        }
#endif

        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        QMainWindow::show();
        raise();
        activateWindow();
    }
    else
    {
        QMainWindow::show();
    }
}

/*
===================
MainWindow::closeEvent
===================
*/
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (QSystemTrayIcon::isSystemTrayAvailable() && syncApp->trayVisible())
    {
        // Hides the window instead of closing as it can appear out of the screen after disconnecting a display.
        hide();
        event->ignore();
    }
    else
    {
        QString title(tr("Quit"));
        QString text(tr("Currently syncing. Are you sure you want to quit?"));

        if (syncApp->manager()->busy() && !syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
        {
            event->ignore();
            return;
        }

        syncApp->manager()->shouldQuit();
        event->accept();
    }
}

/*
===================
MainWindow::showEvent
===================
*/
void MainWindow::showEvent(QShowEvent *event)
{
    updateStatus();
    QMainWindow::showEvent(event);
}

/*
===================
MainWindow::addProfile
===================
*/
void MainWindow::addProfile()
{
    QString newName(tr("New profile"));
    QStringList profileNames;

    for (const auto &profile : syncApp->manager()->profiles())
        profileNames.append(profile.name);

    for (int i = 2; profileNames.contains(newName); i++)
    {
        newName = tr(" (%1)").arg(i);
        newName.insert(0, tr("New profile"));
    }

    profileNames.append(newName);
    profileModel->setStringList(profileNames);
    folderModel->setStringList(QStringList());
    syncApp->manager()->profiles().emplace_back(newName, profileIndexByName(newName));
    syncApp->manager()->profiles().back().paused = syncApp->manager()->paused();
    syncApp->manager()->profiles().back().setupMenus(this);
    connectProfileMenu(syncApp->manager()->profiles().back());
    rebindProfiles();

    QSettings profileData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileData.setValue(newName, folderModel->stringList());

    // Avoids reloading a newly added profile as it's already loaded.
    ui->syncProfilesView->selectionModel()->blockSignals(true);
    ui->syncProfilesView->setCurrentIndex(ui->syncProfilesView->model()->index(ui->syncProfilesView->model()->rowCount() - 1, 0));
    ui->syncProfilesView->selectionModel()->blockSignals(false);

    ui->folderListView->selectionModel()->reset();
    ui->folderListView->update();
    updateProfileTooltip(syncApp->manager()->profiles().back());
    updateStatus();
    syncApp->manager()->updateNextSyncingTime(syncApp->manager()->profiles().back());
    syncApp->manager()->updateTimer(syncApp->manager()->profiles().back());
}

/*
===================
MainWindow::removeProfile
===================
*/
void MainWindow::removeProfile()
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        return;

    for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
    {
        SyncProfile *profile = profileByIndex(index);

        if (!profile)
            continue;

        QString title(tr("Remove profile"));
        QString text;

        if (profile->syncing)
            text.assign(tr("The profile is currently syncing. Are you sure you want to remove it?"));
        else
            text.assign(tr("Are you sure you want to remove the profile?"));

        if (!syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            continue;

        ui->syncProfilesView->model()->removeRow(index.row());
        disconnectProfileMenu(*profile);
        rebindProfiles();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile->name + QLatin1String("_profile"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.remove(profile->name);

        profile->paused = true;
        profile->toBeRemoved = true;

        for (auto &folder : profile->folders)
        {
            folder.paused = true;
            folder.toBeRemoved = true;
            folder.removeDatabase();
        }

        if (!syncApp->manager()->busy())
            syncApp->manager()->profiles().remove(*profile);

        folderModel->setStringList(QStringList());
        syncApp->manager()->updateNextSyncingTime(*profile);
    }

    ui->syncProfilesView->selectionModel()->reset();
    updateStatus();
}

/*
===================
MainWindow::profileClicked
===================
*/
void MainWindow::profileClicked(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);

    // Resets profile folders list when a user clicks on an empty area
    if (selected.indexes().isEmpty())
    {
        folderModel->setStringList(QStringList());
        return;
    }

    QStringList folderPaths;
    const SyncProfile *profile = profileByIndex(ui->syncProfilesView->currentIndex());

    if (!profile)
        return;

    for (const auto &folder : profile->folders)
        folderPaths.append(folder.path);

    folderModel->setStringList(folderPaths);
    updateProfileTooltip(*profile);
    updateStatus();
}

/*
===================
MainWindow::profileNameChanged
===================
*/
void MainWindow::profileNameChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    Q_UNUSED(bottomRight);

    if (!roles.contains(Qt::DisplayRole))
        return;

    QString newName = topLeft.data(Qt::DisplayRole).toString();
    newName.remove('/');
    newName.remove('\\');

    QStringList profileNames;
    QStringList folderPaths;

    for (const auto &profile : syncApp->manager()->profiles())
        profileNames.append(profile.name);

    SyncProfile *profile = profileByIndex(topLeft);

    if (!profile)
        return;

    if (newName == profile->name)
        return;

    for (const auto &folder : profile->folders)
        folderPaths.append(folder.path);

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(profile->name, Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(topLeft, profile->name, Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString oldProfilePrefix = profile->name + QLatin1String("_profile");
    QStringList allKeys = settings.allKeys();
    QStringList keysToRename;

    for (const QString &key : std::as_const(allKeys))
        if (key.startsWith(oldProfilePrefix))
            keysToRename.append(key);

    for (const QString &key : std::as_const(keysToRename))
    {
        QString newKey = QString(key).replace(0, oldProfilePrefix.length(), newName + QLatin1String("_profile"));
        settings.setValue(newKey, settings.value(key));
        settings.remove(key);
    }

    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profilesData.remove(profile->name);
    profilesData.setValue(newName, folderPaths);

    profile->name = newName;
}

/*
===================
MainWindow::addFolder
===================
*/
void MainWindow::addFolder(const QMimeData *mimeData)
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        return;

    QModelIndex index = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
    SyncProfile *profile = profileByIndex(index);
    QStringList existedFolders;
    QStringList foldersToAdd;

    if (!profile)
        return;

    for (const auto &folder : profile->folders)
        if (!folder.toBeRemoved)
            existedFolders.append(folder.path);

    // Drag & drop
    if (mimeData)
    {
        const QList<QUrl> urls = mimeData->urls();

        for (const auto &url : urls)
        {
            if (!QFileInfo(url.toLocalFile()).isDir())
                continue;

            foldersToAdd.append(url.toLocalFile() + "/");
        }
    }
    // Browse dialog
    else
    {
        QString title(tr("Browse For Folder"));
        QString folderPath = QFileDialog::getExistingDirectory(this, title, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (folderPath.isEmpty())
            return;

        foldersToAdd.append(folderPath + "/");
    }

    // Checks if we already have a folder for synchronization in the list
    for (const auto &newFolderPath : foldersToAdd)
    {
        bool exists = false;

        if (const SyncFolder *folder = profile->folderByPath(newFolderPath))
        {
            for (const auto &existedFolderPath : existedFolders)
            {
                if (existedFolderPath.compare(newFolderPath, folder->caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
                {
                    exists = true;
                    break;
                }
            }
        }

        if (!exists)
        {
            profile->folders.emplace_back(profile);
            profile->folders.back().paused = profile->paused;
            profile->folders.back().path = newFolderPath.toUtf8();
            existedFolders.append(profile->folders.back().path);

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(profile->name, existedFolders);

            folderModel->setStringList(existedFolders);
            updateStatus();
        }
    }

    updateProfileTooltip(*profile);
}

/*
===================
MainWindow::removeFolder
===================
*/
void MainWindow::removeFolder()
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty() || ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
        return;

    for (auto &folderIndex : ui->folderListView->selectionModel()->selectedIndexes())
    {
        QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
        SyncProfile *profile = profileByIndex(profileIndex);
        SyncFolder *folder = profile->folderByIndex(folderIndex);

        if (!profile || !folder)
            continue;

        QString title(tr("Remove folder"));
        QString text;

        if (folder->syncing)
            text.assign(tr("The folder is currently syncing. Are you sure you want to remove it?"));
        else
            text.assign(tr("Are you sure you want to remove the folder?"));

        if (!syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            return;

        folder->paused = true;
        folder->toBeRemoved = true;
        ui->folderListView->model()->removeRow(folderIndex.row());

        folder->removeDatabase();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile->name + QLatin1String("_profile/") + folder->path + QLatin1String("_Paused"));

        if (!syncApp->manager()->busy())
            profile->folders.remove(*folder);

        QStringList foldersPaths;

        for (const auto &folder : profile->folders)
            foldersPaths.append(folder.path);

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profile->name, foldersPaths);

        syncApp->manager()->updateTimer(*profile);
        syncApp->manager()->updateNextSyncingTime(*profile);
    }

    ui->folderListView->selectionModel()->reset();
    updateStatus();
}

/*
===================
MainWindow::pauseSyncing
===================
*/
void MainWindow::pauseSyncing()
{
    syncApp->manager()->setPaused(!syncApp->manager()->paused());

    for (auto &profile : syncApp->manager()->profiles())
    {
        profile.paused = syncApp->manager()->paused();

        for (auto &folder : profile.folders)
            folder.paused = syncApp->manager()->paused();

        if (profile.paused)
            profile.syncTimer.stop();
        else
            syncApp->manager()->updateTimer(profile);

        if (syncApp->initiated())
            profile.saveSettings();
    }

    updateStatus();

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MainWindow::pauseSelected
===================
*/
void MainWindow::pauseSelected()
{
    if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
    {
        // Folders are selected
        if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty() && ui->folderListView->hasFocus())
        {
            QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
            SyncProfile *profile = profileByIndex(profileIndex);

            if (!profile)
                return;

            const QModelIndexList selectedIndexes = ui->folderListView->selectionModel()->selectedIndexes();

            for (const auto &index : selectedIndexes)
            {
                SyncFolder *folder = profile->folderByIndex(index);

                if (!folder)
                    continue;

                folder->paused = !folder->paused;
            }

            profile->paused = true;

            for (const auto &folder : profile->folders)
                if (!folder.paused)
                    profile->paused = false;

            if (syncApp->initiated())
                profile->saveSettings();
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            const QModelIndexList selectedIndexes = ui->syncProfilesView->selectionModel()->selectedIndexes();

            for (const auto &index : selectedIndexes)
            {
                SyncProfile *profile = profileByIndex(index);

                if (!profile)
                    return;

                profile->paused = !profile->paused;

                for (auto &folder : profile->folders)
                    folder.paused = profile->paused;

                if (profile->paused)
                    profile->syncTimer.stop();
                else
                    syncApp->manager()->updateTimer(*profile);

                if (syncApp->initiated())
                    profile->saveSettings();
            }
        }

        updateStatus();
    }
}

/*
===================
MainWindow::switchSyncingMode
===================
*/
void MainWindow::switchSyncingMode(SyncProfile &profile, SyncProfile::SyncingMode mode)
{
    if (mode < SyncProfile::Manual || mode > SyncProfile::AutomaticFixed)
        mode = SyncProfile::AutomaticAdaptive;

    profile.setSyncingMode(mode);
    profile.manualAction->setChecked(mode == SyncProfile::Manual);
    profile.automaticAdaptiveAction->setChecked(mode == SyncProfile::AutomaticAdaptive);
    profile.automaticFixedAction->setChecked(mode == SyncProfile::AutomaticFixed);
    profile.increaseSyncTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    profile.syncingTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    profile.decreaseSyncTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    profile.fixedSyncingTimeAction->setVisible(mode == SyncProfile::AutomaticFixed);

    if (mode == SyncProfile::Manual)
    {
        profile.syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        syncApp->manager()->updateNextSyncingTime(profile);
        syncApp->manager()->updateTimer(profile);
    }

    updateStatus();
    updateProfileTooltip(profile);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchDeletionMode
===================
*/
void MainWindow::switchDeletionMode(SyncProfile &profile, SyncProfile::DeletionMode mode)
{
    if (mode < SyncProfile::MoveToTrash || mode > SyncProfile::DeletePermanently)
        mode = SyncProfile::MoveToTrash;

    if (syncApp->initiated() && mode == SyncProfile::DeletePermanently && mode != profile.deletionMode())
    {
        QString title(tr("Switch deletion mode to delete files permanently?"));
        QString text(tr("Are you sure? Beware: this could lead to data loss!"));

        if (!syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
            mode = profile.deletionMode();
    }

    profile.setDeletionMode(mode);
    profile.moveToTrashAction->setChecked(mode == SyncProfile::MoveToTrash);
    profile.versioningAction->setChecked(mode == SyncProfile::Versioning);
    profile.deletePermanentlyAction->setChecked(mode == SyncProfile::DeletePermanently);
    profile.versioningFormatMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);
    profile.versioningLocationMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchVersioningFormat
===================
*/
void MainWindow::switchVersioningFormat(SyncProfile &profile, SyncProfile::VersioningFormat format)
{
    if (format < SyncProfile::FileTimestampBefore || format > SyncProfile::LastVersion)
        format = SyncProfile::FileTimestampAfter;

    profile.fileTimestampBeforeAction->setChecked(format == SyncProfile::FileTimestampBefore);
    profile.fileTimestampAfterAction->setChecked(format == SyncProfile::FileTimestampAfter);
    profile.folderTimestampAction->setChecked(format == SyncProfile::FolderTimestamp);
    profile.lastVersionAction->setChecked(format == SyncProfile::LastVersion);
    profile.setVersioningFormat(format);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchVersioningLocation
===================
*/
void MainWindow::switchVersioningLocation(SyncProfile &profile, SyncProfile::VersioningLocation location)
{
    if (location < SyncProfile::LocallyNextToFolder || location > SyncProfile::CustomLocation)
        location = SyncProfile::LocallyNextToFolder;

    profile.versioningPostfixAction->setVisible(location == SyncProfile::LocallyNextToFolder);
    profile.locallyNextToFolderAction->setChecked(location == SyncProfile::LocallyNextToFolder);
    profile.customLocationAction->setChecked(location == SyncProfile::CustomLocation);
    profile.customLocationPathAction->setVisible(location == SyncProfile::CustomLocation);
    profile.setVersioningLocation(location);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchSyncingType
===================
*/
void MainWindow::switchSyncingType(SyncProfile &profile, SyncFolder &folder, SyncFolder::SyncType type)
{
    if (type < SyncFolder::TWO_WAY || type > SyncFolder::ONE_WAY_UPDATE)
        type = SyncFolder::TWO_WAY;

    folder.syncType = type;
    updateStatus();

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchDatabaseLocation
===================
*/
void MainWindow::switchDatabaseLocation(SyncProfile &profile, SyncProfile::DatabaseLocation location)
{
    if (location < SyncProfile::Locally || location > SyncProfile::Decentralized)
        location = SyncProfile::Decentralized;

    profile.databaseLocallyAction->setChecked(location == false);
    profile.databaseDecentralizedAction->setChecked(location == true);
    profile.setDatabaseLocation(location);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::increaseSyncTime
===================
*/
void MainWindow::increaseSyncTime(SyncProfile &profile)
{
    quint64 max = SyncManager::maxInterval();

    // If exceeds the maximum value of an qint64
    if (profile.syncEvery >= max)
    {
        profile.increaseSyncTimeAction->setEnabled(false);
        return;
    }

    syncApp->manager()->setSyncTimeMultiplier(profile, profile.syncTimeMultiplier() + 1);
    profile.decreaseSyncTimeAction->setEnabled(true);
    updateMenuSyncTime(profile);

    // If exceeds the maximum value of an qint64
    if (profile.syncEvery >= max)
        profile.increaseSyncTimeAction->setEnabled(false);

    syncApp->manager()->updateTimer(profile);
    updateProfileTooltip(profile);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::decreaseSyncTime
===================
*/
void MainWindow::decreaseSyncTime(SyncProfile &profile)
{
    syncApp->manager()->setSyncTimeMultiplier(profile, profile.syncTimeMultiplier() - 1);
    updateMenuSyncTime(profile);

    quint64 max = SyncManager::maxInterval();

    // If exceeds the maximum value of an qint64
    if (profile.syncEvery < max)
        profile.increaseSyncTimeAction->setEnabled(true);

    if (profile.syncTimeMultiplier() <= 1)
        profile.decreaseSyncTimeAction->setEnabled(false);

    syncApp->manager()->updateTimer(profile);
    updateProfileTooltip(profile);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::switchLanguage
===================
*/
void MainWindow::switchLanguage(QLocale::Language language)
{
    syncApp->setLanguage(language);
}

/*
===================
MainWindow::updateLanguageMenu
===================
*/
void MainWindow::updateLanguageMenu()
{
    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setChecked(syncApp->language() == languages[i].language);
}

/*
===================
MainWindow::launchOnStartup
===================
*/
void MainWindow::toggleLaunchOnStartup()
{
    syncApp->setLaunchOnStartup(launchOnStartupAction->isChecked());
    updateLaunchOnStartupState();

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MainWindow::toggleShowInTray
===================
*/
void MainWindow::toggleShowInTray()
{
    syncApp->setTrayVisible(!syncApp->trayVisible());

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        showInTrayAction->setChecked(false);

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MainWindow::disableNotification
===================
*/
void MainWindow::toggleNotification()
{
    syncApp->manager()->enableNotifications(!syncApp->manager()->notificationsEnabled());

    if (syncApp->initiated())
        syncApp->saveSettings();
}

/*
===================
MainWindow::toggleCheckForUpdates
===================
*/
void MainWindow::toggleCheckForUpdates()
{
    syncApp->setCheckForUpdates(!syncApp->checkForUpdatesEnabled());
    updateAvailableButton->setVisible(syncApp->checkForUpdatesEnabled() && syncApp->updateAvailable());

    if (syncApp->checkForUpdatesEnabled())
        syncApp->checkForUpdates();
}

/*
===================
MainWindow::setMaximumTransferRateUsage
===================
*/
void MainWindow::setMaximumTransferRateUsage()
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
MainWindow::setMaximumCpuUsage
===================
*/
void MainWindow::setMaximumCpuUsage()
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
MainWindow::setFixedInterval
===================
*/
void MainWindow::setFixedInterval(SyncProfile &profile)
{
    QString title(tr("Synchronize Every"));
    QString text(tr("Please enter the synchronization interval in seconds:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile.syncIntervalFixed() / 1000, 0))
        return;

    profile.setSyncIntervalFixed(size * 1000);
    updateMenuSyncTime(profile);

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setVersioningPostfix
===================
*/
void MainWindow::setVersioningPostfix(SyncProfile &profile)
{
    QString postfix = profile.versioningFolder();
    QString title(tr("Versioning Folder Postfix"));
    QString text(tr("Please enter the versioning folder postfix:"));

    if (!syncApp->textInputDialog(this, title, text, postfix, postfix))
        return;

    profile.setVersioningFolder(postfix);
    profile.versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(profile.versioningFolder()));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setVersioningPattern
===================
*/
void MainWindow::setVersioningPattern(SyncProfile &profile)
{
    QString pattern = profile.versioningPattern();
    QString title(tr("Versioning Pattern"));
    QString text(tr("Please enter the versioning pattern:"));
    text.append("\n\n");
    text.append(tr("Examples:"));
    text.append("\nyyyy_M_d_h_m_s_z - 2001_5_21_14_13_09_120");
    text.append("\nyyyy_MM_dd - 2001_05_21");
    text.append("\nyy_MMMM_d - 01_May_21");
    text.append("\nhh:mm:ss.zzz - 14_13_09_120");
    text.append("\nap_h_m_s - pm_2_13_9");

    if (!syncApp->textInputDialog(this, title, text, pattern, pattern))
        return;

    profile.setVersioningPattern(pattern);
    profile.versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(profile.versioningPattern()));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setVersioningLocationPath
===================
*/
void MainWindow::setVersioningLocationPath(SyncProfile &profile)
{
    if (profile.versioningLocation() == SyncProfile::CustomLocation && syncApp->initiated())
    {
        QString title(tr("Browse for Versioning Folder"));
        QString path = QFileDialog::getExistingDirectory(this, title, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (!path.isEmpty())
        {
            profile.setVersioningPath(path);
            profile.customLocationPathAction->setText(tr("Custom Location: ") + path);
        }
    }

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setFileMinSize
===================
*/
void MainWindow::setFileMinSize(SyncProfile &profile)
{
    QString title(tr("Minimum File Size"));
    QString text(tr("Please enter the minimum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile.fileMinSize(), 0))
        return;

    if (size && profile.fileMaxSize() && static_cast<quint64>(size) > profile.fileMaxSize())
        size = profile.fileMaxSize();

    profile.setFileMinSize(size);
    profile.fileMinSizeAction->setText("&" + tr("Minimum File Size: %1 bytes").arg(profile.fileMinSize()));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setFileMaxSize
===================
*/
void MainWindow::setFileMaxSize(SyncProfile &profile)
{
    QString title(tr("Maximum File Size"));
    QString text(tr("Please enter the maximum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile.fileMaxSize(), 0))
        return;

    if (size && static_cast<quint64>(size) < profile.fileMinSize())
        size = profile.fileMinSize();

    profile.setFileMaxSize(size);
    profile.fileMaxSizeAction->setText("&" + tr("Maximum File Size: %1 bytes").arg(profile.fileMaxSize()));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setMovedFileMinSize
===================
*/
void MainWindow::setMovedFileMinSize(SyncProfile &profile)
{
    QString title(tr("Minimum Size for Moved File"));
    QString text(tr("Please enter the minimum size for a moved file in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile.movedFileMinSize(), 0))
        return;

    profile.setMovedFileMinSize(size);
    profile.movedFileMinSizeAction->setText("&" + tr("Minimum Size for a Moved File: %1 bytes").arg(profile.movedFileMinSize()));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setIncludeList
===================
*/
void MainWindow::setIncludeList(SyncProfile &profile)
{
    QString includeString = profile.includeList().join("; ");
    QString title(tr("Include List"));
    QString text(tr("Please enter include list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(this, title, text, includeString, includeString))
        return;

    QStringList includeList = includeString.split(";");

    for (auto &include : includeList)
        include = include.trimmed();

    includeString = includeList.join("; ");
    profile.setIncludeList(includeList);
    profile.includeAction->setText("&" + tr("Include: %1").arg(includeString));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::setExcludeList
===================
*/
void MainWindow::setExcludeList(SyncProfile &profile)
{
    QString excludeString = profile.excludeList().join("; ");
    QString title(tr("Exclude List"));
    QString text(tr("Please enter exclude list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(this, title, text, excludeString, excludeString))
        return;

    QStringList excludeList = excludeString.split(";");

    for (auto &exclude : excludeList)
        exclude = exclude.trimmed();

    excludeString = excludeList.join("; ");
    profile.setExcludeList(excludeList);
    profile.excludeAction->setText("&" + tr("Exclude: %1").arg(excludeString));

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::toggleIgnoreHiddenFiles
===================
*/
void MainWindow::toggleIgnoreHiddenFiles(SyncProfile &profile)
{
    profile.setIgnoreHiddenFiles(!profile.ignoreHiddenFiles());

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::detectMovedFiles
===================
*/
void MainWindow::toggleDetectMoved(SyncProfile &profile)
{
    profile.setDetectMovedFiles(!profile.detectMovedFiles());

    if (syncApp->initiated())
        profile.saveSettings();
}

/*
===================
MainWindow::showContextMenu
===================
*/
void MainWindow::showContextMenu(const QPoint &pos)
{
    static QMenu menu;
    menu.clear();

    // Profiles
    if (ui->syncProfilesView->hasFocus())
    {
        menu.addAction(iconAdd, "&" + tr("Add a new profile"), this, SLOT(addProfile()));

        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
            SyncProfile *profile = profileByIndex(profileIndex);

            if (!profile)
                return;

            if (profile->paused)
            {
                menu.addAction(iconResume, "&" + tr("Resume syncing profile"), this, SLOT(pauseSelected()));
            }
            else
            {
                menu.addAction(iconPause, "&" + tr("Pause syncing profile"), this, SLOT(pauseSelected()));

                QAction *action = menu.addAction(iconSync, "&" + tr("Synchronize profile"), this, [=, this](){ sync(profile, false); });
                action->setDisabled(syncApp->manager()->queue().contains(profile));
            }

            menu.addAction(iconRemove, "&" + tr("Remove profile"), this, SLOT(removeProfile()));

            menu.addSeparator();
            menu.addMenu(profile->syncingModeMenu);
            menu.addMenu(profile->deletionModeMenu);
            menu.addMenu(profile->versioningFormatMenu);
            menu.addMenu(profile->versioningLocationMenu);
            menu.addMenu(profile->databaseLocationMenu);
            menu.addMenu(profile->filteringMenu);
        }

        menu.popup(ui->syncProfilesView->mapToGlobal(pos));
    }
    // Folders
    else if (ui->folderListView->hasFocus())
    {
        if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
            return;

        menu.addAction(iconAdd, "&" + tr("Add a new folder"), this, SLOT(addFolder()));

        if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
        {
            QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
            QModelIndex folderIndex = ui->folderListView->selectionModel()->selectedIndexes()[0];
            SyncProfile *profile = profileByIndex(profileIndex);
            SyncFolder *folder = profile->folderByIndex(folderIndex);

            if (!profile || !folder)
                return;

            if (folder->paused)
                menu.addAction(iconResume, "&" + tr("Resume syncing folder"), this, SLOT(pauseSelected()));
            else
                menu.addAction(iconPause, "&" + tr("Pause syncing folder"), this, SLOT(pauseSelected()));

            menu.addAction(iconRemove, "&" + tr("Remove folder"), this, SLOT(removeFolder()));

            if (folder->partiallySynchronized() && !folder->unsyncedList.isEmpty())
            {
                QString menuTitle(tr("Show unsynchronized files"));
                QString title(tr("Couldn't synchronize the following files"));

                menu.addSeparator();
                menu.addAction(iconWarning, "&" + menuTitle, this, [title, folder](){ syncApp->textDialog(title, folder->unsyncedList); });
            }

            menu.addSeparator();

            if (folder->syncType != SyncFolder::TWO_WAY)
                menu.addAction(iconTwoWay, "&" + tr("Switch to two-way synchronization"), this, [profile, folder, this](){ switchSyncingType(*profile, *folder, SyncFolder::TWO_WAY); });

            if (folder->syncType != SyncFolder::ONE_WAY)
                menu.addAction(iconOneWay, "&" + tr("Switch to one-way synchronization"), this, [profile, folder, this](){ switchSyncingType(*profile, *folder, SyncFolder::ONE_WAY); });

            if (folder->syncType != SyncFolder::ONE_WAY_UPDATE)
                menu.addAction(iconOneWayUpdate, "&" + tr("Switch to one-way update synchronization"), this, [profile, folder, this](){ switchSyncingType(*profile, *folder, SyncFolder::ONE_WAY_UPDATE); });
        }

        menu.popup(ui->folderListView->mapToGlobal(pos));
    }
}

/*
===================
MainWindow::sync
===================
*/
void MainWindow::sync(SyncProfile *profile, bool hidden)
{
    if (profile)
    {
        if (syncApp->manager()->queue().contains(profile))
            return;

        profile->syncHidden = hidden;
    }
    else
    {
        for (auto &profile : syncApp->manager()->profiles())
            profile.syncHidden = false;
    }

    syncApp->manager()->addToQueue(profile);

    if (!syncApp->manager()->busy())
    {
        animSync.start();

        if (!syncApp->syncThread()->isRunning())
        {
            syncApp->syncThread()->start();
            updateTimer.start(UPDATE_TIME);
        }
    }
}

/*
===================
MainWindow::syncDone
===================
*/
void MainWindow::syncDone()
{
    updateTimer.stop();
    animSync.stop();

    updateStatus();
    syncApp->manager()->purgeRemovedProfiles();
}

/*
===================
MainWindow::profileSynced
===================
*/
void MainWindow::profileSynced(SyncProfile *profile)
{
    syncApp->manager()->updateTimer(*profile);
    updateMenuSyncTime(*profile);
    updateProfileTooltip(*profile);
    syncApp->saveSettings();
}

/*
===================
MainWindow::updateAvailable
===================
*/
void MainWindow::updateAvailable()
{
    updateAvailableButton->setVisible(true);
    syncApp->tray()->notify("Sync Manager", "New Update Available", QSystemTrayIcon::Information);
}

/*
===================
MainWindow::rebindProfiles

Rebinds profiles to profile models
===================
*/
void MainWindow::rebindProfiles()
{
    for (int i = 0; i < profileModel->rowCount(); i++)
    {
        for (auto &profile : syncApp->manager()->profiles())
        {
            if (profileModel->indexByRow(i).data(Qt::DisplayRole).toString()  == profile.name)
                profile.index = profileModel->indexByRow(i);
        }
    }
}

/*
===================
MainWindow::connectProfileMenu
===================
*/
void MainWindow::connectProfileMenu(SyncProfile &profile)
{
    connect(&profile.syncTimer, &QChronoTimer::timeout, this, [this, &profile](){ sync(&profile, true); });
    connect(profile.manualAction, &QAction::triggered, this, [this, &profile](){ switchSyncingMode(profile, SyncProfile::Manual); });
    connect(profile.automaticAdaptiveAction, &QAction::triggered, this, [this, &profile](){ switchSyncingMode(profile, SyncProfile::AutomaticAdaptive); });
    connect(profile.automaticFixedAction, &QAction::triggered, this, [this, &profile](){ switchSyncingMode(profile, SyncProfile::AutomaticFixed); });
    connect(profile.increaseSyncTimeAction, &QAction::triggered, this, [this, &profile](){ increaseSyncTime(profile); });
    connect(profile.decreaseSyncTimeAction, &QAction::triggered, this, [this, &profile](){ decreaseSyncTime(profile); });
    connect(profile.fixedSyncingTimeAction, &QAction::triggered, this, [this, &profile](){ setFixedInterval(profile); });
    connect(profile.detectMovedFilesAction, &QAction::triggered, this, [this, &profile](){ toggleDetectMoved(profile); });
    connect(profile.moveToTrashAction, &QAction::triggered, this, [this, &profile](){ switchDeletionMode(profile, SyncProfile::MoveToTrash); });
    connect(profile.versioningAction, &QAction::triggered, this, [this, &profile](){ switchDeletionMode(profile, SyncProfile::Versioning); });
    connect(profile.deletePermanentlyAction, &QAction::triggered, this, [this, &profile](){ switchDeletionMode(profile, SyncProfile::DeletePermanently); });
    connect(profile.fileTimestampBeforeAction, &QAction::triggered, this, [this, &profile](){ switchVersioningFormat(profile, SyncProfile::FileTimestampBefore); });
    connect(profile.fileTimestampAfterAction, &QAction::triggered, this, [this, &profile](){ switchVersioningFormat(profile, SyncProfile::FileTimestampAfter); });
    connect(profile.folderTimestampAction, &QAction::triggered, this, [this, &profile](){ switchVersioningFormat(profile, SyncProfile::FolderTimestamp); });
    connect(profile.lastVersionAction, &QAction::triggered, this, [this, &profile](){ switchVersioningFormat(profile, SyncProfile::LastVersion); });
    connect(profile.versioningPostfixAction, &QAction::triggered, this, [this, &profile](){ setVersioningPostfix(profile); });
    connect(profile.versioningPatternAction, &QAction::triggered, this, [this, &profile](){ setVersioningPattern(profile); });
    connect(profile.locallyNextToFolderAction, &QAction::triggered, this, [this, &profile](){ switchVersioningLocation(profile, SyncProfile::LocallyNextToFolder); });
    connect(profile.customLocationAction, &QAction::triggered, this, [this, &profile](){ switchVersioningLocation(profile, SyncProfile::CustomLocation); });
    connect(profile.customLocationPathAction, &QAction::triggered, this, [this, &profile](){ setVersioningLocationPath(profile); });
    connect(profile.databaseLocallyAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Locally); });
    connect(profile.databaseDecentralizedAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Decentralized); });
    connect(profile.fileMinSizeAction, &QAction::triggered, this, [this, &profile](){ setFileMinSize(profile); });
    connect(profile.fileMaxSizeAction, &QAction::triggered, this, [this, &profile](){ setFileMaxSize(profile); });
    connect(profile.movedFileMinSizeAction, &QAction::triggered, this, [this, &profile](){ setMovedFileMinSize(profile); });
    connect(profile.includeAction, &QAction::triggered, this, [this, &profile](){ setIncludeList(profile); });
    connect(profile.excludeAction, &QAction::triggered, this, [this, &profile](){ setExcludeList(profile); });
    connect(profile.ignoreHiddenFilesAction, &QAction::triggered, this, [this, &profile](){ toggleIgnoreHiddenFiles(profile); });
}


/*
===================
MainWindow::disconnectProfileMenu
===================
*/
void MainWindow::disconnectProfileMenu(SyncProfile &profile)
{
    disconnect(&profile.syncTimer, nullptr, nullptr, nullptr);
    disconnect(profile.manualAction, nullptr, nullptr, nullptr);
    disconnect(profile.automaticAdaptiveAction, nullptr, nullptr, nullptr);
    disconnect(profile.automaticFixedAction, nullptr, nullptr, nullptr);
    disconnect(profile.increaseSyncTimeAction, nullptr, nullptr, nullptr);
    disconnect(profile.decreaseSyncTimeAction, nullptr, nullptr, nullptr);
    disconnect(profile.fixedSyncingTimeAction, nullptr, nullptr, nullptr);
    disconnect(profile.detectMovedFilesAction, nullptr, nullptr, nullptr);
    disconnect(profile.moveToTrashAction, nullptr, nullptr, nullptr);
    disconnect(profile.versioningAction, nullptr, nullptr, nullptr);
    disconnect(profile.deletePermanentlyAction, nullptr, nullptr, nullptr);
    disconnect(profile.fileTimestampBeforeAction, nullptr, nullptr, nullptr);
    disconnect(profile.fileTimestampAfterAction, nullptr, nullptr, nullptr);
    disconnect(profile.folderTimestampAction, nullptr, nullptr, nullptr);
    disconnect(profile.lastVersionAction, nullptr, nullptr, nullptr);
    disconnect(profile.versioningPostfixAction, nullptr, nullptr, nullptr);
    disconnect(profile.versioningPatternAction, nullptr, nullptr, nullptr);
    disconnect(profile.locallyNextToFolderAction, nullptr, nullptr, nullptr);
    disconnect(profile.customLocationAction, nullptr, nullptr, nullptr);
    disconnect(profile.databaseLocallyAction, nullptr, nullptr, nullptr);
    disconnect(profile.databaseDecentralizedAction, nullptr, nullptr, nullptr);
    disconnect(profile.fileMinSizeAction, nullptr, nullptr, nullptr);
    disconnect(profile.fileMaxSizeAction, nullptr, nullptr, nullptr);
    disconnect(profile.movedFileMinSizeAction, nullptr, nullptr, nullptr);
    disconnect(profile.includeAction, nullptr, nullptr, nullptr);
    disconnect(profile.excludeAction, nullptr, nullptr, nullptr);
    disconnect(profile.ignoreHiddenFilesAction, nullptr, nullptr, nullptr);
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    syncApp->manager()->updateStatus();
    syncNowAction->setEnabled(syncApp->manager()->queue().size() != syncApp->manager()->existingProfiles());

    if (isVisible())
    {
        // Profiles
        for (size_t i = 0; i < syncApp->manager()->profiles().size(); i++)
        {
            QModelIndex index = profileModel->indexByRow(i);
            SyncProfile *profile = profileByIndex(index);

            if (!profile)
                continue;

            if (profile->toBeRemoved)
                continue;

            int posInQueue = syncApp->manager()->queue().indexOf(profile);

            if (posInQueue == 0)
                profileModel->setData(index, tr("Syncing"), QueueStatusRole);
            else if (posInQueue > 0)
                profileModel->setData(index, tr("In queue") + QString(" (%1)").arg(posInQueue), QueueStatusRole);
            else
                profileModel->setData(index, QString(""), QueueStatusRole);

            if (profile->paused)
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            else if (profile->syncing || (!profile->syncHidden && syncApp->manager()->queue().contains(profile)))
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            else if (!profile->folders.empty() && profile->countExistingFolders() < 2 && profile->folders.size() >= 2)
                profileModel->setData(index, iconRemove, Qt::DecorationRole);
            else if (profile->hasMissingFolders())
                profileModel->setData(index, iconWarning, Qt::DecorationRole);
            else if (profile->partiallySynchronized())
                profileModel->setData(index, iconDonePartial, Qt::DecorationRole);
            else
                profileModel->setData(index, iconDone, Qt::DecorationRole);

            ui->syncProfilesView->update(index);
        }

        // Folders
        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedRows()[0];
            SyncProfile *profile = profileByIndex(profileIndex);

            if (profile)
            {
                for (int i = 0; i < folderModel->rowCount(); i++)
                {
                    QModelIndex index = folderModel->indexByRow(i);
                    const SyncFolder *folder = profile->folderByIndex(index);

                    if (!folder)
                        continue;

                    if (folder->toBeRemoved)
                        continue;

                    QIcon *icon = nullptr;

                    if (folder->syncType == SyncFolder::TWO_WAY)
                        icon = &iconTwoWay;
                    else if (folder->syncType == SyncFolder::ONE_WAY)
                        icon = &iconOneWay;
                    else if (folder->syncType == SyncFolder::ONE_WAY_UPDATE)
                        icon = &iconOneWayUpdate;

                    if (icon)
                        folderModel->setData(index, *icon, SyncTypeRole);

                    if (folder->paused)
                        folderModel->setData(index, iconPause, Qt::DecorationRole);
                    else if (folder->syncing || (syncApp->manager()->queue().contains(profile) && !syncApp->manager()->syncing() && !profile->syncHidden))
                        folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                    else if (!folder->exists)
                        folderModel->setData(index, iconRemove, Qt::DecorationRole);
                    else if (folder->partiallySynchronized())
                        folderModel->setData(index, iconDonePartial, Qt::DecorationRole);
                    else
                        folderModel->setData(index, iconDone, Qt::DecorationRole);

                    ui->folderListView->update(index);
                }
            }
        }
    }

    bool paused = syncApp->manager()->paused();

    // Pause status
    for (const auto &profile : syncApp->manager()->profiles())
    {
        if (profile.toBeRemoved)
            continue;

        if (syncApp->manager()->paused())
            paused = true;

        if (!syncApp->manager()->paused())
        {
            paused = false;
            break;
        }
    }

    syncApp->manager()->setPaused(paused);

    // Tray & Icon
    if (syncApp->manager()->inPausedState())
    {
        syncApp->tray()->setIcon(syncApp->tray()->iconPause());
        setWindowIcon(syncApp->tray()->icon());

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconResume.cacheKey())
            pauseSyncingAction->setIcon(iconResume);

        pauseSyncingAction->setText("&" + tr("Resume Syncing"));
        syncApp->manager()->setPaused(true);
    }
    else
    {
        if (syncApp->manager()->syncing() || syncApp->manager()->hasManualSyncProfile())
        {
            syncApp->tray()->setIcon(syncApp->tray()->iconSync());
            setWindowIcon(syncApp->tray()->iconSync());
        }
        else if (syncApp->manager()->issue())
        {
            syncApp->tray()->setIcon(syncApp->tray()->iconIssue());
            setWindowIcon(syncApp->tray()->iconIssue());
        }
        else if (syncApp->manager()->warning())
        {
            syncApp->tray()->setIcon(syncApp->tray()->iconWarning());
            setWindowIcon(syncApp->tray()->iconWarning());
        }
        else
        {
            bool incomplete = false;

            for (auto &profile : syncApp->manager()->profiles())
                if (profile.partiallySynchronized())
                    incomplete = true;

            if (incomplete)
            {
                syncApp->tray()->setIcon(syncApp->tray()->iconDonePartial());
                setWindowIcon(syncApp->tray()->iconDonePartial());
            }
            else
            {
                syncApp->tray()->setIcon(syncApp->tray()->iconDone());
                setWindowIcon(syncApp->tray()->iconDone());
            }
        }

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconPause.cacheKey())
            pauseSyncingAction->setIcon(iconPause);

        pauseSyncingAction->setText("&" + tr("Pause Syncing"));
        syncApp->manager()->setPaused(false);
    }

    // Title
    if (syncApp->manager()->filesToSync())
    {
        syncApp->tray()->setToolTip(tr("Sync Manager - %1 files to synchronize").arg(syncApp->manager()->filesToSync()));
        setWindowTitle(tr("Sync Manager - %1 files to synchronize").arg(syncApp->manager()->filesToSync()));
    }
    else
    {
        syncApp->tray()->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
}

/*
===================
MainWindow::updateMenuMaxDiskTransferRate
===================
*/
void MainWindow::updateMenuMaxDiskTransferRate()
{
    QString text;

    if (syncApp->manager()->maxDiskTransferRate())
    {
        quint64 bytes = syncApp->manager()->maxDiskTransferRate() % 1024;
        quint64 kilobytes = (syncApp->manager()->maxDiskTransferRate() / 1024) % 1024;
        quint64 megabytes = (syncApp->manager()->maxDiskTransferRate() / 1024 / 1024) % 1024;
        quint64 gigabytes = (syncApp->manager()->maxDiskTransferRate() / 1024 / 1024/ 1024) % 1024;

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
MainWindow::updateMenuSyncTime
===================
*/
void MainWindow::updateMenuSyncTime(const SyncProfile &profile)
{
    quint64 syncEvery = 0;
    QAction *action = nullptr;

    if (profile.syncingMode() == SyncProfile::AutomaticAdaptive)
    {
        syncEvery = profile.syncEvery;
        action = profile.syncingTimeAction;
    }
    else if (profile.syncingMode() == SyncProfile::AutomaticFixed)
    {
        syncEvery = profile.syncIntervalFixed();
        action = profile.fixedSyncingTimeAction;
    }
    else
    {
        return;
    }

    QString text(tr("Synchronize Every") + ": ");
    quint64 seconds = (syncEvery / 1000) % 60;
    quint64 minutes = (syncEvery / 1000 / 60) % 60;
    quint64 hours = (syncEvery / 1000 / 60 / 60) % 24;
    quint64 days = (syncEvery / 1000 / 60 / 60 / 24);

    if (days)
        text.append(tr("%1 days").arg(QString::number(static_cast<float>(days) + static_cast<float>(hours) / 24.0f, 'f', 1)));
    else if (hours)
        text.append(tr("%1 hours").arg(QString::number(static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f, 'f', 1)));
    else if (minutes)
        text.append(tr("%1 minutes").arg(QString::number(static_cast<float>(minutes) + static_cast<float>(seconds) / 60.0f, 'f', 1)));
    else if (seconds)
        text.append(tr("%1 seconds").arg(seconds));

    action->setText(text);
}

/*
===================
MainWindow::updateProfileTooltip
===================
*/
void MainWindow::updateProfileTooltip(const SyncProfile &profile)
{
    QModelIndex index = indexByProfile(profile);
    QString nextSyncText;
    QString dateFormat("dddd, MMMM d, yyyy h:mm:ss AP");

    if (!index.isValid())
        return;

    if (profile.syncingMode() != SyncProfile::Manual)
    {
        nextSyncText.append("\n" + tr("Next Synchronization: "));
        QDateTime dateTime = profile.lastSyncDate;
        dateTime += std::chrono::duration<quint64, std::milli>(profile.syncEvery);
        nextSyncText.append(syncApp->toLocalizedDateTime(dateTime, dateFormat));
        nextSyncText.append(".");
    }

    if (!profile.countExistingFolders())
    {
        profileModel->setData(index, tr("The profile has no folders available."), Qt::ToolTipRole);
    }
    else if (!profile.lastSyncDate.isNull())
    {
        QString time(syncApp->toLocalizedDateTime(profile.lastSyncDate, dateFormat));
        QString text = tr("Last synchronization: %1.").arg(time) + nextSyncText;

        if (profile.partiallySynchronized())
            text.insert(0, tr("Partially synchronized!") + "\n\n");

        profileModel->setData(index, text, Qt::ToolTipRole);
    }
    else
    {
        QString text(tr("Haven't been synchronized yet."));
        profileModel->setData(index, text, Qt::ToolTipRole);
    }

    if (ui->syncProfilesView->selectionModel()->selectedIndexes().contains(index))
    {
        for (int i = 0; auto &folder : profile.folders)
        {
            if (!folder.exists)
            {
                folderModel->setData(folderModel->indexByRow(i), tr("The folder is currently unavailable."), Qt::ToolTipRole);
            }
            else if (!folder.lastSyncDate.isNull())
            {
                QString time(syncApp->toLocalizedDateTime(folder.lastSyncDate, dateFormat));
                QString text = QString("Last synchronization: %1.").arg(time) + nextSyncText;

                if (folder.partiallySynchronized())
                    text.insert(0, tr("Partially synchronized!") + "\n\n");

                folderModel->setData(folderModel->indexByRow(i), text, Qt::ToolTipRole);
            }
            else
            {
                folderModel->setData(folderModel->indexByRow(i), tr("Haven't been synchronized yet."), Qt::ToolTipRole);
            }

            i++;
        }
    }
}

/*
===================
MainWindow::setupMenus
===================
*/
void MainWindow::setupMenus()
{
    iconAdd.addFile(":/Images/IconAdd.png");
    iconDone.addFile(":/Images/IconDone.png");
    iconDonePartial.addFile(":/Images/IconDonePartial.png");
    iconPause.addFile(":/Images/IconPause.png");
    iconRemove.addFile(":/Images/IconRemove.png");
    iconResume.addFile(":/Images/IconResume.png");
    iconSettings.addFile(":/Images/IconSettings.png");
    iconSync.addFile(":/Images/IconSync.png");
    iconWarning.addFile(":/Images/IconWarning.png");
    iconTwoWay.addFile(":/Images/IconTwoWay.png");
    iconOneWay.addFile(":/Images/IconOneWay.png");
    iconOneWayUpdate.addFile(":/Images/IconOneWayUpdate.png");
    animSync.setFileName(":/Images/AnimSync.gif");

    syncNowAction = new QAction(iconSync, "&" + tr("Sync Now"), this);
    pauseSyncingAction = new QAction(iconPause, "&" + tr("Pause Syncing"), this);
    deltaCopyingAction = new QAction("&" + tr("File Delta Copying"), this);
    maximumDiskTransferRateAction = new QAction("&" + tr("Maximum Disk Transfer Rate") + QString(": %1").arg(syncApp->manager()->maxDiskTransferRate()), this);
    maximumCpuUsageAction = new QAction("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(syncApp->maxCpuUsage()), this);

    for (int i = 0; i < Application::languageCount(); i++)
    {
        languageActions.append(new QAction(tr(languages[i].name), this));
        languageActions[i]->setIcon(*(new QIcon(languages[i].flagPath)));
    }

    launchOnStartupAction = new QAction("&" + tr("Launch on Startup"), this);
    showInTrayAction = new QAction("&" + tr("Show in System Tray"));
    disableNotificationAction = new QAction("&" + tr("Disable Notifications"), this);
    checkForUpdatesAction = new QAction("&" + tr("Check for Updates"), this);
    userManualAction = new QAction("&" + tr("User Manual"), this);
    reportBugAction = new QAction("&" + tr("Report a Bug"), this);
    versionAction = new QAction(tr("Version: %1").arg(SYNCMANAGER_VERSION), this);

    versionAction->setDisabled(true);

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setCheckable(true);

    deltaCopyingAction->setCheckable(true);
    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    checkForUpdatesAction->setCheckable(true);

    updateLaunchOnStartupState();

    languageMenu = new UnhidableMenu("&" + tr("Language"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageMenu->addAction(languageActions[i]);

    performanceMenu = new UnhidableMenu("&" + tr("Performance"), this);
    performanceMenu->addAction(deltaCopyingAction);
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
    settingsMenu->addSeparator();
    settingsMenu->addAction(versionAction);

    syncApp->tray()->addMenu(settingsMenu);
    syncApp->tray()->addSeparator();
    syncApp->tray()->addAction(pauseSyncingAction);
    syncApp->tray()->addAction(syncNowAction);

    this->menuBar()->addAction(syncNowAction);
    this->menuBar()->addAction(pauseSyncingAction);
    this->menuBar()->addMenu(settingsMenu);
    this->menuBar()->setStyle(new MenuProxyStyle);

    updateAvailableButton = new QPushButton(tr("New Update Available"));
    updateAvailableButton->setStyleSheet("QPushButton { margin: 2px 5px 0px 0px; padding: 5px 8px }");
    updateAvailableButton->setVisible(false);

    QMenuBar *menuBar = this->menuBar();
    menuBar->setCornerWidget(updateAvailableButton);
    connect(updateAvailableButton, &QPushButton::clicked, this, [](){ QDesktopServices::openUrl(QUrl(LATEST_RELEASE_URL)); });
    connect(syncApp, &Application::updateFound, this, &MainWindow::updateAvailable);

#ifndef Q_OS_WIN
    QString styleSheet;

    // Fixes the issue of overly small menu buttons in the menu bar
    styleSheet.append("QMenuBar { padding: 5px 0px 5px 0px; } QMenuBar::item { padding: 5px 10px 5px 10px; border: none; }");

    // Fixes a disappearing icon when you click on its menu on Linux while using the Fusion style
    styleSheet.append("QMenuBar::item:selected { background: #e3e3e3; } QMenuBar::item:pressed { background: #e3e3e3; }");

    this->menuBar()->setStyleSheet(styleSheet);

    // Makes profiles/folders items slightly bigger
    ui->syncProfilesView->setStyleSheet("QListView::item { padding: 3px;}");
    ui->folderListView->setStyleSheet("QListView::item { padding: 3px; }");
#endif

    for (auto &profile : syncApp->manager()->profiles())
        profile.setupMenus(this);

    connect(syncNowAction, &QAction::triggered, this, [this](){ sync(nullptr); });
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(deltaCopyingAction, &QAction::triggered, this, [this](){ syncApp->manager()->setDeltaCopying(!syncApp->manager()->deltaCopying()); });
    connect(maximumDiskTransferRateAction, SIGNAL(triggered()), this, SLOT(setMaximumTransferRateUsage()));
    connect(maximumCpuUsageAction, SIGNAL(triggered()), this, SLOT(setMaximumCpuUsage()));

    for (int i = 0; i < Application::languageCount(); i++)
        connect(languageActions[i], &QAction::triggered, this, [i, this](){ switchLanguage(languages[i].language); });

    connect(syncApp, &Application::languageChanged, this, &MainWindow::updateLanguageMenu);
    connect(launchOnStartupAction, &QAction::triggered, this, &MainWindow::toggleLaunchOnStartup);
    connect(showInTrayAction, &QAction::triggered, this, &MainWindow::toggleShowInTray);
    connect(disableNotificationAction, &QAction::triggered, this, &MainWindow::toggleNotification);
    connect(checkForUpdatesAction, &QAction::triggered, this, &MainWindow::toggleCheckForUpdates);
    connect(userManualAction, &QAction::triggered, this, [](){ QDesktopServices::openUrl(QUrl::fromLocalFile(USER_MANUAL_PATH)); });
    connect(reportBugAction, &QAction::triggered, this, [](){ QDesktopServices::openUrl(QUrl(BUG_TRACKER_URL)); });

    for (auto &profile : syncApp->manager()->profiles())
        connectProfileMenu(profile);
}

/*
===================
MainWindow::updateLaunchOnStartupState
===================
*/
void MainWindow::updateLaunchOnStartupState()
{
#ifdef Q_OS_WIN
    QString path(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncsyncApp->manager.lnk");
    launchOnStartupAction->setChecked(QFile::exists(path));
#else
    QString path(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncsyncApp->manager.desktop");
    launchOnStartupAction->setChecked(QFile::exists(path));
#endif
}

/*
===================
MainWindow::profileByIndex
===================
*/
SyncProfile *MainWindow::profileByIndex(const QModelIndex &index)
{
    for (auto &profile : syncApp->manager()->profiles())
        if (profile.index == index)
            return &profile;

    return nullptr;
}

/*
===================
MainWindow::indexByProfile
===================
*/
QModelIndex MainWindow::indexByProfile(const SyncProfile &profile)
{
    for (int i = 0; i < profileModel->rowCount(); i++)
        if (profileModel->indexByRow(i) == profile.index)
            return profileModel->indexByRow(i);

    return QModelIndex();
}

/*
===================
MainWindow::profileIndexByName
===================
*/
QModelIndex MainWindow::profileIndexByName(const QString &name)
{
    for (int i = 0; i < profileModel->rowCount(); i++)
        if (profileModel->indexByRow(i).data(Qt::DisplayRole).toString()  == name)
            return profileModel->indexByRow(i);

    return QModelIndex();
}
