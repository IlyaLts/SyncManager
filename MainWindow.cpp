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
        manager.profiles().append(SyncProfile(manager.isPaused()));
        manager.profiles().last().name = name;

        QStringList paths = profilesData.value(name).toStringList();
        paths.sort();

        for (const auto &path : paths)
        {
            manager.profiles().last().folders.append(SyncFolder(manager.isPaused()));
            manager.profiles().last().folders.last().path = path.toUtf8();
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
    connect(&manager, &SyncManager::profileSynced, this, [this](SyncProfile *profile){ manager.updateTimer(*profile);updateSyncTime(); updateLastSyncTime(profile); saveSettings(); });

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
    updateSyncTime();
    updateStatus();

    if (!manager.saveDatabaseEnabled() || fileDataOutdated)
        manager.removeFileData();

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
    saveDatabaseAction = new QAction(tr("&Save Files Data (Requires disk space)"), this);
    saveDatabaseLocallyAction = new QAction(tr("&Locally (On the local machine)"), this);
    saveDatabaseDecentralizedAction = new QAction(tr("&Decentralized (Inside synchronization folders)"), this);

    for (int i = 0; i < languageCount(); i++)
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
    saveDatabaseAction->setCheckable(true);
    saveDatabaseLocallyAction->setCheckable(true);
    saveDatabaseDecentralizedAction->setCheckable(true);

    for (int i = 0; i < languageCount(); i++)
        languageActions[i]->setCheckable(true);

    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    ignoreHiddenFilesAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);

