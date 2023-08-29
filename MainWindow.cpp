/*
===============================================================================
    Copyright (C) 2022-2023 Ilya Lyakhovets

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

#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "DecoratedStringListModel.h"
#include "UnhidableMenu.h"
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
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
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);

    ui->setupUi(this);
    ui->centralWidget->setLayout(ui->mainLayout);
    setWindowTitle(QString("Sync Manager"));
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    resize(QSize(settings.value("Width", 500).toInt(), settings.value("Height", 300).toInt()));
    setWindowState(settings.value("Fullscreen", false).toBool() ? Qt::WindowMaximized : Qt::WindowActive);

    QList<int> hSizes;
    QVariantList hList = settings.value("HorizontalSplitter", QVariantList({ui->syncProfilesLayout->minimumSize().width(), 999999})).value<QVariantList>();
    for (auto &variant : hList) hSizes.append(variant.toInt());
    ui->horizontalSplitter->setSizes(hSizes);
    ui->horizontalSplitter->setStretchFactor(0, 0);
    ui->horizontalSplitter->setStretchFactor(1, 1);

    manager.paused = settings.value(QLatin1String("Paused"), false).toBool();
    showInTray = settings.value("ShowInTray", true).toBool();
    manager.notifications = QSystemTrayIcon::supportsMessages() && settings.value("Notifications", true).toBool();
    manager.rememberFiles = settings.value("RememberFiles", true).toBool();
    manager.detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    manager.syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (manager.syncTimeMultiplier <= 0) manager.syncTimeMultiplier = 1;

    manager.caseSensitiveSystem = settings.value("caseSensitiveSystem", manager.caseSensitiveSystem).toBool();
    manager.versionFolder = settings.value("VersionFolder", "[Deletions]").toString();
    manager.versionPattern = settings.value("VersionPattern", "yyyy_M_d_h_m_s_z").toString();

    profileModel = new DecoratedStringListModel;
    folderModel = new DecoratedStringListModel;
    ui->syncProfilesView->setModel(profileModel);
    ui->folderListView->setModel(folderModel);

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

    syncNowAction = new QAction(iconSync, "&Sync Now", this);
    pauseSyncingAction = new QAction(iconPause, "&Pause Syncing", this);
    automaticAction = new QAction("&Automatic", this);
    manualAction = new QAction("&Manual", this);
    increaseSyncTimeAction = new QAction("&Increase", this);
    syncingTimeAction = new QAction("Synchronize Every:", this);
    decreaseSyncTimeAction = new QAction("&Decrease", this);
    moveToTrashAction = new QAction("&Move Files to Trash", this);
    versioningAction = new QAction("&Versioning", this);
    deletePermanentlyAction = new QAction("&Delete Files Permanently", this);
    launchOnStartupAction = new QAction("&Launch on Startup", this);
    showInTrayAction = new QAction("&Show in System Tray");
    disableNotificationAction = new QAction("&Disable Notifications", this);
    enableRememberFilesAction = new QAction("&Remember Files (Requires disk space)", this);
    detectMovedFilesAction = new QAction("&Detect Renamed and Moved Files (Beta)", this);
    showAction = new QAction("&Show", this);
    quitAction = new QAction("&Quit", this);
    QAction *version = new QAction(QString("Version: %1").arg(SYNCMANAGER_VERSION), this);

    // Adds file data size info to the context menu
    if (int size = QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME).size())
    {
        if (size < 1024)
            enableRememberFilesAction->setText(QString("&Remember Files (Requires ~%1 bytes)").arg(size));
        else if ((size / 1024) < 1024)
            enableRememberFilesAction->setText(QString("&Remember Files (Requires ~%1 KB)").arg(size / 1024));
        else
            enableRememberFilesAction->setText(QString("&Remember Files (Requires ~%1 MB)").arg(size / 1024 / 1024));
    }

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(manager.syncTimeMultiplier <= 1);
    version->setDisabled(true);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);
    deletePermanentlyAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    versioningAction->setCheckable(true);
    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    enableRememberFilesAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);

    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!manager.notifications);
    enableRememberFilesAction->setChecked(manager.rememberFiles);
    detectMovedFilesAction->setChecked(manager.detectMovedFiles);

#ifdef Q_OS_WIN
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk"));
#else
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop"));
#endif

    syncingModeMenu = new UnhidableMenu("&Syncing Mode", this);
    syncingModeMenu->addAction(automaticAction);
    syncingModeMenu->addAction(manualAction);

    syncingTimeMenu = new UnhidableMenu("&Syncing Time", this);
    syncingTimeMenu->addAction(increaseSyncTimeAction);
    syncingTimeMenu->addAction(syncingTimeAction);
    syncingTimeMenu->addAction(decreaseSyncTimeAction);

    deletionModeMenu = new UnhidableMenu("&Deletion Mode", this);
    deletionModeMenu->addAction(moveToTrashAction);
    deletionModeMenu->addAction(versioningAction);
    deletionModeMenu->addAction(deletePermanentlyAction);

    settingsMenu = new UnhidableMenu("&Settings", this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(syncingModeMenu);
    settingsMenu->addMenu(syncingTimeMenu);
    settingsMenu->addMenu(deletionModeMenu);
    settingsMenu->addAction(launchOnStartupAction);
    settingsMenu->addAction(showInTrayAction);
    settingsMenu->addAction(disableNotificationAction);
    settingsMenu->addAction(enableRememberFilesAction);
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
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip("Sync Manager");
    trayIcon->setIcon(trayIconDone);

    this->menuBar()->addAction(syncNowAction);
    this->menuBar()->addAction(pauseSyncingAction);
    this->menuBar()->addMenu(settingsMenu);

    connect(ui->syncProfilesView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(profileClicked(QItemSelection,QItemSelection)));
    connect(ui->syncProfilesView->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)), SLOT(profileNameChanged(QModelIndex)));
    connect(ui->syncProfilesView, SIGNAL(deletePressed()), SLOT(removeProfile()));
    connect(ui->folderListView, &FolderListView::drop, this, &MainWindow::addFolder);
    connect(ui->folderListView, SIGNAL(deletePressed()), SLOT(removeFolder()));
    connect(syncNowAction, &QAction::triggered, this, [this](){ manager.syncHidden = false; doSync(); });
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(automaticAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, SyncManager::Automatic));
    connect(manualAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, SyncManager::Manual));
    connect(increaseSyncTimeAction, &QAction::triggered, this, &MainWindow::increaseSyncTime);
    connect(decreaseSyncTimeAction, &QAction::triggered, this, &MainWindow::decreaseSyncTime);
    connect(moveToTrashAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.MoveToTrash));
    connect(versioningAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.Versioning));
    connect(deletePermanentlyAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, manager.DeletePermanently));
    connect(launchOnStartupAction, &QAction::triggered, this, [this](){ setLaunchOnStartup(launchOnStartupAction->isChecked()); });
    connect(showInTrayAction, &QAction::triggered, this, [this](){ setTrayVisible(!showInTray); });
    connect(disableNotificationAction, &QAction::triggered, this, [this](){ manager.notifications = !manager.notifications; });
    connect(enableRememberFilesAction, &QAction::triggered, this, [this](){ manager.rememberFiles = !manager.rememberFiles; });
    connect(detectMovedFilesAction, &QAction::triggered, this, [this](){ manager.detectMovedFiles = !manager.detectMovedFiles; });
    connect(showAction, &QAction::triggered, this, std::bind(&MainWindow::trayIconActivated, this, QSystemTrayIcon::DoubleClick));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(&manager.syncTimer, &QTimer::timeout, this, [this](){ if (manager.queue.isEmpty()) { manager.syncHidden = true; doSync(); }});
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(&manager, &SyncManager::warning, this, [this](QString title, QString message){ trayIcon->showMessage(title, message, QSystemTrayIcon::Critical, TRAY_MESSAGE_TIME); });
    connect(&manager, &SyncManager::profileSynced, this, &MainWindow::updateLastSyncTime);

    // Loads synchronization profiles
    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    QStringList profileNames = profilesData.allKeys();
    profileNames.sort();
    profileModel->setStringList(profileNames);

    for (auto &name : profileNames)
    {
        manager.profiles.append(SyncProfile(manager.paused));
        manager.profiles.last().name = name;

        QStringList paths = profilesData.value(name).toStringList();
        paths.sort();

        for (const auto &path : paths)
        {
            manager.profiles.last().folders.append(SyncFolder(manager.paused));
            manager.profiles.last().folders.last().path = path.toUtf8();
        }
    }

    for (int i = 0; i < manager.profiles.size(); i++)
    {
        // Loads saved pause states and checks if synchronization folders exist
        manager.profiles[i].paused = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), false).toBool();
        if (!manager.profiles[i].paused) manager.paused = false;

        for (auto &folder : manager.profiles[i].folders)
        {
            folder.paused = settings.value(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), false).toBool();
            if (!folder.paused) manager.paused = false;
            folder.exists = QFileInfo::exists(folder.path);

            if (manager.notifications && !folder.exists)
                trayIcon->showMessage("Couldn't find folder", folder.path, QSystemTrayIcon::Warning, 1000);
        }

        // Loads last sync dates for all profiles
        manager.profiles[i].lastSyncDate = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("LastSyncDate")).toDateTime();
        updateLastSyncTime(&manager.profiles[i]);
    }

    if (manager.rememberFiles && settings.value("AppVersion").toString().compare("1.5") >= 0)
        manager.restoreData();
    else
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);

    manager.syncTimer.setSingleShot(true);
    updateTimer.setSingleShot(true);

    switchSyncingMode(static_cast<SyncManager::SyncingMode>(settings.value("SyncingMode", SyncManager::Automatic).toInt()));
    switchDeletionMode(static_cast<SyncManager::DeletionMode>(settings.value("DeletionMode", manager.MoveToTrash).toInt()));
    updateStatus();

    if (manager.syncingMode == SyncManager::Automatic) manager.syncTimer.start(0);
}

/*
===================
MainWindow::~MainWindow
===================
*/
MainWindow::~MainWindow()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QVariantList hSizes;

    for (auto &size : ui->horizontalSplitter->sizes()) hSizes.append(size);

    if (!isMaximized())
    {
        settings.setValue("Width", size().width());
        settings.setValue("Height", size().height());
    }

    // Saves profiles/folders pause states and last sync dates
    for (auto &profile : manager.profiles)
    {
        if (!profile.toBeRemoved) settings.setValue(profile.name + QLatin1String("_profile/") + QLatin1String("Paused"), profile.paused);

        for (auto &folder : profile.folders)
            if (!folder.toBeRemoved)
                settings.setValue(profile.name + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

        settings.setValue(profile.name + QLatin1String("_profile/") + QLatin1String("LastSyncDate"), profile.lastSyncDate);
    }

    settings.setValue("Paused", manager.paused);
    settings.setValue("Fullscreen", isMaximized());
    settings.setValue("HorizontalSplitter", hSizes);
    settings.setValue("SyncingMode", manager.syncingMode);
    settings.setValue("DeletionMode", manager.deletionMode);
    settings.setValue("ShowInTray", showInTray);
    settings.setValue("Notifications", manager.notifications);
    settings.setValue("RememberFiles", manager.rememberFiles);
    settings.setValue("DetectMovedFiles", manager.detectMovedFiles);
    settings.setValue("SyncTimeMultiplier", manager.syncTimeMultiplier);
    settings.setValue("caseSensitiveSystem", manager.caseSensitiveSystem);
    settings.setValue("VersionFolder", manager.versionFolder);
    settings.setValue("VersionPattern", manager.versionPattern);
    settings.setValue("AppVersion", SYNCMANAGER_VERSION);

    if (manager.rememberFiles) manager.saveData();

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
        qApp->setQuitOnLastWindowClosed(false);
    }
    else
    {
        QMainWindow::show();
        trayIcon->hide();
        qApp->setQuitOnLastWindowClosed(true);
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
}

