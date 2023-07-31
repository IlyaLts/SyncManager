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
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMenuBar>
#include <QDirIterator>
#include <QTimer>
#include <QStack>
#include <QtConcurrent/QtConcurrent>
#include "UnhidableMenu.h"

#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#endif

#ifdef DEBUG
#include <chrono>

#define SET_TIME(t) debugSetTime(t);
#define TIMESTAMP(t, ...) debugTimestamp(t, __VA_ARGS__);

std::chrono::high_resolution_clock::time_point startTime;

/*
===================
debugSetTime
===================
*/
void debugSetTime(std::chrono::high_resolution_clock::time_point &startTime)
{
    startTime = std::chrono::high_resolution_clock::now();
}

/*
===================
debugTimestamp
===================
*/
void debugTimestamp(const std::chrono::high_resolution_clock::time_point &startTime, const char *message, ...)
{
    char buffer[256];

    va_list ap;
    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    std::chrono::high_resolution_clock::time_point time(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
    qDebug("%lld ms - %s", ml.count(), buffer);
}
#else

#define SET_TIME(t)
#define TIMESTAMP(t, m, ...)

#endif // DEBUG

/*
===================
hash64
===================
*/
hash64_t hash64(const QByteArray &str)
{
    QByteArray hash = QCryptographicHash::hash(str, QCryptographicHash::Md5);
    QDataStream stream(hash);
    quint64 a, b;
    stream >> a >> b;
    return a ^ b;
}

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

    paused = settings.value(QLatin1String("Paused"), false).toBool();
    showInTray = settings.value("ShowInTray", true).toBool();
    notifications = QSystemTrayIcon::supportsMessages() && settings.value("Notifications", true).toBool();
    rememberFiles = settings.value("RememberFiles", true).toBool();
    detectMovedFiles = settings.value("DetectMovedFiles", false).toBool();
    syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (syncTimeMultiplier <= 0) syncTimeMultiplier = 1;

    caseSensitiveSystem = settings.value("caseSensitiveSystem", caseSensitiveSystem).toBool();
    versionFolder = settings.value("VersionFolder", "!Versions").toString();
    versionPattern = settings.value("VersionPattern", "yyyy_M_d_h_m_s_z").toString();

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
    syncingTimeAction = new QAction(this);
    decreaseSyncTimeAction = new QAction("&Decrease", this);
    moveToTrashAction = new QAction("&Move Files and Folders to Trash", this);
    moveToVersionFolderAction = new QAction("&Move Files to Version Folder", this);
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
    decreaseSyncTimeAction->setDisabled(syncTimeMultiplier <= 1);
    version->setDisabled(true);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);
    deletePermanentlyAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    moveToVersionFolderAction->setCheckable(true);
    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    enableRememberFilesAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);

    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!notifications);
    enableRememberFilesAction->setChecked(rememberFiles);
    detectMovedFilesAction->setChecked(detectMovedFiles);

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
    deletionModeMenu->addAction(moveToVersionFolderAction);
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
    connect(syncNowAction, &QAction::triggered, this, [this](){ syncHidden = false; sync(); });
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(automaticAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Automatic));
    connect(manualAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Manual));
    connect(increaseSyncTimeAction, &QAction::triggered, this, &MainWindow::increaseSyncTime);
    connect(decreaseSyncTimeAction, &QAction::triggered, this, &MainWindow::decreaseSyncTime);
    connect(moveToTrashAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, MoveToTrash));
    connect(moveToVersionFolderAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, MoveToVersionFolder));
    connect(deletePermanentlyAction, &QAction::triggered, this, std::bind(&MainWindow::switchDeletionMode, this, DeletePermanently));
    connect(launchOnStartupAction, &QAction::triggered, this, [this](){ setLaunchOnStartup(launchOnStartupAction->isChecked()); });
    connect(showInTrayAction, &QAction::triggered, this, [this](){ setTrayVisible(!showInTray); });
    connect(disableNotificationAction, &QAction::triggered, this, [this](){ notifications = !notifications; });
    connect(enableRememberFilesAction, &QAction::triggered, this, [this](){ rememberFiles = !rememberFiles; });
    connect(detectMovedFilesAction, &QAction::triggered, this, [this](){ detectMovedFiles = !detectMovedFiles; });
    connect(showAction, &QAction::triggered, this, std::bind(&MainWindow::trayIconActivated, this, QSystemTrayIcon::DoubleClick));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(&syncTimer, &QTimer::timeout, this, [this](){ if (queue.isEmpty()) { syncHidden = true; sync(); }});
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));

    // Loads synchronization profiles
    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileNames = profilesData.allKeys();
    profileModel->setStringList(profileNames);

    for (auto &name : profileNames)
    {
        profiles.append(SyncProfile(paused));
        folderPaths.append(profilesData.value(name).toStringList());

        for (const auto &path : folderPaths.last())
        {
            profiles.last().folders.append(SyncFolder(paused));
            profiles.last().folders.last().path = path.toUtf8();
            profiles.last().folders.last().path.append("/");
        }
    }

    for (int i = 0; i < profiles.size(); i++)
    {
        // Loads saved pause states and checks if synchronization folders exist
        profiles[i].paused = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), false).toBool();
        if (!profiles[i].paused) paused = false;

        for (auto &folder : profiles[i].folders)
        {
            folder.paused = settings.value(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), false).toBool();
            if (!folder.paused) paused = false;
            folder.exists = QFileInfo::exists(folder.path);

            if (notifications && !folder.exists)
                trayIcon->showMessage("Couldn't find folder", folder.path, QSystemTrayIcon::Warning, 1000);
        }

        // Loads last sync dates for all profiles
        profiles[i].lastSyncDate = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("LastSyncDate")).toDateTime();

        if (!profiles[i].lastSyncDate.isNull())
        {
            QString lastSync = QString("Last Synchronization: %1.").arg(profiles[i].lastSyncDate.toString());
            profileModel->setData(profileModel->index(i, 0), lastSync, Qt::ToolTipRole);
        }
        else
        {
            profileModel->setData(profileModel->index(i, 0), "Haven't been synchronized yet.", Qt::ToolTipRole);
        }
    }

    if (rememberFiles)
        restoreData();
    else
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);

    syncTimer.setSingleShot(true);
    updateTimer.setSingleShot(true);

    switchSyncingMode(static_cast<SyncingMode>(settings.value("SyncingMode", Automatic).toInt()));
    switchDeletionMode(static_cast<DeletionMode>(settings.value("DeletionMode", MoveToTrash).toInt()));
    updateStatus();

    if (syncingMode == Automatic) syncTimer.start(0);
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
    for (int i = 0; i < profiles.size(); i++)
    {
        if (!profiles[i].toBeRemoved) settings.setValue(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), profiles[i].paused);

        for (auto &folder : profiles[i].folders)
            if (!folder.toBeRemoved)
                settings.setValue(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

        settings.setValue(profileNames[i] + QLatin1String("_profile/") + QLatin1String("LastSyncDate"), profiles[i].lastSyncDate);
    }

    settings.setValue("Paused", paused);
    settings.setValue("Fullscreen", isMaximized());
    settings.setValue("HorizontalSplitter", hSizes);
    settings.setValue("SyncingMode", syncingMode);
    settings.setValue("DeletionMode", deletionMode);
    settings.setValue("ShowInTray", showInTray);
    settings.setValue("Notifications", notifications);
    settings.setValue("RememberFiles", rememberFiles);
    settings.setValue("DetectMovedFiles", detectMovedFiles);
    settings.setValue("SyncTimeMultiplier", syncTimeMultiplier);
    settings.setValue("caseSensitiveSystem", caseSensitiveSystem);
    settings.setValue("VersionFolder", versionFolder);
    settings.setValue("VersionPattern", versionPattern);

    if (rememberFiles) saveData();

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
    }
    else
    {
        QMainWindow::show();
        trayIcon->hide();
    }
}