#ifdef Q_OS_WIN
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk"));
#else
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop"));
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

    for (int i = 0; i < languageCount(); i++)
        languageMenu->addAction(languageActions[i]);

    databaseLocationMenu = new UnhidableMenu(tr("&Database Location"), this);
    databaseLocationMenu->addAction(saveDatabaseLocallyAction);
    databaseLocationMenu->addAction(saveDatabaseDecentralizedAction);

    databaseMenu = new UnhidableMenu(tr("&Database"), this);
    databaseMenu->addAction(saveDatabaseAction);
    databaseMenu->addMenu(databaseLocationMenu);

    settingsMenu = new UnhidableMenu(tr("&Settings"), this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(syncingModeMenu);
    settingsMenu->addMenu(syncingTimeMenu);
    settingsMenu->addMenu(deletionModeMenu);
    settingsMenu->addMenu(databaseMenu);
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
    connect(automaticAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, SyncManager::Automatic));
    connect(manualAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, SyncManager::Manual));
    connect(moveToTrashAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.MoveToTrash));
    connect(versioningAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.Versioning));
    connect(deletePermanentlyAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.DeletePermanently));
    connect(increaseSyncTimeAction, &QAction::triggered, this, &MainWindow::increaseSyncTime);
    connect(decreaseSyncTimeAction, &QAction::triggered, this, &MainWindow::decreaseSyncTime);
    connect(saveDatabaseAction, &QAction::triggered, this, &MainWindow::toggleSaveDatabase);
    connect(saveDatabaseLocallyAction, &QAction::triggered, this, std::bind(&MainWindow::setDatabaseLocation, this, SyncManager::Locally));
    connect(saveDatabaseDecentralizedAction, &QAction::triggered, this, std::bind(&MainWindow::setDatabaseLocation, this, SyncManager::Decentralized));

    for (int i = 0; i < languageCount(); i++)
        connect(languageActions[i], &QAction::triggered, this, std::bind(&MainWindow::switchLanguage, this, languages[i].language));

    connect(launchOnStartupAction, &QAction::triggered, this, &MainWindow::toggleLaunchOnStartup);
    connect(showInTrayAction, &QAction::triggered, this, &MainWindow::toggleShowInTray);
    connect(disableNotificationAction, &QAction::triggered, this, &MainWindow::toggleNotification);
    connect(ignoreHiddenFilesAction, &QAction::triggered, this, &MainWindow::toggleIgnoreHiddenFiles);
    connect(detectMovedFilesAction, &QAction::triggered, this, &MainWindow::toggleDetectMoved);
    connect(showAction, &QAction::triggered, this, std::bind(&MainWindow::trayIconActivated, this, QSystemTrayIcon::DoubleClick));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
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

    manager.profiles().append(SyncProfile(manager.isPaused()));
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
    updateLastSyncTime(&manager.profiles().last());
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
            manager.removeFileData(folder);
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
    updateLastSyncTime(&manager.profiles()[ui->syncProfilesView->currentIndex().row()]);
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

    for (auto &folder : manager.profiles()[row].folders)
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
            manager.profiles()[row].folders.append(SyncFolder(manager.isPaused()));
            manager.profiles()[row].folders.last().path = folder.toUtf8();
            manager.profiles()[row].folders.last().path.append("/");
            folderPaths.append(manager.profiles()[row].folders.last().path);

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(manager.profiles()[row].name, folderPaths);

            folderModel->setStringList(folderPaths);
            updateStatus();
        }
    }

    updateLastSyncTime(&manager.profiles()[row]);
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

        manager.removeFileData(folder);

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

        if (!questionBox(QMessageBox::Warning, title, text, QMessageBox::Yes, this))
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
    for (auto &profile : manager.profiles())
    {
        if (profile.syncEvery >= std::numeric_limits<int>::max())
        {
            increaseSyncTimeAction->setEnabled(false);
            return;
        }
    }

    manager.setSyncTimeMultiplier(manager.syncTimeMultiplier() + 1);
    decreaseSyncTimeAction->setEnabled(true);
    manager.updateNextSyncingTime();
    updateSyncTime();

    for (auto &profile : manager.profiles())
    {
        if (profile.syncEvery >= std::numeric_limits<int>::max())
            increaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateLastSyncTime(&profile);
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
    manager.updateNextSyncingTime();
    updateSyncTime();

    for (auto &profile : manager.profiles())
    {
        if (profile.syncEvery < std::numeric_limits<int>::max())
            increaseSyncTimeAction->setEnabled(true);

        if (manager.syncTimeMultiplier() <= 1)
            decreaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateLastSyncTime(&profile);
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
    for (int i = 0; i < languageCount(); i++)
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
void MainWindow::toggleSaveDatabase()
{
    if (manager.saveDatabaseEnabled())
    {
        QString title(tr("Disable database saving?"));
        QString text(tr("Are you sure? Beware: without database files, SyncManager will not detect removed and moved files!"));

        if (!questionBox(QMessageBox::Warning, title, text, QMessageBox::Yes, this))
        {
            saveDatabaseAction->setChecked(true);
            return;
        }
    }

    manager.enableDatabaseSaving(!manager.saveDatabaseEnabled());
    databaseLocationMenu->setEnabled(manager.saveDatabaseEnabled());
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
MainWindow::updateSyncTime
===================
*/
void MainWindow::updateSyncTime()
{
    qint64 syncEvery = 0;
    QString text(tr("Average Synchronization Time: "));

    for (auto &profile : manager.profiles())
        syncEvery += profile.syncEvery / manager.profiles().size();

    int seconds = (syncEvery / 1000) % 60;
    int minutes = (syncEvery / 1000 / 60) % 60;
    int hours = (syncEvery / 1000 / 60 / 60) % 24;
    int days = (syncEvery / 1000 / 60 / 60 / 24);

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
MainWindow::updateLastSyncTime
===================
*/
void MainWindow::updateLastSyncTime(SyncProfile *profile)
{
    for (int i = 0; i < profileModel->rowCount(); i++)
    {
        if (profileModel->index(i, 0).data(Qt::DisplayRole).toString() == profile->name)
        {
            bool hasFolders = false;

            for (const auto &folder : manager.profiles()[i].folders)
                if (folder.exists)
                    hasFolders = true;

            QString nextSyncText("\n");
            nextSyncText.append(tr("Next Synchronization: "));
            QString dateFormat("dddd, MMMM d, yyyy h:mm:ss AP");
            QDateTime dateTime = profile->lastSyncDate;
            dateTime += std::chrono::duration_cast<std::chrono::duration<quint64, std::milli>>(profile->syncTimer.remainingTime());
            nextSyncText.append(syncApp->toLocalizedDateTime(dateTime, dateFormat));
            nextSyncText.append(".");

            if (!hasFolders)
            {
                profileModel->setData(profileModel->index(i, 0), tr("The profile has no folders available."), Qt::ToolTipRole);
            }
            else if (!manager.profiles()[i].lastSyncDate.isNull())
            {
                QString time(syncApp->toLocalizedDateTime(profile->lastSyncDate, dateFormat));
                QString text = QString(tr("Last synchronization: %1.")).arg(time);
                text.append(nextSyncText);
                profileModel->setData(profileModel->index(i, 0), text, Qt::ToolTipRole);
            }
            else
            {
                QString text(tr("Haven't been synchronized yet."));
                text.append(nextSyncText);
                profileModel->setData(profileModel->index(i, 0), text, Qt::ToolTipRole);
            }

            if (ui->syncProfilesView->selectionModel()->selectedIndexes().contains(profileModel->index(i, 0)))
            {
                for (int j = 0; auto &folder : profile->folders)
                {
                    if (!folder.exists)
                    {
                        folderModel->setData(folderModel->index(j, 0), tr("The folder is currently unavailable."), Qt::ToolTipRole);
                    }
                    else if (!folder.lastSyncDate.isNull())
                    {
                        QString time(syncApp->toLocalizedDateTime(folder.lastSyncDate, dateFormat));
                        QString lastSync = QString("Last synchronization: %1.").arg(time);
                        folderModel->setData(folderModel->index(j, 0), lastSync, Qt::ToolTipRole);
                    }
                    else
                    {
                        folderModel->setData(folderModel->index(j, 0), tr("Haven't been synchronized yet."), Qt::ToolTipRole);
                    }

                    j++;
                }
            }

            return;
        }
    }
}

/*
===================
MainWindow::updateDatabaseSize
===================
*/
void MainWindow::updateDatabaseSize()
{
    int size = 0;

    if (manager.databaseLocation() == SyncManager::Decentralized)
    {
        for (auto &profile : manager.profiles())
            for (auto &folder : profile.folders)
                size += QFileInfo(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME).size();
    }
    else
    {
        QDir::Filters filters = QDir::Files | QDir::Hidden;
        QDirIterator it(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/", {"*.db"}, filters);

        while (it.hasNext())
        {
            it.next();
            size += it.fileInfo().size();
        }
    }

    // Adds file data size info to the context menu
    if (size)
    {
        if (size == 0)
            saveDatabaseAction->setText(tr("&Save Files Data  (Requires disk space)"));
        else if (size < 1024)
            saveDatabaseAction->setText(QString(tr("&Save Files Data (Requires ~%1 bytes)")).arg(size));
        else if ((size / 1024) < 1024)
            saveDatabaseAction->setText(QString(tr("&Save Files Data (Requires ~%1 KB)")).arg(size / 1024));
        else
            saveDatabaseAction->setText(QString(tr("&Save Files Data (Requires ~%1 MB)")).arg(size / 1024 / 1024));
    }
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    manager.updateStatus();

    if (isVisible())
    {
        // Profile list
        for (int i = 0, j = 0; i < manager.profiles().size(); i++)
        {
            if (manager.profiles()[i].toBeRemoved)
                continue;

            QModelIndex index = profileModel->index(j, 0);

            bool hasFolders = false;
            bool missingFolder = false;

            for (const auto &folder : manager.profiles()[i].folders)
            {
                if (folder.exists)
                    hasFolders = true;
                else
                    missingFolder = true;
            }

            if (manager.profiles()[i].paused)
            {
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            }
            else if (manager.profiles()[i].syncing || (!manager.profiles()[i].syncHidden && manager.queue().contains(&manager.profiles()[i])))
            {
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            }
            else if (!hasFolders && !manager.profiles()[i].folders.isEmpty())
            {
                profileModel->setData(index, iconRemove, Qt::DecorationRole);
            }
            else if (missingFolder)
            {
                profileModel->setData(index, iconWarning, Qt::DecorationRole);
            }
            else
            {
                profileModel->setData(index, iconDone, Qt::DecorationRole);
            }

            ui->syncProfilesView->update(index);
            j++;
        }

        // Folders
        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            int row = ui->syncProfilesView->selectionModel()->selectedRows()[0].row();

            for (int i = 0, j = 0; i < manager.profiles()[row].folders.size(); i++)
            {
                if (manager.profiles()[row].folders[j].toBeRemoved)
                    continue;

                QModelIndex index = folderModel->index(i, 0);

                if (manager.profiles()[row].folders[i].paused)
                    folderModel->setData(index, iconPause, Qt::DecorationRole);
                else if (manager.profiles()[row].folders[i].syncing || (manager.queue().contains(&manager.profiles()[row]) && !manager.isSyncing() && !manager.profiles()[row].syncHidden))
                    folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                else if (!manager.profiles()[row].folders[i].exists)
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
        if (manager.isSyncing() || (!manager.isThereHiddenProfile() && !manager.queue().empty()))
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

    syncNowAction->setEnabled(manager.queue().size() != manager.existingProfiles());

    if (!manager.filesToSync())
    {
        trayIcon->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
    else
    {
        trayIcon->setToolTip(QString(tr("Sync Manager - %1 files to synchronize")).arg(manager.filesToSync()));
        setWindowTitle(QString(tr("Sync Manager - %1 files to synchronize")).arg(manager.filesToSync()));
    }
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
MainWindow::showContextMenu
===================
*/
void MainWindow::showContextMenu(const QPoint &pos) const
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

            if (manager.profiles()[row].paused)
            {
                menu.addAction(iconResume, tr("&Resume syncing profile"), this, SLOT(pauseSelected()));
            }
            else
            {
                menu.addAction(iconPause, tr("&Pause syncing profile"), this, SLOT(pauseSelected()));
                menu.addAction(iconSync, tr("&Synchronize profile"), this, std::bind(&MainWindow::sync, const_cast<MainWindow *>(this), &const_cast<SyncProfile &>(manager.profiles()[row]), false))->setDisabled(manager.queue().contains(&manager.profiles()[row]));
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

        menu.addAction(iconAdd, tr("&Add a new folder"), this, SLOT(addFolder()));

        if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
        {
            if (manager.profiles()[ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row()].folders[ui->folderListView->selectionModel()->selectedIndexes()[0].row()].paused)
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

        databaseLocationMenu->setEnabled(false);

        QFuture<void> future = QtConcurrent::run([&]() { manager.sync(); });

        while (!future.isFinished())
            updateApp();

        for (auto &action : syncingModeMenu->actions())
            action->setEnabled(true);

        for (auto &action : deletionModeMenu->actions())
            action->setEnabled(true);

        if (manager.saveDatabaseEnabled())
            databaseLocationMenu->setEnabled(true);

        animSync.stop();

        updateDatabaseSize();
        updateStatus();
        updateSyncTime();
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
    QVariantList hList = settings.value("HorizontalSplitter", QVariantList({ui->syncProfilesLayout->minimumSize().width(), 999999})).value<QVariantList>();
    for (auto &variant : hList) hSizes.append(variant.toInt());
    ui->horizontalSplitter->setSizes(hSizes);
    ui->horizontalSplitter->setStretchFactor(0, 0);
    ui->horizontalSplitter->setStretchFactor(1, 1);

    showInTray = settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool();
    language = static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt());
    fileDataOutdated = settings.value("AppVersion").toString().compare("1.9.2") < 0;

    for (int i = 0; i < manager.profiles().size(); i++)
    {
        QString profileName = profileModel->index(i).data(Qt::DisplayRole).toString();

        // Loads saved pause states and checks if synchronization folders exist
        manager.profiles()[i].paused = settings.value(profileName + QLatin1String("_profile/") + QLatin1String("Paused"), false).toBool();

        if (!manager.profiles()[i].paused)
            manager.setPaused(false);

        // Loads last sync dates for all profiles
        manager.profiles()[i].lastSyncDate = settings.value(profileName + QLatin1String("_profile/") + QLatin1String("LastSyncDate")).toDateTime();
        manager.profiles()[i].syncTime = settings.value(manager.profiles()[i].name + QLatin1String("_profile/") + QLatin1String("SyncTime"), 0).toULongLong();

        for (auto &folder : manager.profiles()[i].folders)
        {
            folder.paused = settings.value(profileName + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), false).toBool();

            if (!folder.paused)
                manager.setPaused(false);

            folder.exists = QFileInfo::exists(folder.path);
            folder.lastSyncDate = settings.value(profileName + QLatin1String("_profile/") + folder.path + QLatin1String("_LastSyncDate")).toDateTime();
        }

        updateLastSyncTime(&manager.profiles()[i]);

        // Loads exclude list
        for (auto &exclude : settings.value(profileName + QLatin1String("_profile/") + QLatin1String("ExcludeList")).toStringList())
            manager.profiles()[i].excludeList.append(exclude.toUtf8());
    }

    saveDatabaseAction->setChecked(manager.saveDatabaseEnabled());
    databaseLocationMenu->setEnabled(manager.saveDatabaseEnabled());
    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!manager.notificationsEnabled());
    ignoreHiddenFilesAction->setChecked(manager.ignoreHiddenFilesEnabled());
    detectMovedFilesAction->setChecked(manager.detectMovedFilesEnabled());
    switchSyncingMode(static_cast<SyncManager::SyncingMode>(settings.value("SyncingMode", SyncManager::Automatic).toInt()));
    switchDeletionMode(static_cast<SyncManager::DeletionMode>(settings.value("DeletionMode", manager.MoveToTrash).toInt()));

    manager.updateNextSyncingTime();

    for (auto &profile : manager.profiles())
        if (profile.syncEvery >= std::numeric_limits<int>::max())
            increaseSyncTimeAction->setEnabled(false);
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
    settings.setValue("AppVersion", SYNCMANAGER_VERSION);
    settings.setValue("Paused", manager.isPaused());
    settings.setValue("SyncingMode", manager.syncingMode());
    settings.setValue("DeletionMode", manager.deletionMode());
    settings.setValue("SaveDatabase", manager.saveDatabaseEnabled());
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

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved)
                continue;

            settings.setValue(profile.name + QLatin1String("_profile/") + folder.path + QLatin1String("_LastSyncDate"), profile.lastSyncDate);
            settings.setValue(profile.name + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);
        }

        settings.setValue(profile.name + QLatin1String("_profile/") + QLatin1String("Paused"), profile.paused);
        settings.setValue(profile.name + QLatin1String("_profile/") + QLatin1String("LastSyncDate"), profile.lastSyncDate);
        settings.setValue(profile.name + QLatin1String("_profile/") + QLatin1String("SyncTime"), profile.syncTime);
    }
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
    saveDatabaseAction->setText(tr("&Save Files Data  (Requires disk space)"));
    saveDatabaseLocallyAction->setText(tr("&Locally (On the local machine)"));
    saveDatabaseDecentralizedAction->setText(tr("&Decentralized (Inside synchronization folders)"));

    for (int i = 0; i < languageCount(); i++)
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
    databaseMenu->setTitle(tr("&Database"));
    databaseLocationMenu->setTitle(tr("&Database Location"));
    languageMenu->setTitle(tr("&Language"));
    settingsMenu->setTitle(tr("&Settings"));

    syncNowAction->setToolTip(tr("&Sync Now"));
    pauseSyncingAction->setToolTip(tr("&Pause Syncing"));
    ui->SyncLabel->setText(tr("Synchronization profiles:"));
    ui->foldersLabel->setText(tr("Folders to synchronize:"));

    updateDatabaseSize();
    updateStatus();
    updateSyncTime();

    for (auto &profile : manager.profiles())
        updateLastSyncTime(&profile);
}