/*
===================
MainWindow::setLaunchOnStartup
===================
*/
void MainWindow::setLaunchOnStartup(bool enable)
{
    QString path;

    launchOnStartupAction->setChecked(enable);

#ifdef Q_OS_WIN
    path = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk";
#elif defined(Q_OS_LINUX)
    path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop";
#endif

    if (enable)
    {
#ifdef Q_OS_WIN
        QFile::link(QCoreApplication::applicationFilePath(), path);
#elif defined(Q_OS_LINUX)
        QFile::remove(path);
        QFile shortcut(path);
        if (shortcut.open(QIODevice::WriteOnly))
        {
            QTextStream stream(&shortcut);
            stream << "[Desktop Entry]\n";
            stream << "Type=Application\n";
            stream << "Exec=" + QCoreApplication::applicationDirPath() + "/SyncManager\n";
            stream << "Hidden=false\n";
            stream << "NoDisplay=false\n";
            stream << "Terminal=false\n";
            stream << "Name=Sync Manager\n";
            stream << "Icon=" + QCoreApplication::applicationDirPath() + "/SyncManager.png\n";
        }

        // Somehow doesn't work on Linux
        //QFile::link(QCoreApplication::applicationFilePath(), path);
#endif
    }
    else
    {
        QFile::remove(path);
    }
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
        auto buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No);

        if (manager.busy && QMessageBox::warning(nullptr, QString("Quit"), QString("Currently syncing. Are you sure you want to quit?"), buttons, QMessageBox::No) == QMessageBox::No)
        {
            event->ignore();
            return;
        }

        manager.shouldQuit = true;
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
    QString newName("New profile");
    QStringList profileNames;

    for (auto &profile : manager.profiles)
        profileNames.append(profile.name);

    for (int i = 2; profileNames.contains(newName); i++)
        newName = QString("New profile (%1)").arg(i);

    manager.profiles.append(SyncProfile(manager.paused));
    manager.profiles.last().name = newName;
    profileNames.append(newName);
    profileModel->setStringList(profileNames);
    folderModel->setStringList(QStringList());

    QSettings profileData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileData.setValue(newName, folderModel->stringList());

    // Avoids reloading a newly added profile as it's already loaded.
    ui->syncProfilesView->selectionModel()->blockSignals(true);
    ui->syncProfilesView->setCurrentIndex(ui->syncProfilesView->model()->index(ui->syncProfilesView->model()->rowCount() - 1, 0));
    ui->syncProfilesView->selectionModel()->blockSignals(false);

    ui->folderListView->selectionModel()->reset();
    ui->folderListView->update();
    updateLastSyncTime(&manager.profiles.last());
    updateStatus();
}

