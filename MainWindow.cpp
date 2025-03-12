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
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

/*
===================
MainWindow::MainWindow
===================
*/
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->centralWidget->setLayout(ui->mainLayout);
    setWindowTitle("Sync Manager");
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    profileModel = new DecoratedStringListModel;
    folderModel = new DecoratedStringListModel;
    ui->syncProfilesView->setModel(profileModel);
    ui->folderListView->setModel(folderModel);

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
        manager.profiles().append(SyncProfile());

        SyncProfile &profile = manager.profiles().last();
        profile.paused = manager.isPaused();
        profile.name = name;

        QStringList paths = profilesData.value(name).toStringList();
        paths.sort();

        for (auto &path : paths)
        {
            profile.folders.append(SyncFolder());
            profile.folders.last().paused = manager.isPaused();
            profile.folders.last().path = path.toUtf8();
        }
    }

    connect(ui->syncProfilesView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(profileClicked(QItemSelection,QItemSelection)));
    connect(ui->syncProfilesView->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)), SLOT(profileNameChanged(QModelIndex)));
    connect(ui->syncProfilesView, SIGNAL(deletePressed()), SLOT(removeProfile()));
    connect(ui->folderListView, &FolderListView::drop, this, &MainWindow::addFolder);
    connect(ui->folderListView, SIGNAL(deletePressed()), SLOT(removeFolder()));
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(&manager, &SyncManager::warning, this, [this](QString title, QString message){ notify(title, message, QSystemTrayIcon::Critical); });
    connect(&manager, &SyncManager::profileSynced, this, &MainWindow::profileSynced);

    for (auto &profile : manager.profiles())
    {
        connect(&profile.syncTimer, &QChronoTimer::timeout, this, [&profile, this](){ sync(&const_cast<SyncProfile &>(profile), true); });
        profile.syncTimer.setSingleShot(true);
    }

    setupMenus();
    readSettings();
    retranslate();
    switchLanguage(language);
    setDatabaseLocation(manager.databaseLocation());
    updateMenuSyncTime();
    updateStatus();

    updateTimer.setSingleShot(true);
    appInitiated = true;

    for (auto &profile : manager.profiles())
        for (auto &folder : profile.folders)
            if (!folder.exists)
                notify(tr("Couldn't find folder"), folder.path, QSystemTrayIcon::Warning);
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
MainWindow::show
===================
*/
void MainWindow::show()
{
    if (QSystemTrayIcon::isSystemTrayAvailable() && showInTray)
    {
        trayIcon->show();
        syncApp->setQuitOnLastWindowClosed(false);
    }
    else
    {
        QMainWindow::show();
        trayIcon->hide();
        syncApp->setQuitOnLastWindowClosed(true);
    }
}

/*
===================
MainWindow::setTrayVisible
===================
*/
void MainWindow::setTrayVisible(bool visible)
{
    if (QSystemTrayIcon::isSystemTrayAvailable())
        showInTray = visible;
    else
        showInTray = false;

    show();
    saveSettings();
}