/*
===================
MainWindow::setTrayVisible
===================
*/
void MainWindow::setTrayVisible(bool visible)
{
    showInTray = visible;
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
        if (busy && QMessageBox::warning(nullptr, QString("Quit"), QString("Currently syncing. Are you sure you want to quit?"), QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No), QMessageBox::No) == QMessageBox::No)
        {
            event->ignore();
            return;
        }

        shouldQuit = true;
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

    for (int i = 2; profileNames.contains(newName); i++)
        newName = QString("New profile (%1)").arg(i);

    profiles.append(SyncProfile(paused));
    profileNames.append(newName);
    folderPaths.append(QStringList());
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

    if (QMessageBox::question(nullptr, QString("Remove profile"), QString("Are you sure you want to remove profile?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::No)
        return;

    for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
    {
        ui->syncProfilesView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profileNames[index.row()] + QLatin1String("_profile"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.remove(profileNames[index.row()]);

        profiles[index.row()].paused = true;
        profiles[index.row()].toBeRemoved = true;

        for (auto &folder : profiles[index.row()].folders)
        {
            folder.paused = true;
            folder.toBeRemoved = true;
        }

        if (!busy) profiles.remove(index.row());
        profileNames.removeAt(index.row());
        folderPaths.removeAt(index.row());
        folderModel->setStringList(QStringList());
    }

    ui->syncProfilesView->selectionModel()->reset();
    updateStatus();
    updateNextSyncingTime();
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

    folderModel->setStringList(folderPaths[ui->syncProfilesView->currentIndex().row()]);
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

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(profileNames[row], Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(index, profileNames[row], Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(profileNames[row] + QLatin1String("_profile"));
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("Paused"), profiles[row].paused);
    for (auto &folder : profiles[row].folders) settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profilesData.remove(profileNames[row]);
    profilesData.setValue(newName, folderPaths[row]);

    profileNames[row] = newName;
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

    // Checks if we already have a folder for synchronization in the list
    for (const auto &folder : folders)
    {
        bool exists = false;

        for (const auto &path : folderPaths[row])
        {
            if ((caseSensitiveSystem && folder.toStdString().compare(0, std::string::npos, path.toStdString()) == 0) ||
                (!caseSensitiveSystem && folder.toLower().toStdString().compare(0, std::string::npos, path.toLower().toStdString()) == 0))
            {
                exists = true;
            }
        }

        if (!exists)
        {
            folderPaths[row].append(folder);
            profiles[row].folders.append(SyncFolder(paused));
            profiles[row].folders.last().path = folder.toUtf8();
            profiles[row].folders.last().path.append("/");

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(profileNames[row], folderPaths[row]);

            folderModel->setStringList(folderPaths[row]);
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

        profiles[profileRow].folders[index.row()].paused = true;
        profiles[profileRow].folders[index.row()].toBeRemoved = true;
        if (!busy) profiles[profileRow].folders.remove(index.row());
        folderPaths[profileRow].removeAt(index.row());
        ui->folderListView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profileNames[profileRow] + QLatin1String("_profile/") + profiles[profileRow].folders[index.row()].path + QLatin1String("_Paused"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profileNames[profileRow], folderPaths[profileRow]);
    }

    ui->folderListView->selectionModel()->reset();
    updateStatus();
    updateNextSyncingTime();
}

/*
===================
MainWindow::pauseSyncing
===================
*/
void MainWindow::pauseSyncing()
{
    paused = !paused;

    for (auto &profile : profiles)
    {
        profile.paused = paused;

        for (auto &folder : profile.folders)
            folder.paused = paused;
    }

    updateStatus();
    updateNextSyncingTime();
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
                profiles[profileRow].folders[index.row()].paused = !profiles[profileRow].folders[index.row()].paused;

            profiles[profileRow].paused = true;

            for (const auto &folder : profiles[profileRow].folders)
                if (!folder.paused)
                    profiles[profileRow].paused = false;
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
            {
                profiles[index.row()].paused = !profiles[index.row()].paused;

                for (auto &folder : profiles[index.row()].folders)
                    folder.paused = profiles[index.row()].paused;
            }
        }

        updateStatus();
        updateNextSyncingTime();
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

    if ((!busy && QMessageBox::question(nullptr, QString("Quit"), QString("Are you sure you want to quit?"), buttons, QMessageBox::No) == QMessageBox::Yes) ||
        (busy && QMessageBox::warning(nullptr, QString("Quit"), QString("Currently syncing. Are you sure you want to quit?"), buttons, QMessageBox::No) == QMessageBox::Yes))
    {
        shouldQuit = true;
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
void MainWindow::switchSyncingMode(SyncingMode mode)
{
    syncingMode = mode;

    automaticAction->setChecked(false);
    manualAction->setChecked(false);

    if (mode == Manual)
    {
        pauseSyncingAction->setVisible(false);
        manualAction->setChecked(true);
        syncingTimeMenu->menuAction()->setVisible(false);
        syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        pauseSyncingAction->setVisible(true);
        automaticAction->setChecked(true);
        syncingTimeMenu->menuAction()->setVisible(true);
        updateNextSyncingTime();
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
    if (syncEvery != std::numeric_limits<int>::max())
    {
        syncTimeMultiplier++;
        decreaseSyncTimeAction->setDisabled(false);
        updateNextSyncingTime();
    }
}

/*
===================
MainWindow::decreaseSyncTime
===================
*/
void MainWindow::decreaseSyncTime()
{
    syncTimeMultiplier--;

    if (syncTimeMultiplier <= 1)
    {
        syncTimeMultiplier = 1;
        decreaseSyncTimeAction->setDisabled(true);
    }

    updateNextSyncingTime();
}

/*
===================
MainWindow::switchDeletionMode
===================
*/
void MainWindow::switchDeletionMode(DeletionMode mode)
{
    deletionMode = mode;

    moveToTrashAction->setChecked(false);
    moveToVersionFolderAction->setChecked(false);
    deletePermanentlyAction->setChecked(false);

    if (mode == MoveToTrash)
        moveToTrashAction->setChecked(true);
    else if (mode == MoveToVersionFolder)
        moveToVersionFolderAction->setChecked(true);
    else
        deletePermanentlyAction->setChecked(true);
}

/*
===================
MainWindow::sync
===================
*/
void MainWindow::sync(int profileNumber)
{
    if (profiles.isEmpty()) return;

    if (!queue.contains(profileNumber))
    {
        // Adds the passed profile number to the sync queue
        if (profileNumber >= 0 && profileNumber < profiles.size())
        {
            if ((!profiles[profileNumber].paused || syncingMode != Automatic) && !profiles[profileNumber].toBeRemoved)

            queue.enqueue(profileNumber);
        }
        // If a profile number is not passed, adds all remaining profiles to the sync queue
        else
        {
            for (int i = 0; i < profiles.size(); i++)
            {
                if ((!profiles[i].paused || syncingMode != Automatic) && !profiles[i].toBeRemoved && !queue.contains(i))
                {
                    queue.enqueue(i);
                }
            }
        }
    }

    if (busy || queue.isEmpty()) return;

    animSync.start();
    busy = true;
    syncing = false;
    for (auto &action : syncingModeMenu->actions()) action->setEnabled(false);
    for (auto &action : deletionModeMenu->actions()) action->setEnabled(false);

#ifdef DEBUG
    std::chrono::high_resolution_clock::time_point syncTime;
    debugSetTime(syncTime);
#endif

    if (!paused)
    {
        int activeFolders = 0;
        QElapsedTimer timer;
        timer.start();

        // Counts active folders in a profile
        for (auto &folder : profiles[queue.head()].folders)
        {
            folder.exists = QFileInfo::exists(folder.path);

            if (!folder.paused && folder.exists && !folder.toBeRemoved)
                activeFolders++;
        }

        if (activeFolders >= 2)
        {
#ifdef DEBUG
            qDebug("=======================================");
            qDebug("Started syncing %s", qUtf8Printable(ui->syncProfilesView->model()->index(queue.head(), 0).data().toString()));
            qDebug("=======================================");
#endif

            // Gets lists of all files in folders
            SET_TIME(startTime);

            bool isFinished = false;
            QList<QFuture<int>> futureList;
            QSet<int> finished;
            QList<hash64_t> devicesRequired;

            for (auto &folder : profiles[queue.head()].folders)
                devicesRequired.append(hash64(QStorageInfo(folder.path).device()));

            while (!isFinished)
            {
                int i = 0;

                for (auto &folder : profiles[queue.head()].folders)
                {
                    if (finished.contains(i)) continue;

                    hash64_t device = devicesRequired.value(i);

                    if (!usedDevices.contains(device))
                    {
                        finished.insert(i);
                        usedDevices.insert(device);
                        futureList.append(QFuture(QtConcurrent::run([&](){ return getListOfFiles(folder); })));
                    }

                    i++;
                }

                for (const auto &future : futureList)
                {
                    if (future.isFinished())
                    {
                        isFinished = true;
                    }
                    else
                    {
                        isFinished = false;
                        break;
                    }
                }

                if (updateApp()) return;
            }

            int result = 0;

            for (const auto &future : futureList)
                result += future.result();

            TIMESTAMP(startTime, "Found %d files in %s.", result, qUtf8Printable(ui->syncProfilesView->model()->index(queue.head(), 0).data().toString()));

            checkForChanges(profiles[queue.head()]);

            bool countAverage = profiles[queue.head()].syncTime ? true : false;
            profiles[queue.head()].syncTime += timer.elapsed();
            if (countAverage) profiles[queue.head()].syncTime /= 2;

#ifdef DEBUG
            int numOfFilesToMove = 0;
            int numOfFoldersToAdd = 0;
            int numOfFilesToAdd = 0;
            int numOfFoldersToRemove = 0;
            int numOfFilesToRemove = 0;

            for (auto &folder : profiles[queue.head()].folders)
            {
                numOfFilesToMove += folder.filesToMove.size();
                numOfFoldersToAdd += folder.foldersToAdd.size();
                numOfFilesToAdd += folder.filesToAdd.size();
                numOfFoldersToRemove += folder.foldersToRemove.size();
                numOfFilesToRemove += folder.filesToRemove.size();
            }

            if (numOfFilesToMove || numOfFoldersToAdd || numOfFilesToAdd || numOfFoldersToRemove || numOfFilesToRemove)
            {
                qDebug("---------------------------------------");
                if (numOfFilesToMove)       qDebug("Files to move: %d", numOfFilesToMove);
                if (numOfFoldersToAdd)      qDebug("Folders to add: %d", numOfFoldersToAdd);
                if (numOfFilesToAdd)        qDebug("Files to add: %d", numOfFilesToAdd);
                if (numOfFoldersToRemove)   qDebug("Folders to remove: %d", numOfFoldersToRemove);
                if (numOfFilesToRemove)     qDebug("Files to remove: %d", numOfFilesToRemove);
                qDebug("---------------------------------------");
            }
#endif

            updateStatus();
            if (updateApp()) return;
        }

        if (syncing)
        {
            // Synchronizes files/folders
            for (auto &folder : profiles[queue.head()].folders)
            {
                QString rootPath = QStorageInfo(folder.path).rootPath();
                bool shouldNotify = notificationList.contains(rootPath) ? !notificationList.value(rootPath)->isActive() : true;
                QSet<QString> foldersToUpdate;

                QString timeStampFolder(folder.path);
                timeStampFolder.append(versionFolder);
                timeStampFolder.append("/");
                timeStampFolder.append(QDateTime::currentDateTime().toString(versionPattern));
                timeStampFolder.append("/");

                auto createParentFolders = [&](QString path)
                {
                    QStack<QString> foldersToCreate;

                    while ((path = QFileInfo(path).path()).length() > folder.path.length())
                    {
                        if (QDir(path).exists()) break;
                        foldersToCreate.append(path);
                    }

                    while (!foldersToCreate.isEmpty())
                    {
                        if (QDir().mkdir(foldersToCreate.top()))
                        {
                            QString shortPath(foldersToCreate.top());
                            shortPath.remove(0, folder.path.size());
                            hash64_t hash = hash64(shortPath.toUtf8());

                            folder.files.insert(hash, File(shortPath.toUtf8(), File::folder, QFileInfo(foldersToCreate.top()).lastModified()));
                            folder.foldersToAdd.remove(hash);
                            foldersToUpdate.insert(foldersToCreate.top());
                        }

                        foldersToCreate.pop();
                    }
                };

                // Sorts the folders for removal from the top to the bottom.
                // This ensures that the trash folder has the exact same folder structure as in the original destination.
                QVector<QString> sortedFoldersToRemove;
                sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
                for (const auto &str : qAsConst(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
                std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

                // Files to move
                for (auto it = folder.filesToMove.begin(); it != folder.filesToMove.end() && (!paused && !folder.paused);)
                {
                    // Removes from the "files to move" list if the source file doesn't exist
                    if (!QFileInfo::exists(it.value().second))
                    {
                        it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                        continue;
                    }

                    QString filePath(folder.path);
                    filePath.append(it.value().first);
                    hash64_t fileHash = hash64(it.value().first);

                    createParentFolders(QDir::cleanPath(filePath));

                    // TODO: Fix the case if a file exists at the given destination file path
                    QFuture<bool> future = QtConcurrent::run([&](){ return QFile::rename(it.value().second, filePath); });
                    while (!future.isFinished()) updateApp();

                    if (future.result())
                    {
                        QString parentFrom = QFileInfo(filePath).path();
                        QString parentTo = QFileInfo(it.value().second).path();

                        if (QFileInfo::exists(parentFrom)) foldersToUpdate.insert(parentFrom);
                        if (QFileInfo::exists(parentTo)) foldersToUpdate.insert(parentTo);

                        folder.files.remove(hash64(QByteArray(it.value().second).remove(0, folder.path.size())));
                        folder.files.insert(fileHash, File(it.value().first, File::file, QFileInfo(filePath).lastModified()));
                        folder.sizeList[fileHash] = QFileInfo(filePath).size();
                        it = folder.filesToMove.erase(static_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (updateApp() && rememberFiles) return;
                }

                // Folders to remove
                for (auto it = sortedFoldersToRemove.begin(); it != sortedFoldersToRemove.end() && (!paused && !folder.paused);)
                {
                    QString folderPath(folder.path);
                    folderPath.append(*it);
                    hash64_t fileHash = hash64(it->toUtf8());
                    QString parentPath = QFileInfo(folderPath).path();

                    QFuture<bool> future = QtConcurrent::run([&]()
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            // Used to make sure that moveToTrash function really moved a folder
                            // to the trash as it can return true even though it failed to do so
                            QString pathInTrash;

                            return QFile::moveToTrash(folderPath) && !pathInTrash.isEmpty();
                        }
                        else if (deletionMode == MoveToVersionFolder)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(*it);

                            createParentFolders(QDir::cleanPath(newLocation));
                            return QDir().rename(folderPath, newLocation);
                        }
                        else
                        {
                            return QDir(folderPath).removeRecursively();
                        }
                    });

                    while (!future.isFinished()) updateApp();

                    if (future.result() || !QDir().exists(folderPath))
                    {
                        folder.files.remove(fileHash);
                        folder.foldersToRemove.remove(fileHash);
                        it = sortedFoldersToRemove.erase(static_cast<QVector<QString>::const_iterator>(it));

                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (updateApp() && rememberFiles) return;
                }

                // Files to remove
                for (auto it = folder.filesToRemove.begin(); it != folder.filesToRemove.end() && (!paused && !folder.paused);)
                {
                    QString filePath(folder.path);
                    filePath.append(*it);
                    hash64_t fileHash = hash64(*it);
                    QString parentPath = QFileInfo(filePath).path();

                    QFuture<bool> future = QtConcurrent::run([&]()
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            // Used to make sure that moveToTrash function really moved a folder
                            // to the trash as it can return true even though it failed to do so
                            QString pathInTrash;

                            return QFile::moveToTrash(filePath, &pathInTrash) && !pathInTrash.isEmpty();
                        }
                        else if (deletionMode == MoveToVersionFolder)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(*it);

                            createParentFolders(QDir::cleanPath(newLocation));
                            return QFile().rename(filePath, newLocation);
                        }
                        else
                        {
                            return QFile::remove(filePath) || !QFileInfo::exists(filePath);
                        }
                    });

                    while (!future.isFinished()) updateApp();

                    if (future.result() || !QFile().exists(filePath))
                    {
                        folder.files.remove(fileHash);
                        it = folder.filesToRemove.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(it));

                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (updateApp() && rememberFiles) return;
                }

                // Folders to add
                for (auto it = folder.foldersToAdd.begin(); it != folder.foldersToAdd.end() && (!paused && !folder.paused);)
                {
                    QString folderPath(folder.path);
                    folderPath.append(*it);
                    hash64_t fileHash = hash64(*it);
                    QFileInfo fileInfo(folderPath);

                    createParentFolders(QDir::cleanPath(folderPath));

                    // Removes a file with the same filename first if exists
                    if (fileInfo.exists() && fileInfo.isFile())
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            QFile::moveToTrash(folderPath);
                        }
                        else if (deletionMode == MoveToVersionFolder)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(*it);

                            createParentFolders(QDir::cleanPath(newLocation));
                            QFile::rename(folderPath, newLocation);
                        }
                        else
                        {
                            QFile::remove(folderPath);
                        }
                    }

                    if (QDir().mkdir(folderPath) || fileInfo.exists())
                    {
                        folder.files.insert(fileHash, File(*it, File::folder, fileInfo.lastModified()));
                        it = folder.foldersToAdd.erase(static_cast<QHash<hash64_t, QByteArray>::const_iterator>(it));

                        QString parentPath = fileInfo.path();
                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (updateApp() && rememberFiles) return;
                }

                // Files to copy
                for (auto it = folder.filesToAdd.begin(); it != folder.filesToAdd.end() && (!paused && !folder.paused);)
                {
                    // Removes from the "files to add" list if the source file doesn't exist
                    if (!QFileInfo::exists(it.value().first.second) || it.value().first.first.isEmpty() || it.value().first.second.isEmpty())
                    {
                        it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                        continue;
                    }

                    QString filePath(folder.path);
                    filePath.append(it.value().first.first);
                    hash64_t fileHash = hash64(it.value().first.first);
                    const File &file = folder.files.value(fileHash);
                    QFileInfo destination(filePath);

                    // Destination file is a newly added file
                    if (file.type == File::unknown && destination.exists())
                    {
                        QFileInfo origin(it.value().first.second);

                        // Aborts the copy operation if the origin file is older than the destination file
                        if (destination.lastModified() > origin.lastModified())
                        {
                            it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                            continue;
                        }

                        // Fixes the case of two new files in two folders (one file for each folder) with the same file names but in different cases (e.g. filename vs. FILENAME)
                        // Without this, copy operation causes undefined behavior as some file systems, such as Windows, are case insensitive.
                        if (!caseSensitiveSystem)
                        {
                            QDirIterator originIterator(origin.absolutePath(), {origin.fileName()}, QDir::Files);

                            // Using QDirIterator is the only way to find out the current filename on a disk
                            if (originIterator.hasNext())
                            {
                                originIterator.next();

                                // Aborts the copy operation if the origin path and the path on a disk have different cases
                                if (originIterator.filePath().compare(it.value().first.second, Qt::CaseSensitive) != 0)
                                {
                                    it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));
                                    continue;
                                }
                            }
                        }
                    }

                    createParentFolders(QDir::cleanPath(filePath));

                    // Removes a file with the same filename first if it exists
                    if (destination.exists())
                    {
                        if (deletionMode == MoveToTrash)
                        {
                            QFile::moveToTrash(filePath);
                        }
                        else if (deletionMode == MoveToVersionFolder)
                        {
                            QString newLocation(timeStampFolder);
                            newLocation.append(it.value().first.first);

                            createParentFolders(QDir::cleanPath(newLocation));
                            QFile::rename(filePath, newLocation);
                        }
                        else
                        {
                            if (file.type == File::folder)
                                QDir(filePath).removeRecursively();
                            else
                                QFile::remove(filePath);
                        }
                    }

                    QFuture<bool> future = QtConcurrent::run([&](){ return QFile::copy(it.value().first.second, filePath); });
                    while (!future.isFinished()) updateApp();

                    if (future.result())
                    {
                        folder.files.insert(fileHash, File(it.value().first.first, File::file, destination.lastModified()));
                        folder.sizeList[fileHash] = QFileInfo(filePath).size();
                        it = folder.filesToAdd.erase(static_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::const_iterator>(it));

                        QString parentPath = destination.path();
                        if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                    }
                    else
                    {
                        // Not enough disk space notification
                        if (notifications && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(it.value().first.second).size())
                        {
                            if (!notificationList.contains(rootPath))
                                notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                            shouldNotify = false;
                            notificationList.value(rootPath)->start(NOTIFICATION_DELAY);
                            trayIcon->showMessage(QString("Not enough disk space on %1 (%2)").arg(QStorageInfo(folder.path).displayName(), rootPath), "", QSystemTrayIcon::Critical, 1000);
                        }

                        ++it;
                    }

                    // Returns only if "remember files" is enabled, otherwise all made changes will be lost
                    if (updateApp() && rememberFiles) return;
                }

                // Updates the modified date of the parent folders as adding/removing files and folders change their modified date
                for (auto &folderPath : foldersToUpdate)
                {
                    hash64_t folderHash = hash64(QByteArray(folderPath.toUtf8()).remove(0, folder.path.size()));
                    if (folder.files.contains(folderHash)) folder.files[folderHash].date = QFileInfo(folderPath).lastModified();
                }
            }
        }
    }

    // Optimizes memory usage
    for (auto &folder : profiles[queue.head()].folders)
    {
        folder.files.squeeze();
        folder.filesToMove.squeeze();
        folder.foldersToAdd.squeeze();
        folder.filesToAdd.squeeze();
        folder.foldersToRemove.squeeze();
        folder.filesToRemove.squeeze();

        for (auto &file : folder.files)
            file.path.clear();

        for (auto it = folder.sizeList.begin(); it != folder.sizeList.end();)
        {
            if (!folder.files.contains(it.key()))
                it = folder.sizeList.erase(static_cast<QHash<hash64_t, qint64>::const_iterator>(it));
            else
                ++it;
        }
    }

    // Updates the last synchronization date
    profiles[queue.head()].lastSyncDate = QDateTime::currentDateTime();
    QString lastSync = QString("Last synchronization: %1.").arg(profiles[queue.head()].lastSyncDate.toString());
    profileModel->setData(profileModel->index(queue.head(), 0), lastSync, Qt::ToolTipRole);

    queue.dequeue();
    busy = false;
    updateStatus();
    updateNextSyncingTime();

    TIMESTAMP(syncTime, "Syncing is complete.");

    // Starts synchronization of the next profile in the queue if exists
    if (!queue.empty()) sync(queue.head());

    // Removes profiles/folders completely if we remove them during syncing
    for (auto profileIt = profiles.begin(); profileIt != profiles.end();)
    {
        // Profiles
        if (profileIt->toBeRemoved)
        {
            profileIt = profiles.erase(static_cast<QList<SyncProfile>::const_iterator>(profileIt));
            continue;
        }

        // Folders
        for (auto folderIt = profileIt->folders.begin(); folderIt != profileIt->folders.end();)
        {
            if (folderIt->toBeRemoved)
                folderIt = profileIt->folders.erase(static_cast<QList<SyncFolder>::const_iterator>(folderIt));
            else
                folderIt++;
        }

        profileIt++;
    }

    for (auto &action : syncingModeMenu->actions()) action->setEnabled(true);
    for (auto &action : deletionModeMenu->actions()) action->setEnabled(true);
    syncHidden = false;
    animSync.stop();
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    bool isThereIssue = true;
    bool isThereWarning = false;
    int existingProfiles = 0;
    syncing = false;
    numOfFilesToSync = 0;

    // Syncing status
    for (int i = -1; auto &profile : profiles)
    {
        i++;
        profile.syncing = false;
        int existingFolders = 0;

        existingProfiles++;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((!queue.isEmpty() && queue.head() != i ) || profile.toBeRemoved)
                continue;

            if (!folder.toBeRemoved)
            {
                if (folder.exists)
                {
                    existingFolders++;

                    if (existingFolders >= 2) isThereIssue = false;
                }
                else
                {
                    isThereWarning = true;
                }
            }

            if (busy && folder.exists && !folder.paused && (!folder.foldersToAdd.isEmpty() || !folder.filesToAdd.isEmpty() || !folder.filesToMove.isEmpty() || !folder.foldersToRemove.isEmpty() || !folder.filesToRemove.isEmpty()))
            {
                animSync.start();
                syncing = true;
                profile.syncing = true;
                folder.syncing = true;
            }
        }
    }

    if (isVisible())
    {
        // Profile list
        for (int i = 0, j = 0; i < profiles.size(); i++)
        {
            if (profiles[i].toBeRemoved) continue;

            QModelIndex index = profileModel->index(j, 0);

            if (profiles[i].paused)
            {
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            }
            else if (profiles[i].syncing || (!syncHidden && queue.contains(i)))
            {
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            }
            else
            {
                profileModel->setData(index, iconDone, Qt::DecorationRole);

                // Shows a warning icon if at least one folder doesn't exist
                for (const auto &folder : profiles[i].folders)
                {
                    if (!folder.exists)
                    {
                        profileModel->setData(index, iconWarning, Qt::DecorationRole);
                        break;
                    }
                }
            }

            ui->syncProfilesView->update(index);
            j++;
        }

        // Folders
        if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        {
            int row = ui->syncProfilesView->selectionModel()->selectedRows()[0].row();

            for (int i = 0, j = 0; i < profiles[row].folders.size(); i++)
            {
                if (profiles[row].folders[j].toBeRemoved) continue;

                QModelIndex index = folderModel->index(i, 0);

                if (profiles[row].folders[i].paused)
                    folderModel->setData(index, iconPause, Qt::DecorationRole);
                else if (profiles[row].folders[i].syncing || (queue.contains(row) && !syncing && !syncHidden))
                    folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                else if (!profiles[row].folders[i].exists)
                    folderModel->setData(index, iconRemove, Qt::DecorationRole);
                else
                    folderModel->setData(index, iconDone, Qt::DecorationRole);

                ui->folderListView->update(index);
                j++;
            }
        }
    }

    // Pause status
    for (const auto &profile : profiles)
    {
        paused = profile.paused;
        if (!paused) break;
    }

    // Tray & Icon
    if (syncingMode == Automatic && paused)
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
        if (syncing || (!syncHidden && !queue.empty()))
        {
            if (trayIcon->icon().cacheKey() != trayIconSync.cacheKey())
                trayIcon->setIcon(trayIconSync);

            if (windowIcon().cacheKey() != trayIconSync.cacheKey())
                setWindowIcon(trayIconSync);
        }
        else if (isThereWarning)
        {
            if (isThereIssue)
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

    syncNowAction->setEnabled(queue.size() != existingProfiles);

    // Number of files left to sync
    if (busy)
    {
        for (auto &folder : profiles[queue.head()].folders)
            if (folder.exists && !folder.paused)
                numOfFilesToSync += folder.foldersToAdd.size() + folder.filesToAdd.size() + folder.filesToMove.size() + folder.foldersToRemove.size() + folder.filesToRemove.size();
    }

    if (!numOfFilesToSync)
    {
        trayIcon->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
    else
    {
        trayIcon->setToolTip(QString("Sync Manager - %1 files to synchronize").arg(numOfFilesToSync));
        setWindowTitle(QString("Sync Manager - %1 files to synchronize").arg(numOfFilesToSync));
    }
}

/*
===================
MainWindow::updateNextSyncingTime
===================
*/
void MainWindow::updateNextSyncingTime()
{
    if (syncingMode == Manual) return;

    int time = 0;

    // Counts the total syncing time of profiles with at least two active folders
    for (const auto &profile : profiles)
    {
        if (profile.paused) continue;

        int activeFolders = 0;

        for (const auto &folder : profile.folders)
            if (!folder.paused && folder.exists)
                activeFolders++;

        if (activeFolders >= 2) time += profile.syncTime;
    }

    // Multiplies sync time by 2
    for (int i = 0; i < syncTimeMultiplier - 1; i++)
    {
        time <<= 1;

        // If exceeds the maximum value of an int
        if (time < 0)
        {
            time = std::numeric_limits<int>::max();
            break;
        }
    }

    if (time < SYNC_MIN_DELAY) time = SYNC_MIN_DELAY;
    syncEvery = time;

    int seconds = (time / 1000) % 60;
    int minutes = (time / 1000 / 60) % 60;
    int hours = (time / 1000 / 60 / 60) % 24;
    int days = (time / 1000 / 60 / 60 / 24);

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

    if ((!busy && syncTimer.isActive()) || (!syncTimer.isActive() || time < syncTimer.remainingTime()))
        syncTimer.start(time);
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
    return shouldQuit;
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

            if (profiles[row].paused)
            {
                menu.addAction(iconResume, "&Resume syncing profile", this, SLOT(pauseSelected()));
            }
            else
            {
                menu.addAction(iconPause, "&Pause syncing profile", this, SLOT(pauseSelected()));
                menu.addAction(iconSync, "&Synchronize profile", this, std::bind(&MainWindow::sync, const_cast<MainWindow *>(this), row))->setDisabled(queue.contains(row));
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
            if (profiles[ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row()].folders[ui->folderListView->selectionModel()->selectedIndexes()[0].row()].paused)
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
MainWindow::saveData
===================
*/
void MainWindow::saveData() const
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    if (!data.open(QIODevice::WriteOnly)) return;

    QDataStream stream(&data);
    stream << profiles.size();

    for (int i = 0; auto &profile : profiles)
    {
        if (profile.toBeRemoved) continue;

        stream << profileNames[i];
        stream << profile.folders.size();

        for (auto &folder : profile.folders)
        {
            if (folder.toBeRemoved) continue;

            // File data
            stream << folder.path;
            stream << folder.files.size();

            for (auto it = folder.files.begin(); it != folder.files.end(); it++)
            {
                stream << it.key();
                stream << it->date;
                stream << it->type;
                stream << it->updated;
                stream << it->exists;
            }

            // File sizes
            stream << folder.sizeList.size();

            for (auto it = folder.sizeList.begin(); it != folder.sizeList.end(); it++)
            {
                stream << it.key();
                stream << it.value();
            }

            // Files to move
            stream << folder.filesToMove.size();

            for (const auto &it : folder.filesToMove)
            {
                stream << it.first;
                stream << it.second;
            }

            // Folders to add
            stream << folder.foldersToAdd.size();

            for (const auto &path : folder.foldersToAdd)
                stream << path;

            // Files to add
            stream << folder.filesToAdd.size();

            for (const auto &it : folder.filesToAdd)
            {
                stream << it.first;
                stream << it.second;
            }

            // Folders to remove
            stream << folder.foldersToRemove.size();

            for (const auto &path : folder.foldersToRemove)
                stream << path;

            // Files to remove
            stream << folder.filesToRemove.size();

            for (const auto &path : folder.filesToRemove)
                stream << path;
        }

        i++;
    }
}

/*
===================
MainWindow::restoreData
===================
*/
void MainWindow::restoreData()
{
    QFile data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    if (!data.open(QIODevice::ReadOnly)) return;

    QDataStream stream(&data);

    qsizetype profilesSize;
    stream >> profilesSize;

    for (qsizetype i = 0; i < profilesSize; i++)
    {
        QString profileName;
        qsizetype foldersSize;

        stream >> profileName;
        stream >> foldersSize;

        int profileIndex = profileNames.indexOf(profileName);

        for (qsizetype j = 0; j < foldersSize; j++)
        {
            qsizetype size;
            QByteArray folderPath;

            stream >> folderPath;
            stream >> size;

            folderPath.chop(1); // Removes a forward slash in the end of the path
            int folderIndex = folderPaths[profileIndex].indexOf(folderPath);
            bool restore = profileIndex >= 0 && folderIndex >= 0;

            // File data
            for (qsizetype k = 0; k < size; k++)
            {
                hash64_t hash;
                QDateTime date;
                File::Type type;
                bool updated;
                bool exists;

                stream >> hash;
                stream >> date;
                stream >> type;
                stream >> updated;
                stream >> exists;

                if (restore)
                {
                    const_cast<File *>(profiles[profileIndex].folders[folderIndex].files.insert(hash, File(QByteArray(), type, date, updated, exists, true)).operator->())->path.squeeze();
                }
            }

            // File sizes
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                hash64_t fileHash;
                qint64 fileSize;

                stream >> fileHash;
                stream >> fileSize;

                if (restore)
                {
                    profiles[profileIndex].folders[folderIndex].sizeList.insert(fileHash, fileSize);
                }
            }


            // Files to move
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray to;
                QByteArray from;

                stream >> to;
                stream >> from;

                if (restore)
                {
                    QPair<QByteArray, QByteArray> pair(to, from);
                    const auto &it = profiles[profileIndex].folders[folderIndex].filesToMove.insert(hash64(to), pair);
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().first.squeeze();
                    const_cast<QHash<hash64_t, QPair<QByteArray, QByteArray>>::iterator &>(it).value().second.squeeze();
                }
            }

            // Folders to add
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToAdd.insert(hash64(path), path).operator->())->squeeze();
                }
            }

            // Files to add
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray to;
                QByteArray from;
                QDateTime time;

                stream >> to;
                stream >> from;
                stream >> time;

                if (restore)
                {
                    QPair<QPair<QByteArray, QByteArray>, QDateTime> pair(QPair<QByteArray, QByteArray>(to, from), time);
                    const auto &it = profiles[profileIndex].folders[folderIndex].filesToAdd.insert(hash64(to), pair);
                    const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.first.squeeze();
                    const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.second.squeeze();
                }
            }

            // Folders to remove
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToRemove.insert(hash64(path), path).operator->())->squeeze();
                }
            }

            // Files to remove
            stream >> size;

            for (qsizetype k = 0; k < size; k++)
            {
                QByteArray path;
                stream >> path;

                if (restore)
                {
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].filesToRemove.insert(hash64(path), path).operator->())->squeeze();
                }
            }
        }
    }
}