/*
===================
MainWindow::removeProfile
===================
*/
void MainWindow::removeProfile()
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty()) return;

    auto buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No);

    if (QMessageBox::question(nullptr, QString("Remove profile"), QString("Are you sure you want to remove profile?"), buttons, QMessageBox::Yes) == QMessageBox::No)
        return;

    for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
    {
        ui->syncProfilesView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(manager.profiles[index.row()].name + QLatin1String("_profile"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.remove(manager.profiles[index.row()].name);

        manager.profiles[index.row()].paused = true;
        manager.profiles[index.row()].toBeRemoved = true;

        for (auto &folder : manager.profiles[index.row()].folders)
        {
            folder.paused = true;
            folder.toBeRemoved = true;
        }

        if (!manager.busy) manager.profiles.remove(index.row());
        folderModel->setStringList(QStringList());
    }

    ui->syncProfilesView->selectionModel()->reset();
    updateStatus();
    manager.updateNextSyncingTime();
    manager.updateTimer();
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

    for (auto &folder : manager.profiles[ui->syncProfilesView->currentIndex().row()].folders)
        folderPaths.append(folder.path);

    folderModel->setStringList(folderPaths);
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

    for (auto &profile : manager.profiles)
        profileNames.append(profile.name);

    for (auto &folder : manager.profiles[row].folders)
        folderPaths.append(folder.path);

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(manager.profiles[row].name, Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(index, manager.profiles[row].name, Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(manager.profiles[row].name + QLatin1String("_profile"));
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("Paused"), manager.profiles[row].paused);
    for (auto &folder : manager.profiles[row].folders) settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profilesData.remove(manager.profiles[row].name);
    profilesData.setValue(newName, folderPaths);

    manager.profiles[row].name = newName;
}

/*
===================
MainWindow::addFolder
===================
*/
void MainWindow::addFolder(const QMimeData *mimeData)
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty()) return;

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
        QString folderPath = QFileDialog::getExistingDirectory(this, "Browse For Folder", QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);
        if (folderPath.isEmpty()) return;
        folders.append(folderPath);
    }

    QStringList folderPaths;

    for (auto &folder : manager.profiles[row].folders)
        if (!folder.toBeRemoved)
            folderPaths.append(folder.path);

    // Checks if we already have a folder for synchronization in the list
    for (const auto &folder : folders)
    {
        bool exists = false;

        for (const auto &path : folderPaths)
        {
            int n = path.size() > folder.size() ? path.size() - 1 : folder.size() - 1;

            if ((manager.caseSensitiveSystem && path.toStdString().compare(0, n, folder.toStdString()) == 0) ||
                (!manager.caseSensitiveSystem && path.toLower().toStdString().compare(0, n, folder.toLower().toStdString()) == 0))
            {
                exists = true;
            }
        }

        if (!exists)
        {
            manager.profiles[row].folders.append(SyncFolder(manager.paused));
            manager.profiles[row].folders.last().path = folder.toUtf8();
            manager.profiles[row].folders.last().path.append("/");
            folderPaths.append(manager.profiles[row].folders.last().path);

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(manager.profiles[row].name, folderPaths);

            folderModel->setStringList(folderPaths);
            updateStatus();
        }
    }
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

        manager.profiles[profileRow].folders[index.row()].paused = true;
        manager.profiles[profileRow].folders[index.row()].toBeRemoved = true;
        ui->folderListView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(manager.profiles[profileRow].name + QLatin1String("_profile/") + manager.profiles[profileRow].folders[index.row()].path + QLatin1String("_Paused"));

        if (!manager.busy) manager.profiles[profileRow].folders.remove(index.row());

        QStringList foldersPaths;

        for (auto &folder : manager.profiles[profileRow].folders)
            foldersPaths.append(folder.path);

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(manager.profiles[profileRow].name, foldersPaths);
    }

    ui->folderListView->selectionModel()->reset();
    updateStatus();
    manager.updateNextSyncingTime();
    manager.updateTimer();
}