/*
===================
MainWindow::closeEvent
===================
*/
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (QSystemTrayIcon::isSystemTrayAvailable() && showInTray)
    {
        // Hides the window instead of closing as it can appear out of the screen after disconnecting a display.
        hide();
        event->ignore();
    }
    else
    {
        QString title(tr("Quit"));
        QString text(tr("Currently syncing. Are you sure you want to quit?"));

        if (manager.isBusy() && !questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
        {
            event->ignore();
            return;
        }

        manager.shouldQuit();
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

    for (auto &profile : manager.profiles())
        profileNames.append(profile.name);

    for (int i = 2; profileNames.contains(newName); i++)
    {
        newName = QString(tr(" (%1)")).arg(i);
        newName.insert(0, tr("New profile"));
    }

    manager.profiles().append(SyncProfile());
    manager.profiles().last().paused = manager.isPaused();
    manager.profiles().last().name = newName;
    profileNames.append(newName);
    profileModel->setStringList(profileNames);
    folderModel->setStringList(QStringList());

    connect(&manager.profiles().last().syncTimer, &QChronoTimer::timeout, this, [this](){ sync(&manager.profiles().last(), true); });

    QSettings profileData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileData.setValue(newName, folderModel->stringList());

    // Avoids reloading a newly added profile as it's already loaded.
    ui->syncProfilesView->selectionModel()->blockSignals(true);
    ui->syncProfilesView->setCurrentIndex(ui->syncProfilesView->model()->index(ui->syncProfilesView->model()->rowCount() - 1, 0));
    ui->syncProfilesView->selectionModel()->blockSignals(false);

    ui->folderListView->selectionModel()->reset();
    ui->folderListView->update();
    updateProfileTooltip(manager.profiles().last());
    updateStatus();
    manager.updateNextSyncingTime();
    manager.updateTimer(manager.profiles().last());
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

    QString title(tr("Remove profile"));
    QString text(tr("Are you sure you want to remove profile?"));

    if (!questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
        return;

    for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
    {
        SyncProfile &profile = manager.profiles()[index.row()];

        disconnect(&profile.syncTimer, &QChronoTimer::timeout, this, nullptr);
        ui->syncProfilesView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile.name + QLatin1String("_profile"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.remove(profile.name);

        profile.paused = true;
        profile.toBeRemoved = true;

        for (auto &folder : profile.folders)
        {
            folder.paused = true;
            folder.toBeRemoved = true;
            folder.removeDatabase();
        }

        if (!manager.isBusy())
            manager.profiles().remove(index.row());

        folderModel->setStringList(QStringList());
    }

    ui->syncProfilesView->selectionModel()->reset();
    updateStatus();
    manager.updateNextSyncingTime();
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

    for (auto &folder : manager.profiles()[ui->syncProfilesView->currentIndex().row()].folders)
        folderPaths.append(folder.path);

    folderModel->setStringList(folderPaths);
    updateProfileTooltip(manager.profiles()[ui->syncProfilesView->currentIndex().row()]);
    updateStatus();
}

/*
===================
MainWindow::profileNameChanged
===================
*/
void MainWindow::profileNameChanged(const QModelIndex &index)
{
    int row = index.row();
    QString newName = index.data(Qt::DisplayRole).toString();
    newName.remove('/');
    newName.remove('\\');

    QStringList profileNames;
    QStringList folderPaths;

    for (auto &profile : manager.profiles())
        profileNames.append(profile.name);

    for (auto &folder : manager.profiles()[row].folders)
        folderPaths.append(folder.path);

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(manager.profiles()[row].name, Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(index, manager.profiles()[row].name, Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(manager.profiles()[row].name + QLatin1String("_profile"));
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("Paused"), manager.profiles()[row].paused);
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("LastSyncDate"), manager.profiles()[row].lastSyncDate);

    for (auto &folder : manager.profiles()[row].folders)
        settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

    for (auto &folder : manager.profiles()[row].folders)
        settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_LastSyncDate"), folder.lastSyncDate);

    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profilesData.remove(manager.profiles()[row].name);
    profilesData.setValue(newName, folderPaths);

    manager.profiles()[row].name = newName;
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

    int row = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();
    SyncProfile &profile = manager.profiles()[row];
    QStringList folders;

    if (mimeData)
    {
        for (const auto &url : mimeData->urls())
        {
            if (!QFileInfo(url.toLocalFile()).isDir())
                continue;

            folders.append(url.toLocalFile());
        }
    }
    else
    {
        QString folderPath = QFileDialog::getExistingDirectory(this, tr("Browse For Folder"), QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (folderPath.isEmpty())
            return;

        folders.append(folderPath);
    }

    QStringList folderPaths;

    for (auto &folder : profile.folders)
        if (!folder.toBeRemoved)
            folderPaths.append(folder.path);

    // Checks if we already have a folder for synchronization in the list
    for (const auto &folder : folders)
    {
        bool exists = false;

        for (const auto &path : folderPaths)
        {
            int n = path.size() > folder.size() ? path.size() - 1 : folder.size() - 1;

            if ((manager.isCaseSensitiveSystem() && path.toStdString().compare(0, n, folder.toStdString()) == 0) ||
                (!manager.isCaseSensitiveSystem() && path.toLower().toStdString().compare(0, n, folder.toLower().toStdString()) == 0))
            {
                exists = true;
            }
        }

        if (!exists)
        {
            profile.folders.append(SyncFolder());
            profile.folders.last().paused = manager.isPaused();
            profile.folders.last().path = folder.toUtf8();
            profile.folders.last().path.append("/");
            folderPaths.append(profile.folders.last().path);

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(profile.name, folderPaths);

            folderModel->setStringList(folderPaths);
            updateStatus();
        }
    }

    updateProfileTooltip(profile);
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

    for (auto &index : ui->folderListView->selectionModel()->selectedIndexes())
    {
        int profileRow = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();
        SyncFolder &folder = manager.profiles()[profileRow].folders[index.row()];

        if (folder.syncing)
        {
            QString title(tr("Remove folder"));
            QString text(tr("The folder is currently syncing. Are you sure you want to remove it?"));

            if (!questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
                return;
        }

        folder.paused = true;
        folder.toBeRemoved = true;
        ui->folderListView->model()->removeRow(index.row());

        folder.removeDatabase();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(manager.profiles()[profileRow].name + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"));

        if (!manager.isBusy())
            manager.profiles()[profileRow].folders.remove(index.row());

        QStringList foldersPaths;

        for (auto &folder : manager.profiles()[profileRow].folders)
            foldersPaths.append(folder.path);

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(manager.profiles()[profileRow].name, foldersPaths);

        manager.updateTimer(manager.profiles()[profileRow]);
    }

    ui->folderListView->selectionModel()->reset();
    updateStatus();
    manager.updateNextSyncingTime();
}

/*
===================
MainWindow::pauseSyncing
===================
*/
void MainWindow::pauseSyncing()
{
    manager.setPaused(!manager.isPaused());

    for (auto &profile : manager.profiles())
    {
        profile.paused = manager.isPaused();

        for (auto &folder : profile.folders)
            folder.paused = manager.isPaused();

        if (profile.paused)
            profile.syncTimer.stop();
        else
            manager.updateTimer(profile);
    }

    updateStatus();
    saveSettings();
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
            int profileRow = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();

            for (auto &index : ui->folderListView->selectionModel()->selectedIndexes())
            {
                SyncFolder &folder = manager.profiles()[profileRow].folders[index.row()];
                folder.paused = !folder.paused;
            }

            manager.profiles()[profileRow].paused = true;

            for (const auto &folder : manager.profiles()[profileRow].folders)
                if (!folder.paused)
                    manager.profiles()[profileRow].paused = false;
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
            {
                SyncProfile &profile = manager.profiles()[index.row()];

                profile.paused = !profile.paused;

                for (auto &folder : profile.folders)
                    folder.paused = profile.paused;

                if (profile.paused)
                    profile.syncTimer.stop();
                else
                    manager.updateTimer(profile);
            }
        }

        updateStatus();
        saveSettings();
    }
}

/*
===================
MainWindow::quit
===================
*/
void MainWindow::quit()
{
    QString title(tr("Quit"));
    QString text(tr("Are you sure you want to quit?"));
    QString syncText(tr("Currently syncing. Are you sure you want to quit?"));

    if ((!manager.isBusy() && questionBox(QMessageBox::Question, title, text, QMessageBox::No, this)) ||
        (manager.isBusy() && questionBox(QMessageBox::Warning, title, syncText, QMessageBox::No, this)))
    {
        manager.shouldQuit();
        syncApp->quit();
    }
}

/*
===================
MainWindow::trayIconActivated
===================
*/
void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason)
    {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:

#ifdef Q_OS_LINUX
    // Double click doesn't work on GNOME
    case QSystemTrayIcon::MiddleClick:

        // Fixes wrong window position after hiding the window.
        if (isHidden()) move(pos().x() + (frameSize().width() - size().width()), pos().y() + (frameSize().height() - size().height()));
#endif

        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        QMainWindow::show();
        raise();
        activateWindow();

        break;
    default:
        break;
    }
}

/*
===================
MainWindow::switchSyncingMode
===================
*/
void MainWindow::switchSyncingMode(SyncManager::SyncingMode mode)
{
    manager.setSyncingMode(mode);
    automaticAction->setChecked(mode == SyncManager::Automatic);
    manualAction->setChecked(mode == SyncManager::Manual);
    pauseSyncingAction->setVisible(mode == SyncManager::Automatic);
    syncingTimeMenu->menuAction()->setVisible(mode == SyncManager::Automatic);

    if (mode == SyncManager::Manual)
    {
        for (auto &profile : manager.profiles())
            profile.syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        manager.updateNextSyncingTime();

        for (auto &profile : manager.profiles())
            manager.updateTimer(profile);
    }

    updateStatus();
    saveSettings();
}

/*
===================
MainWindow::switchDeletionMode
===================
*/
void MainWindow::switchDeletionMode(SyncManager::DeletionMode mode)
{
    if (appInitiated && mode == SyncManager::DeletePermanently && mode != manager.deletionMode())
    {
        QString title(tr("Switch deletion mode to delete files permanently?"));
        QString text(tr("Are you sure? Beware: this could lead to data loss!"));

        if (!questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
            mode = manager.deletionMode();
    }

    manager.setDeletionMode(mode);
    moveToTrashAction->setChecked(mode == SyncManager::MoveToTrash);
    versioningAction->setChecked(mode == SyncManager::Versioning);
    deletePermanentlyAction->setChecked(mode == SyncManager::DeletePermanently);

    saveSettings();
}

/*
===================
MainWindow::increaseSyncTime
===================
*/
void MainWindow::increaseSyncTime()
{
    quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

    for (auto &profile : manager.profiles())
    {
        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
        {
            increaseSyncTimeAction->setEnabled(false);
            return;
        }
    }

    manager.setSyncTimeMultiplier(manager.syncTimeMultiplier() + 1);
    decreaseSyncTimeAction->setEnabled(true);
    updateMenuSyncTime();

    for (auto &profile : manager.profiles())
    {
        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
            increaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateProfileTooltip(profile);
    }

    saveSettings();
}

/*
===================
MainWindow::decreaseSyncTime
===================
*/
void MainWindow::decreaseSyncTime()
{
    manager.setSyncTimeMultiplier(manager.syncTimeMultiplier() - 1);
    updateMenuSyncTime();

    for (auto &profile : manager.profiles())
    {
        quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

        // If exceeds the maximum value of an qint64
        if (profile.syncEvery < max)
            increaseSyncTimeAction->setEnabled(true);

        if (manager.syncTimeMultiplier() <= 1)
            decreaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateProfileTooltip(profile);
    }

    saveSettings();
}

/*
===================
MainWindow::switchLanguage
===================
*/
void MainWindow::switchLanguage(QLocale::Language language)
{
    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setChecked(language == languages[i].language);

    syncApp->setTranslator(language);
    this->language = language;
    retranslate();
}

/*
===================
MainWindow::launchOnStartup
===================
*/
void MainWindow::toggleLaunchOnStartup()
{
    launchOnStartupAction->setChecked(!launchOnStartupAction->isChecked());
    syncApp->setLaunchOnStartup(launchOnStartupAction->isChecked());
    saveSettings();
}

/*
===================
MainWindow::showInTray
===================
*/
void MainWindow::toggleShowInTray()
{
    setTrayVisible(!showInTray);
    saveSettings();
}

/*
===================
MainWindow::disableNotification
===================
*/
void MainWindow::toggleNotification()
{
    manager.enableNotifications(!manager.notificationsEnabled());
    saveSettings();
}

/*
===================
MainWindow::setDatabaseLocation
===================
*/
void MainWindow::setDatabaseLocation(SyncManager::DatabaseLocation location)
{
    saveDatabaseLocallyAction->setChecked(location == false);
    saveDatabaseDecentralizedAction->setChecked(location == true);
    manager.setDatabaseLocation(location);
    saveSettings();
}

/*
===================
MainWindow::toggleIgnoreHiddenFiles
===================
*/
void MainWindow::toggleIgnoreHiddenFiles()
{
    manager.enableIgnoreHiddenFiles(!manager.ignoreHiddenFilesEnabled());
    saveSettings();
}

/*
===================
MainWindow::detectMovedFiles
===================
*/
void MainWindow::toggleDetectMoved()
{
    manager.enableDetectMovedFiles(!manager.detectMovedFilesEnabled());
    saveSettings();
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
        menu.addAction(iconAdd, tr("&Add a new profile"), this, SLOT(addProfile()));

        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            int row = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();
            const SyncProfile &profile = manager.profiles()[row];

            if (profile.paused)
            {
                menu.addAction(iconResume, tr("&Resume syncing profile"), this, SLOT(pauseSelected()));
            }
            else
            {
                menu.addAction(iconPause, tr("&Pause syncing profile"), this, SLOT(pauseSelected()));

                QAction *action = menu.addAction(iconSync, tr("&Synchronize profile"), this, [&](){ sync(&const_cast<SyncProfile &>(profile), false); });
                action->setDisabled(manager.queue().contains(&profile));
            }

            menu.addAction(iconRemove, tr("&Remove profile"), this, SLOT(removeProfile()));
        }

        menu.popup(ui->syncProfilesView->mapToGlobal(pos));
    }
    // Folders
    else if (ui->folderListView->hasFocus())
    {
        if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
            return;

        int row = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();

        menu.addAction(iconAdd, tr("&Add a new folder"), this, SLOT(addFolder()));

        if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
        {
            if (manager.profiles()[row].folders[ui->folderListView->selectionModel()->selectedIndexes()[0].row()].paused)
                menu.addAction(iconResume, tr("&Resume syncing folder"), this, SLOT(pauseSelected()));
            else
                menu.addAction(iconPause, tr("&Pause syncing folder"), this, SLOT(pauseSelected()));

            menu.addAction(iconRemove, tr("&Remove folder"), this, SLOT(removeFolder()));
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
        if (manager.queue().contains(profile))
            return;

        profile->syncHidden = hidden;
    }
    else
    {
        for (auto &profile : manager.profiles())
            profile.syncHidden = false;
    }

    manager.addToQueue(profile);

    if (!manager.isBusy())
    {
        animSync.start();

        for (auto &action : syncingModeMenu->actions())
            action->setEnabled(false);

        for (auto &action : deletionModeMenu->actions())
            action->setEnabled(false);

        for (auto &action : databaseLocationMenu->actions())
            action->setEnabled(false);

        QFuture<void> future = QtConcurrent::run([&]() { manager.sync(); });

        while (!future.isFinished())
            updateApp();

        for (auto &action : syncingModeMenu->actions())
            action->setEnabled(true);

        for (auto &action : deletionModeMenu->actions())
            action->setEnabled(true);

        for (auto &action : databaseLocationMenu->actions())
            action->setEnabled(true);

        animSync.stop();

        updateStatus();
        updateMenuSyncTime();
    }
}

/*
===================
MainWindow::profileSynced
===================
*/
void MainWindow::profileSynced(SyncProfile *profile)
{
    manager.updateTimer(*profile);
    updateMenuSyncTime();
    updateProfileTooltip(*profile);
    saveSettings();
}

/*
===================
MainWindow::notify

QSystemTrayIcon doesn't display messages when hidden.
A quick workaround is to temporarily show the tray, display the message, and then re-hide it.
===================
*/
void MainWindow::notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon)
{
    if (!trayIcon->isSystemTrayAvailable() || !manager.notificationsEnabled())
        return;

    bool visible = trayIcon->isVisible();

    if (!visible)
        trayIcon->show();

    trayIcon->showMessage(title, message, icon, std::numeric_limits<int>::max());

    if (!visible)
        trayIcon->hide();
}

/*
===================
MainWindow::updateApp
===================
*/
bool MainWindow::updateApp()
{
    if (updateTimer.remainingTime() <= 0)
    {
        updateStatus();
        updateTimer.start(UPDATE_DELAY);
    }

    QApplication::processEvents();
    return manager.isQuitting();
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    manager.updateStatus();
    syncNowAction->setEnabled(manager.queue().size() != manager.existingProfiles());

    if (isVisible())
    {
        // Profiles
        for (int i = 0, j = 0; i < manager.profiles().size(); i++)
        {
            if (manager.profiles()[i].toBeRemoved)
                continue;

            SyncProfile &profile = manager.profiles()[i];
            QModelIndex index = profileModel->index(j, 0);

            if (profile.paused)
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            else if (profile.syncing || (!profile.syncHidden && manager.queue().contains(&profile)))
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            else if (!profile.hasExistingFolders() && !profile.folders.isEmpty())
                profileModel->setData(index, iconRemove, Qt::DecorationRole);
            else if (profile.hasMissingFolders())
                profileModel->setData(index, iconWarning, Qt::DecorationRole);
            else
                profileModel->setData(index, iconDone, Qt::DecorationRole);

            ui->syncProfilesView->update(index);
            j++;
        }

        // Folders
        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            int row = ui->syncProfilesView->selectionModel()->selectedRows()[0].row();
            SyncProfile &profile = manager.profiles()[row];

            for (int i = 0, j = 0; i < profile.folders.size(); i++)
            {
                if (profile.folders[j].toBeRemoved)
                    continue;

                SyncFolder &folder = profile.folders[i];
                QModelIndex index = folderModel->index(i, 0);

                if (folder.paused)
                    folderModel->setData(index, iconPause, Qt::DecorationRole);
                else if (folder.syncing || (manager.queue().contains(&profile) && !manager.isSyncing() && !profile.syncHidden))
                    folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                else if (!folder.exists)
                    folderModel->setData(index, iconRemove, Qt::DecorationRole);
                else
                    folderModel->setData(index, iconDone, Qt::DecorationRole);

                ui->folderListView->update(index);
                j++;
            }
        }
    }

    // Pause status
    for (const auto &profile : manager.profiles())
    {
        manager.setPaused(profile.paused);

        if (!manager.isPaused())
            break;
    }

    // Tray & Icon
    if (manager.syncingMode() == SyncManager::Automatic && manager.isPaused())
    {
        trayIcon->setIcon(trayIconPause);
        setWindowIcon(trayIconPause);

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconResume.cacheKey())
            pauseSyncingAction->setIcon(iconResume);

        pauseSyncingAction->setText(tr("&Resume Syncing"));
    }
    else
    {
        if (manager.isSyncing() || (!manager.isThereProfileWithHiddenSync() && !manager.queue().empty()))
        {
            if (trayIcon->icon().cacheKey() != trayIconSync.cacheKey())
                trayIcon->setIcon(trayIconSync);

            if (windowIcon().cacheKey() != trayIconSync.cacheKey())
                setWindowIcon(trayIconSync);
        }
        else if (manager.isThereWarning())
        {
            if (manager.isThereIssue())
            {
                trayIcon->setIcon(trayIconIssue);
                setWindowIcon(trayIconIssue);
            }
            else
            {
                trayIcon->setIcon(trayIconWarning);
                setWindowIcon(trayIconWarning);
            }
        }
        else
        {
            trayIcon->setIcon(trayIconDone);
            setWindowIcon(trayIconDone);
        }

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconPause.cacheKey())
            pauseSyncingAction->setIcon(iconPause);

        pauseSyncingAction->setText(tr("&Pause Syncing"));
    }

    // Title
    if (manager.filesToSync())
    {
        trayIcon->setToolTip(QString(tr("Sync Manager - %1 files to synchronize")).arg(manager.filesToSync()));
        setWindowTitle(QString(tr("Sync Manager - %1 files to synchronize")).arg(manager.filesToSync()));
    }
    else
    {
        trayIcon->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
}