/*
===================
MainWindow::getListOfFiles
===================
*/
int MainWindow::getListOfFiles(SyncFolder &folder)
{
    if (folder.paused || !folder.exists) return -1;

    int totalNumOfFiles = 0;

    // Resets file states
    for (auto &file : folder.files)
    {
        file.exists = false;
        file.updated = (file.onRestore) ? file.updated : false;     // Keeps value if it was loaded from saved file data
        file.onRestore = false;
        file.newlyAdded = false;
        file.moved = false;
        file.movedSource = false;
    }

#ifndef USE_STD_FILESYSTEM
    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (folder.paused) return -1;

        dir.next();

        QFileInfo fileInfo(dir.fileInfo());
        QByteArray filePath(fileInfo.filePath().toUtf8());
        filePath.remove(0, folder.path.size());
        File::Type type = fileInfo.isDir() ? File::folder : File::file;
        hash64_t fileHash = hash64(filePath);

        // Excludes the versioning folder from scanning
        QByteArray vf(versionFolder.toUtf8());

        if (caseSensitiveSystem ? qstrncmp(vf, filePath, vf.length()) == 0 : qstrnicmp(vf, filePath, vf.length()) == 0)
            continue;

        // If a file is already in our database
        if (folder.files.contains(fileHash))
        {
            File &file = folder.files[fileHash];
            QDateTime fileDate(fileInfo.lastModified());
            bool updated = file.updated;

            // Restores filepath
            if (file.path.isEmpty())
            {
                file.path = filePath;
                file.path.squeeze();
            }

            // Quits if a hash collision is detected
            if (file.path != filePath)
            {
#ifndef DEBUG
                QMessageBox::critical(nullptr, QString("Hash collision detected!"), QString("%s vs %s").arg(qUtf8Printable(filePath), qUtf8Printable(file.path)));
#else
                qCritical("Hash collision detected: %s vs %s", qUtf8Printable(filePath), qUtf8Printable(file.path));
#endif

                shouldQuit = true;
                qApp->quit();
                return -1;
            }

            // Sets updated flag if the last modified date of a file differs with a new one
            if (type == File::folder)
            {
                updated = (file.date < fileDate);

                // Marks all parent folders as updated if the current folder was updated
                if (updated)
                {
                    QByteArray folderPath(fileInfo.filePath().toUtf8());

                    while (folderPath.remove(folderPath.lastIndexOf("/"), folderPath.length()).length() > folder.path.length())
                    {
                        hash64_t hash = hash64(QByteArray(folderPath).remove(0, folder.path.size()));
                        if (folder.files.value(hash).updated) break;

                        folder.files[hash].updated = true;
                    }
                }
            }
            else if (type == File::file && file.date != fileDate)
            {
                updated = true;
            }

            file.date = fileDate;
            file.type = type;
            file.exists = true;
            file.updated = updated;
        }
        else
        {
            File *file = const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, fileInfo.lastModified())).operator->());
            file->path.squeeze();
            file->newlyAdded = true;
        }

        if (type == File::file) folder.sizeList[fileHash] = fileInfo.size();

        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#elif defined(USE_STD_FILESYSTEM)