/*
===================
MainWindow::pauseSyncing
===================
*/
void MainWindow::pauseSyncing()
{
    manager.paused = !manager.paused;

    for (auto &profile : manager.profiles)
    {
        profile.paused = manager.paused;

        for (auto &folder : profile.folders)
            folder.paused = manager.paused;
    }

    updateStatus();
    manager.updateNextSyncingTime();
    manager.updateTimer();
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
                manager.profiles[profileRow].folders[index.row()].paused = !manager.profiles[profileRow].folders[index.row()].paused;

            manager.profiles[profileRow].paused = true;

            for (const auto &folder : manager.profiles[profileRow].folders)
                if (!folder.paused)
                    manager.profiles[profileRow].paused = false;
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
            {
                manager.profiles[index.row()].paused = !manager.profiles[index.row()].paused;

                for (auto &folder : manager.profiles[index.row()].folders)
                    folder.paused = manager.profiles[index.row()].paused;
            }
        }

        updateStatus();
        manager.updateNextSyncingTime();
        manager.updateTimer();
    }
}

/*
===================
MainWindow::quit
===================
*/
void MainWindow::quit()
{
    auto buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No);

    if ((!manager.busy && QMessageBox::question(nullptr, QString("Quit"), QString("Are you sure you want to quit?"), buttons, QMessageBox::No) == QMessageBox::Yes) ||
        (manager.busy && QMessageBox::warning(nullptr, QString("Quit"), QString("Currently syncing. Are you sure you want to quit?"), buttons, QMessageBox::No) == QMessageBox::Yes))
    {
        manager.shouldQuit = true;
        qApp->quit();
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
    manager.syncingMode = mode;

    automaticAction->setChecked(false);
    manualAction->setChecked(false);

    if (mode == SyncManager::Manual)
    {
        pauseSyncingAction->setVisible(false);
        manualAction->setChecked(true);
        syncingTimeMenu->menuAction()->setVisible(false);
        manager.syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        pauseSyncingAction->setVisible(true);
        automaticAction->setChecked(true);
        syncingTimeMenu->menuAction()->setVisible(true);
        manager.updateNextSyncingTime();
        manager.updateTimer();
    }

    updateStatus();
}