/*
===================
MainWindow::updateMenuSyncTime
===================
*/
void MainWindow::updateMenuSyncTime()
{
    quint64 syncEvery = 0;
    QString text(tr("Average Synchronization Time: "));

    for (auto &profile : manager.profiles())
        syncEvery += profile.syncEvery / manager.profiles().size();

    quint64 seconds = (syncEvery / 1000) % 60;
    quint64 minutes = (syncEvery / 1000 / 60) % 60;
    quint64 hours = (syncEvery / 1000 / 60 / 60) % 24;
    quint64 days = (syncEvery / 1000 / 60 / 60 / 24);

    if (days)
        text.append(QString(tr("%1 days")).arg(QString::number(static_cast<float>(days) + static_cast<float>(hours) / 24.0f, 'f', 1)));
    else if (hours)
        text.append(QString(tr("%1 hours")).arg(QString::number(static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f, 'f', 1)));
    else if (minutes)
        text.append(QString(tr("%1 minutes")).arg(QString::number(static_cast<float>(minutes) + static_cast<float>(seconds) / 60.0f, 'f', 1)));
    else if (seconds)
        text.append(QString(tr("%1 seconds")).arg(seconds));

    syncingTimeAction->setText(text);
}

/*
===================
MainWindow::updateProfileTooltip
===================
*/
void MainWindow::updateProfileTooltip(const SyncProfile &profile)
{
    QModelIndex index = profileIndex(profile);

    if (!index.isValid())
        return;

    QString nextSyncText("\n");
    nextSyncText.append(tr("Next Synchronization: "));
    QString dateFormat("dddd, MMMM d, yyyy h:mm:ss AP");
    QDateTime dateTime = profile.lastSyncDate;
    dateTime += std::chrono::duration<quint64, std::milli>(profile.syncEvery);
    nextSyncText.append(syncApp->toLocalizedDateTime(dateTime, dateFormat));
    nextSyncText.append(".");

    if (!profile.hasExistingFolders())
    {
        profileModel->setData(index, tr("The profile has no folders available."), Qt::ToolTipRole);
    }
    else if (!profile.lastSyncDate.isNull())
    {
        QString time(syncApp->toLocalizedDateTime(profile.lastSyncDate, dateFormat));
        QString text = QString(tr("Last synchronization: %1.")).arg(time) + nextSyncText;
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
                folderModel->setData(folderModel->index(i, 0), tr("The folder is currently unavailable."), Qt::ToolTipRole);
            }
            else if (!folder.lastSyncDate.isNull())
            {
                QString time(syncApp->toLocalizedDateTime(folder.lastSyncDate, dateFormat));
                QString text = QString("Last synchronization: %1.").arg(time) + nextSyncText;
                folderModel->setData(folderModel->index(i, 0), text, Qt::ToolTipRole);
            }
            else
            {
                folderModel->setData(folderModel->index(i, 0), tr("Haven't been synchronized yet."), Qt::ToolTipRole);
            }

            i++;
        }
    }
}