#error FIX: An update required
    for (auto const &dir : std::filesystem::recursive_directory_iterator{std::filesystem::path{folder.path.toStdString()}})
    {
        if (folder.paused) return -1;

        QString filePath(dir.path().string().c_str());
        filePath.remove(0, folder.path.size());
        filePath.replace("\\", "/");

        File::Type type = dir.is_directory() ? File::dir : File::file;
        hash_t fileHash = hash64(filePath);

        const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, QDateTime(), false)).operator->())->path.squeeze();
        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#endif

    usedDevices.remove(hash64(QStorageInfo(folder.path).device()));

    return totalNumOfFiles;
}

/*
===================
MainWindow::checkForChanges
===================
*/
void MainWindow::checkForChanges(SyncProfile &profile)
{
    if ((syncingMode == Automatic && profile.paused) || profile.folders.size() < 2) return;

    SET_TIME(startTime);

    // Checks for moved/renamed files
    if (detectMovedFiles)
    {
        for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
        {
            for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin(); fileIt != folderIt->files.end(); ++fileIt)
            {
                bool needToCheck = false;

                // Only a newly added file could be renamed or moved
                if (!fileIt->newlyAdded || fileIt->type != File::file || !fileIt->exists)
                    continue;

                // Checks if other folders contain the same file or not
                for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                {
                    if (folderIt == otherFolderIt) continue;

                    if (otherFolderIt->files.value(fileIt.key()).type == File::unknown)
                    {
                        needToCheck = true;
                        break;
                    }
                }

                if (!needToCheck) continue;

                int matches = 0;
                File *matchedFile;
                hash64_t matchedHash;
                QFileInfo fileInfo(QString(folderIt->path).append(fileIt->path));
                auto keys = folderIt->sizeList.keys(fileInfo.size());

                // Searches for potential matches by comparing the size of moved or renamed files to the size of their counterparts
                for (auto &match : keys)
                {
                    if (!folderIt->files.contains(match)) continue;

                    // A potential match should not exist and have the same modified date as the moved/renamed file
                    if (!folderIt->files.value(match).exists && fileInfo.lastModified() == fileIt->date)
                    {
                        matches++;
                        matchedFile = &folderIt->files[match];
                        matchedHash = match;

                        // We only need one match, not more
                        if (matches > 1) break;
                    }
                }

                if (matches == 1)
                {
                    bool needToMove = false;

                    for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
                    {
                        if (folderIt == otherFolderIt || !otherFolderIt->files.contains(matchedHash))
                            continue;

                        const File &otherFile = otherFolderIt->files.value(matchedHash);                        

                        if (!otherFile.exists)
                            continue;

                        // We do not want to move files that have different sizes and dates
                        if (otherFolderIt->sizeList.value(matchedHash) != folderIt->sizeList.value(matchedHash) || otherFile.date != fileIt->date)
                            continue;

                        hash64_t hash = hash64(fileIt->path);

                        // Marks a file as moved, which prevents it from being added to other folders
                        if (!otherFolderIt->files.contains(hash))
                            otherFolderIt->files[hash].moved = true;

                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);
                        otherFolderIt->filesToMove.insert(matchedHash, QPair<QByteArray, QByteArray>(fileIt->path, from));
                        needToMove = true;

                        qDebug("Moving file from %s to %s", qUtf8Printable(from), qUtf8Printable(fileIt->path));
                    }

                    if (needToMove)
                    {
                        matchedFile->moved = true;
                        matchedFile->movedSource = true;
                        fileIt->moved = true;
                    }
                }
            }
        }
    }

    TIMESTAMP(startTime, "Checked for moved/renamed files.");
    SET_TIME(startTime);

    // Checks for added/modified files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists) continue;
            if (folderIt->paused) break;

            for (QHash<hash64_t, File>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (otherFolderIt->paused) break;
                if (!otherFileIt.value().exists) continue;

                const File &file = folderIt->files.value(otherFileIt.key());
                const File &otherFile = otherFileIt.value();
                bool newFile = (file.type == File::unknown);

                if (file.moved || otherFile.moved || otherFile.movedSource) continue;

                bool alreadyAdded = folderIt->filesToAdd.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && folderIt->filesToAdd.value(otherFileIt.key()).second < otherFile.date;

                // Removes a file path from the "to remove" list if a file was updated
                if (otherFile.type == File::file)
                {
                    if (otherFile.updated)
                        otherFolderIt->filesToRemove.remove(otherFileIt.key());

                    if (file.updated || (otherFile.exists && folderIt->filesToRemove.contains(otherFileIt.key()) && !otherFolderIt->filesToRemove.contains(otherFileIt.key())))
                        folderIt->filesToRemove.remove(otherFileIt.key());
                }
                else if (otherFile.type == File::folder)
                {
                    if (otherFile.updated)
                        otherFolderIt->foldersToRemove.remove(otherFileIt.key());

                    if (file.updated || (otherFile.exists && folderIt->foldersToRemove.contains(otherFileIt.key()) && !otherFolderIt->foldersToRemove.contains(otherFileIt.key())))
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                }

                if ((newFile ||
                // Or if we have a newer version of a file from other folders
                (caseSensitiveSystem && (file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated && otherFile.updated && file.date < otherFile.date)))) ||
                (!caseSensitiveSystem && (file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated == otherFile.updated && file.date < otherFile.date)))) ||
                // Or if other folders has a new version of a file and our file was removed
                (!file.exists && (otherFile.updated || otherFolderIt->files.value(hash64(QByteArray(otherFile.path).remove(QByteArray(otherFile.path).indexOf('/', 0), 999999))).updated))) &&
                // Checks for the newest version of a file in case if we have three folders or more
                (!alreadyAdded || hasNewer) &&
                // Checks whether we supposed to remove this file or not
                ((otherFile.type == File::file && !otherFolderIt->filesToRemove.contains(otherFileIt.key())) ||
                 (otherFile.type == File::folder && !otherFolderIt->foldersToRemove.contains(otherFileIt.key()))))
                {
                    if (otherFile.type == File::folder)
                    {
                        const_cast<QByteArray *>(folderIt->foldersToAdd.insert(otherFileIt.key(), otherFile.path).operator->())->squeeze();
                        folderIt->foldersToRemove.remove(otherFileIt.key());
                    }
                    else
                    {
                        QByteArray from(otherFolderIt->path);
                        from.append(otherFile.path);
                        QPair<QPair<QByteArray, QByteArray>, QDateTime> pair(QPair<QByteArray, QByteArray>(otherFile.path, from), otherFile.date);
                        const auto &it = folderIt->filesToAdd.insert(otherFileIt.key(), pair);
                        const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.first.squeeze();
                        const_cast<QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>>::iterator &>(it).value().first.second.squeeze();
                        folderIt->filesToRemove.remove(otherFileIt.key());
                    }
                }
            }
        }
    }

    TIMESTAMP(startTime, "Checked for added/modified files and folders.");
    SET_TIME(startTime);

    // Checks for removed files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (QHash<hash64_t, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (folderIt->paused) break;

            //
            if (fileIt.value().movedSource)
            {
                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
                continue;
            }

            if (fileIt.value().moved)
            {
                ++fileIt;
                continue;
            }

            if (!fileIt.value().exists && !folderIt->filesToMove.contains(fileIt.key()) && !folderIt->foldersToAdd.contains(fileIt.key()) && !folderIt->filesToAdd.contains(fileIt.key()) &&
               ((fileIt.value().type == File::file && !folderIt->filesToRemove.contains(fileIt.key())) ||
               (fileIt.value().type == File::folder && !folderIt->foldersToRemove.contains(fileIt.key()))))
            {
                // Adds files from other folders for removal
                for (auto removeIt = profile.folders.begin(); removeIt != profile.folders.end(); ++removeIt)
                {
                    if (folderIt == removeIt || !removeIt->exists || removeIt->paused) continue;

                    const File &fileToRemove = removeIt->files.value(fileIt.key());

                    if (fileToRemove.exists)
                    {
                        if (fileIt.value().type == File::folder)
                            const_cast<QByteArray *>(removeIt->foldersToRemove.insert(fileIt.key(), fileToRemove.path).operator->())->squeeze();
                        else
                            const_cast<QByteArray *>(removeIt->filesToRemove.insert(fileIt.key(), fileToRemove.path).operator->())->squeeze();
                    }
                    else
                    {
                        removeIt->files.remove(fileIt.key());
                    }
                }

                fileIt = folderIt->files.erase(static_cast<QHash<quint64, File>::const_iterator>(fileIt));
            }
            else
            {
                ++fileIt;
            }
        }
    }

    TIMESTAMP(startTime, "Checked for removed files.");
}