/*
===================
MainWindow::increaseSyncTime
===================
*/
void MainWindow::increaseSyncTime()
{
    if (manager.syncEvery != std::numeric_limits<int>::max())
    {
        manager.syncTimeMultiplier++;
        decreaseSyncTimeAction->setDisabled(false);
        manager.updateNextSyncingTime();
        manager.updateTimer();
        updateSyncTime();

        if (manager.syncEvery == std::numeric_limits<int>::max())
            increaseSyncTimeAction->setDisabled(true);
    }
}

/*
===================
MainWindow::decreaseSyncTime
===================
*/
void MainWindow::decreaseSyncTime()
{
    manager.syncTimeMultiplier--;
    manager.updateNextSyncingTime();
    manager.updateTimer();
    updateSyncTime();

    if (manager.syncEvery != std::numeric_limits<int>::max())
        increaseSyncTimeAction->setDisabled(false);

    if (manager.syncTimeMultiplier <= 1)
        decreaseSyncTimeAction->setDisabled(true);
}

/*
===================
MainWindow::updateSyncTime
===================
*/
void MainWindow::updateSyncTime()
{
    int seconds = (manager.syncEvery / 1000) % 60;
    int minutes = (manager.syncEvery / 1000 / 60) % 60;
    int hours = (manager.syncEvery / 1000 / 60 / 60) % 24;
    int days = (manager.syncEvery / 1000 / 60 / 60 / 24);

    QString str("Synchronize Every: ");

    if (days)
        str.append(QString("%1 days").arg(QString::number(static_cast<float>(days) + static_cast<float>(hours) / 24.0f, 'f', 1)));
    else if (hours)
        str.append(QString("%1 hours").arg(QString::number(static_cast<float>(hours) + static_cast<float>(minutes) / 60.0f, 'f', 1)));
    else if (minutes)
        str.append(QString("%1 minutes").arg(QString::number(static_cast<float>(minutes) + static_cast<float>(seconds) / 60.0f, 'f', 1)));
    else if (seconds)
        str.append(QString("%1 seconds").arg(seconds));

    syncingTimeAction->setText(str);
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
            if (!manager.profiles[i].lastSyncDate.isNull())
            {
                QString lastSync = QString("Last synchronization: %1.").arg(profile->lastSyncDate.toString());
                profileModel->setData(profileModel->index(i, 0), lastSync, Qt::ToolTipRole);
            }
            else
            {
                profileModel->setData(profileModel->index(i, 0), "Haven't been synchronized yet.", Qt::ToolTipRole);
            }

            return;
        }
    }
}