/*
===================
MainWindow::readSettings
===================
*/
void MainWindow::readSettings()
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

    showInTray = settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool();
    language = static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt());

    for (int i = 0; i < manager.profiles().size(); i++)
    {
        QString profileKeyPath(profileModel->index(i).data(Qt::DisplayRole).toString() + QLatin1String("_profile/"));
        SyncProfile &profile = manager.profiles()[i];

        // Loads saved pause states and checks if synchronization folders exist
        profile.paused = settings.value(profileKeyPath + QLatin1String("Paused"), false).toBool();

        if (!profile.paused)
            manager.setPaused(false);

        // Loads last sync dates for all profiles
        profile.lastSyncDate = settings.value(profileKeyPath + QLatin1String("LastSyncDate")).toDateTime();
        profile.syncTime = settings.value(profile.name + QLatin1String("_profile/") + QLatin1String("SyncTime"), 0).toULongLong();

        for (auto &folder : profile.folders)
        {
            folder.paused = settings.value(profileKeyPath + folder.path + QLatin1String("_Paused"), false).toBool();

            if (!folder.paused)
                manager.setPaused(false);

            folder.exists = QFileInfo::exists(folder.path);
            folder.lastSyncDate = settings.value(profileKeyPath + folder.path + QLatin1String("_LastSyncDate")).toDateTime();
        }

        // Loads exclude list
        for (auto &exclude : settings.value(profileKeyPath + QLatin1String("ExcludeList")).toStringList())
            profile.excludeList.append(exclude.toUtf8());
    }

    databaseLocationMenu->setEnabled(manager.databaseLocation());
    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!manager.notificationsEnabled());
    ignoreHiddenFilesAction->setChecked(manager.ignoreHiddenFilesEnabled());
    detectMovedFilesAction->setChecked(manager.detectMovedFilesEnabled());
    switchSyncingMode(static_cast<SyncManager::SyncingMode>(settings.value("SyncingMode", SyncManager::Automatic).toInt()));
    switchDeletionMode(static_cast<SyncManager::DeletionMode>(settings.value("DeletionMode", manager.MoveToTrash).toInt()));

    manager.updateNextSyncingTime();

    for (auto &profile : manager.profiles())
    {
        quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
            increaseSyncTimeAction->setEnabled(false);

        updateProfileTooltip(profile);
    }
}

/*
===================
MainWindow::saveSettings
===================
*/
void MainWindow::saveSettings() const
{
    if (!appInitiated)
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QVariantList hSizes;

    for (auto &size : ui->horizontalSplitter->sizes())
        hSizes.append(size);

    if (!isMaximized())
    {
        settings.setValue("Width", size().width());
        settings.setValue("Height", size().height());
    }

    settings.setValue("Fullscreen", isMaximized());
    settings.setValue("HorizontalSplitter", hSizes);
    settings.setValue("ShowInTray", showInTray);
    settings.setValue("Paused", manager.isPaused());
    settings.setValue("SyncingMode", manager.syncingMode());
    settings.setValue("DeletionMode", manager.deletionMode());
    settings.setValue("DatabaseLocation", manager.databaseLocation());
    settings.setValue("Language", language);
    settings.setValue("Notifications", manager.notificationsEnabled());
    settings.setValue("IgnoreHiddenFiles", manager.ignoreHiddenFilesEnabled());
    settings.setValue("DetectMovedFiles", manager.detectMovedFilesEnabled());
    settings.setValue("SyncTimeMultiplier", manager.syncTimeMultiplier());
    settings.setValue("MovedFileMinSize", manager.movedFileMinSize());
    settings.setValue("caseSensitiveSystem", manager.isCaseSensitiveSystem());
    settings.setValue("VersionFolder", manager.versionFolder());
    settings.setValue("VersionPattern", manager.versionPattern());

    // Saves profiles/folders pause states and last sync dates
    for (auto &profile : manager.profiles())
    {
        if (profile.toBeRemoved)
            continue;

        QString profileKeyPath(profile.name + QLatin1String("_profile/"));

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved)
                continue;

            settings.setValue(profileKeyPath + folder.path + QLatin1String("_LastSyncDate"), profile.lastSyncDate);
            settings.setValue(profileKeyPath + folder.path + QLatin1String("_Paused"), folder.paused);
        }

        settings.setValue(profileKeyPath + QLatin1String("Paused"), profile.paused);
        settings.setValue(profileKeyPath + QLatin1String("LastSyncDate"), profile.lastSyncDate);
        settings.setValue(profileKeyPath + QLatin1String("SyncTime"), profile.syncTime);
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
    iconPause.addFile(":/Images/IconPause.png");
    iconRemove.addFile(":/Images/IconRemove.png");
    iconResume.addFile(":/Images/IconResume.png");
    iconSettings.addFile(":/Images/IconSettings.png");
    iconSync.addFile(":/Images/IconSync.png");
    iconWarning.addFile(":/Images/IconWarning.png");
    trayIconDone.addFile(":/Images/TrayIconDone.png");
    trayIconIssue.addFile(":/Images/TrayIconIssue.png");
    trayIconPause.addFile(":/Images/TrayIconPause.png");
    trayIconSync.addFile(":/Images/TrayIconSync.png");
    trayIconWarning.addFile(":/Images/TrayIconWarning.png");
    animSync.setFileName(":/Images/AnimSync.gif");

    syncNowAction = new QAction(iconSync, tr("&Sync Now"), this);
    pauseSyncingAction = new QAction(iconPause, tr("&Pause Syncing"), this);
    automaticAction = new QAction(tr("&Automatic"), this);
    manualAction = new QAction(tr("&Manual"), this);
    increaseSyncTimeAction = new QAction(tr("&Increase"), this);
    syncingTimeAction = new QAction(tr("Synchronize Every: "), this);
    decreaseSyncTimeAction = new QAction(tr("&Decrease"), this);
    moveToTrashAction = new QAction(tr("&Move Files to Trash"), this);
    saveDatabaseLocallyAction = new QAction(tr("&Locally (On the local machine)"), this);
    saveDatabaseDecentralizedAction = new QAction(tr("&Decentralized (Inside synchronization folders)"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions.append(new QAction(tr(languages[i].name), this));

    versioningAction = new QAction(tr("&Versioning"), this);
    deletePermanentlyAction = new QAction(tr("&Delete Files Permanently"), this);
    launchOnStartupAction = new QAction(tr("&Launch on Startup"), this);
    showInTrayAction = new QAction(tr("&Show in System Tray"));
    disableNotificationAction = new QAction(tr("&Disable Notifications"), this);
    ignoreHiddenFilesAction = new QAction(tr("&Ignore Hidden Files"), this);
    detectMovedFilesAction = new QAction(tr("&Detect Renamed and Moved Files"), this);
    showAction = new QAction(tr("&Show"), this);
    quitAction = new QAction(tr("&Quit"), this);
    version = new QAction(QString(tr("Version: %1")).arg(SYNCMANAGER_VERSION), this);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(manager.syncTimeMultiplier() <= 1);
    version->setDisabled(true);

    increaseSyncTimeAction->setEnabled(true);
    decreaseSyncTimeAction->setEnabled(manager.syncTimeMultiplier() > 1);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);
    deletePermanentlyAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    versioningAction->setCheckable(true);
    saveDatabaseLocallyAction->setCheckable(true);
    saveDatabaseDecentralizedAction->setCheckable(true);

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setCheckable(true);

    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    ignoreHiddenFilesAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);