/*
===================
MainWindow::switchDeletionMode
===================
*/
void MainWindow::switchDeletionMode(SyncManager::DeletionMode mode)
{
    manager.deletionMode = mode;

    moveToTrashAction->setChecked(false);
    versioningAction->setChecked(false);
    deletePermanentlyAction->setChecked(false);

    if (mode == SyncManager::MoveToTrash)
        moveToTrashAction->setChecked(true);
    else if (mode == SyncManager::Versioning)
        versioningAction->setChecked(true);
    else
        deletePermanentlyAction->setChecked(true);
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    manager.syncing = false;
    manager.numOfFilesToSync = 0;

    manager.updateStatus();

    if (isVisible())
    {
        // Profile list
        for (int i = 0, j = 0; i < manager.profiles.size(); i++)
        {
            if (manager.profiles[i].toBeRemoved) continue;

            QModelIndex index = profileModel->index(j, 0);

            bool hasFolders = false;
            bool missingFolder = false;

            for (const auto &folder : manager.profiles[i].folders)
            {
                if (folder.exists)
                    hasFolders = true;
                else
                    missingFolder = true;
            }

            if (manager.profiles[i].paused)
            {
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            }
            else if (manager.profiles[i].syncing || (!manager.syncHidden && manager.queue.contains(i)))
            {
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            }
            else if (!hasFolders && !manager.profiles[i].folders.isEmpty())
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

            for (int i = 0, j = 0; i < manager.profiles[row].folders.size(); i++)
            {
                if (manager.profiles[row].folders[j].toBeRemoved) continue;

                QModelIndex index = folderModel->index(i, 0);

                if (manager.profiles[row].folders[i].paused)
                    folderModel->setData(index, iconPause, Qt::DecorationRole);
                else if (manager.profiles[row].folders[i].syncing || (manager.queue.contains(row) && !manager.syncing && !manager.syncHidden))
                    folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                else if (!manager.profiles[row].folders[i].exists)
                    folderModel->setData(index, iconRemove, Qt::DecorationRole);
                else
                    folderModel->setData(index, iconDone, Qt::DecorationRole);

                ui->folderListView->update(index);
                j++;
            }
        }
    }

    // Pause status
    for (const auto &profile : manager.profiles)
    {
        manager.paused = profile.paused;
        if (!manager.paused) break;
    }

    // Tray & Icon
    if (manager.syncingMode == SyncManager::Automatic && manager.paused)
    {
        trayIcon->setIcon(trayIconPause);
        setWindowIcon(trayIconPause);

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconResume.cacheKey())
            pauseSyncingAction->setIcon(iconResume);

        pauseSyncingAction->setText("&Resume Syncing");
    }
    else
    {
        if (manager.syncing || (!manager.syncHidden && !manager.queue.empty()))
        {
            if (trayIcon->icon().cacheKey() != trayIconSync.cacheKey())
                trayIcon->setIcon(trayIconSync);

            if (windowIcon().cacheKey() != trayIconSync.cacheKey())
                setWindowIcon(trayIconSync);
        }
        else if (manager.isThereWarning)
        {
            if (manager.isThereIssue)
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

        pauseSyncingAction->setText("&Pause Syncing");
    }

    syncNowAction->setEnabled(manager.queue.size() != manager.existingProfiles);

    // Number of files left to sync
    if (manager.busy)
    {
        for (auto &folder : manager.profiles[manager.queue.head()].folders)
        {
            if (folder.exists && !folder.paused)
            {
                manager.numOfFilesToSync += folder.foldersToRename.size();
                manager.numOfFilesToSync += folder.filesToMove.size();
                manager.numOfFilesToSync += folder.foldersToAdd.size();
                manager.numOfFilesToSync += folder.filesToAdd.size();
                manager.numOfFilesToSync += folder.foldersToRemove.size();
                manager.numOfFilesToSync += folder.filesToRemove.size();
            }
        }
    }

    if (!manager.numOfFilesToSync)
    {
        trayIcon->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
    else
    {
        trayIcon->setToolTip(QString("Sync Manager - %1 files to synchronize").arg(manager.numOfFilesToSync));
        setWindowTitle(QString("Sync Manager - %1 files to synchronize").arg(manager.numOfFilesToSync));
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
    return manager.shouldQuit;
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
        menu.addAction(iconAdd, "&Add a new profile", this, SLOT(addProfile()));

        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            int row = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();

            if (manager.profiles[row].paused)
            {
                menu.addAction(iconResume, "&Resume syncing profile", this, SLOT(pauseSelected()));
            }
            else
            {
                menu.addAction(iconPause, "&Pause syncing profile", this, SLOT(pauseSelected()));
                menu.addAction(iconSync, "&Synchronize profile", this, std::bind(&MainWindow::doSync, const_cast<MainWindow *>(this), row))->setDisabled(manager.queue.contains(row));
            }

            menu.addAction(iconRemove, "&Remove profile", this, SLOT(removeProfile()));
        }

        menu.popup(ui->syncProfilesView->mapToGlobal(pos));
    }
    // Folders
    else if (ui->folderListView->hasFocus())
    {
        if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty()) return;

        menu.addAction(iconAdd, "&Add a new folder", this, SLOT(addFolder()));

        if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
        {
            if (manager.profiles[ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row()].folders[ui->folderListView->selectionModel()->selectedIndexes()[0].row()].paused)
                menu.addAction(iconResume, "&Resume syncing folder", this, SLOT(pauseSelected()));
            else
                menu.addAction(iconPause, "&Pause syncing folder", this, SLOT(pauseSelected()));

            menu.addAction(iconRemove, "&Remove folder", this, SLOT(removeFolder()));
        }

        menu.popup(ui->folderListView->mapToGlobal(pos));
    }
}

/*
===================
MainWindow::doSync
===================
*/
void MainWindow::doSync(int profileNumber)
{
    manager.addToQueue(profileNumber);

    if (!manager.busy)
    {
        animSync.start();
        for (auto &action : syncingModeMenu->actions()) action->setEnabled(false);
        for (auto &action : deletionModeMenu->actions()) action->setEnabled(false);

        QFuture<void> future = QtConcurrent::run([&]() { manager.sync(); });
        while (!future.isFinished()) updateApp();

        for (auto &action : syncingModeMenu->actions()) action->setEnabled(true);
        for (auto &action : deletionModeMenu->actions()) action->setEnabled(true);
        animSync.stop();

        updateStatus();
        updateSyncTime();
        manager.updateTimer();
    }
}