#ifdef Q_OS_WIN
    QString path(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk");
    launchOnStartupAction->setChecked(QFile::exists(path));
#else
    QString path(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop");
    launchOnStartupAction->setChecked(QFile::exists(path));
#endif

    syncingModeMenu = new UnhidableMenu(tr("&Syncing Mode"), this);
    syncingModeMenu->addAction(automaticAction);
    syncingModeMenu->addAction(manualAction);

    syncingTimeMenu = new UnhidableMenu(tr("&Syncing Time"), this);
    syncingTimeMenu->addAction(increaseSyncTimeAction);
    syncingTimeMenu->addAction(syncingTimeAction);
    syncingTimeMenu->addAction(decreaseSyncTimeAction);

    deletionModeMenu = new UnhidableMenu(tr("&Deletion Mode"), this);
    deletionModeMenu->addAction(moveToTrashAction);
    deletionModeMenu->addAction(versioningAction);
    deletionModeMenu->addAction(deletePermanentlyAction);

    languageMenu = new UnhidableMenu(tr("&Language"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageMenu->addAction(languageActions[i]);

    databaseLocationMenu = new UnhidableMenu(tr("&Database Location"), this);
    databaseLocationMenu->addAction(saveDatabaseLocallyAction);
    databaseLocationMenu->addAction(saveDatabaseDecentralizedAction);

    settingsMenu = new UnhidableMenu(tr("&Settings"), this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(syncingModeMenu);
    settingsMenu->addMenu(syncingTimeMenu);
    settingsMenu->addMenu(deletionModeMenu);
    settingsMenu->addMenu(databaseLocationMenu);
    settingsMenu->addMenu(languageMenu);
    settingsMenu->addAction(launchOnStartupAction);
    settingsMenu->addAction(showInTrayAction);
    settingsMenu->addAction(disableNotificationAction);
    settingsMenu->addAction(ignoreHiddenFilesAction);
    settingsMenu->addAction(detectMovedFilesAction);
    settingsMenu->addSeparator();
    settingsMenu->addAction(version);

    trayIconMenu = new QMenu(this);
    trayIconMenu->addAction(syncNowAction);
    trayIconMenu->addAction(pauseSyncingAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addMenu(settingsMenu);

#ifdef Q_OS_LINUX
    trayIconMenu->addAction(showAction);
#endif

    trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setToolTip("Sync Manager");
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setIcon(trayIconDone);

    this->menuBar()->addAction(syncNowAction);
    this->menuBar()->addAction(pauseSyncingAction);
    this->menuBar()->addMenu(settingsMenu);
    this->menuBar()->setStyle(new MenuProxyStyle);

    connect(syncNowAction, &QAction::triggered, this, [this](){ sync(nullptr); });
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(automaticAction, &QAction::triggered, this, [this](){ switchSyncingMode(SyncManager::Automatic); });
    connect(manualAction, &QAction::triggered, this, [this](){ switchSyncingMode(SyncManager::Manual); });
    connect(moveToTrashAction, &QAction::triggered, this, [this](){ switchDeletionMode(manager.MoveToTrash); });
    connect(versioningAction, &QAction::triggered, this, [this](){ switchDeletionMode(manager.Versioning); });
    connect(deletePermanentlyAction, &QAction::triggered, this, [this](){ switchDeletionMode(manager.DeletePermanently); });
    connect(increaseSyncTimeAction, &QAction::triggered, this, &MainWindow::increaseSyncTime);
    connect(decreaseSyncTimeAction, &QAction::triggered, this, &MainWindow::decreaseSyncTime);
    connect(saveDatabaseLocallyAction, &QAction::triggered, this, [this](){ setDatabaseLocation(SyncManager::Locally); });
    connect(saveDatabaseDecentralizedAction, &QAction::triggered, this, [this](){ setDatabaseLocation(SyncManager::Decentralized); });

    for (int i = 0; i < Application::languageCount(); i++)
        connect(languageActions[i], &QAction::triggered, this, [i, this](){ switchLanguage(languages[i].language); });

    connect(launchOnStartupAction, &QAction::triggered, this, &MainWindow::toggleLaunchOnStartup);
    connect(showInTrayAction, &QAction::triggered, this, &MainWindow::toggleShowInTray);
    connect(disableNotificationAction, &QAction::triggered, this, &MainWindow::toggleNotification);
    connect(ignoreHiddenFilesAction, &QAction::triggered, this, &MainWindow::toggleIgnoreHiddenFiles);
    connect(detectMovedFilesAction, &QAction::triggered, this, &MainWindow::toggleDetectMoved);
    connect(showAction, &QAction::triggered, this, [this](){ trayIconActivated(QSystemTrayIcon::DoubleClick); });
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
}

/*
===================
MainWindow::retranslate
===================
*/
void MainWindow::retranslate()
{
    syncNowAction->setText(tr("&Sync Now"));
    pauseSyncingAction->setText(tr("&Pause Syncing"));
    automaticAction->setText(tr("&Automatic"));
    manualAction->setText(tr("&Manual"));
    increaseSyncTimeAction->setText(tr("&Increase"));
    syncingTimeAction->setText(tr("Synchronize Every:"));
    decreaseSyncTimeAction->setText(tr("&Decrease"));
    moveToTrashAction->setText(tr("&Move Files to Trash"));
    saveDatabaseLocallyAction->setText(tr("&Locally (On the local machine)"));
    saveDatabaseDecentralizedAction->setText(tr("&Decentralized (Inside synchronization folders)"));

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setText(tr(languages[i].name));

    versioningAction->setText(tr("&Versioning"));
    deletePermanentlyAction->setText(tr("&Delete Files Permanently"));
    launchOnStartupAction->setText(tr("&Launch on Startup"));
    showInTrayAction->setText(tr("&Show in System Tray"));
    disableNotificationAction->setText(tr("&Disable Notifications"));
    ignoreHiddenFilesAction->setText(tr("&Ignore Hidden Files"));
    detectMovedFilesAction->setText(tr("&Detect Renamed and Moved Files"));
    showAction->setText(tr("&Show"));
    quitAction->setText(tr("&Quit"));
    version->setText(QString(tr("Version: %1")).arg(SYNCMANAGER_VERSION));

    syncingModeMenu->setTitle(tr("&Syncing Mode"));
    syncingTimeMenu->setTitle(tr("&Syncing Time"));
    deletionModeMenu->setTitle(tr("&Deletion Mode"));
    databaseLocationMenu->setTitle(tr("&Database Location"));
    languageMenu->setTitle(tr("&Language"));
    settingsMenu->setTitle(tr("&Settings"));

    syncNowAction->setToolTip(tr("&Sync Now"));
    pauseSyncingAction->setToolTip(tr("&Pause Syncing"));
    ui->SyncLabel->setText(tr("Synchronization profiles:"));
    ui->foldersLabel->setText(tr("Folders to synchronize:"));

    updateStatus();
    updateMenuSyncTime();

    for (auto &profile : manager.profiles())
        updateProfileTooltip(profile);
}

/*
===================
MainWindow::profileIndex
===================
*/
QModelIndex MainWindow::profileIndex(const SyncProfile &profile)
{
    for (int i = 0; i < profileModel->rowCount(); i++)
        if (profileModel->index(i, 0).data(Qt::DisplayRole).toString() == profile.name)
            return profileModel->index(i, 0);

    return QModelIndex();
}
