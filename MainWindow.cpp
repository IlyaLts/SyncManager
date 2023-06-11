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
quint64 hash64(const QByteArray &str)
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
    notificationsEnabled = QSystemTrayIcon::supportsMessages() && settings.value("Notifications", true).toBool();
    moveToTrash = settings.value("MoveToTrash", false).toBool();
    rememberFilesEnabled = settings.value("RememberFiles", true).toBool();
    syncTimeMultiplier = settings.value("SyncTimeMultiplier", 1).toInt();
    if (syncTimeMultiplier <= 0) syncTimeMultiplier = 1;

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
    animSync.start();

    syncNowAction = new QAction(iconSync, "&Sync Now", this);
    pauseSyncingAction = new QAction(iconPause, "&Pause Syncing", this);
    automaticAction = new QAction("&Automatic", this);
    manualAction = new QAction("&Manual", this);
    increaseSyncTimeAction = new QAction("&Increase", this);
    syncingTimeAction = new QAction(this);
    decreaseSyncTimeAction = new QAction("&Decrease", this);
    launchOnStartupAction = new QAction("&Launch on Startup", this);
    showInTrayAction = new QAction("&Show in System Tray");
    disableNotificationAction = new QAction("&Disable Notifications", this);
    moveToTrashAction = new QAction("&Move Files and Folders to Trash", this);
    enableRememberFilesAction = new QAction("&Remember Files (Requires disk space)", this);
    showAction = new QAction("&Show", this);
    quitAction = new QAction("&Quit", this);
    QAction *version = new QAction(QString("Version: %1").arg(SYNCMANAGER_VERSION), this);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(syncTimeMultiplier <= 1);
    version->setDisabled(true);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);
    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    enableRememberFilesAction->setCheckable(true);

    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!notificationsEnabled);
    moveToTrashAction->setChecked(moveToTrash);
    enableRememberFilesAction->setChecked(rememberFilesEnabled);

#ifdef Q_OS_WIN
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk"));
#else
    launchOnStartupAction->setChecked(QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop"));
#endif

    syncingModeMenu = new QMenu("&Syncing Mode", this);
    syncingModeMenu->addAction(automaticAction);
    syncingModeMenu->addAction(manualAction);

    syncingTimeMenu = new QMenu("&Syncing Time", this);
    syncingTimeMenu->addAction(increaseSyncTimeAction);
    syncingTimeMenu->addAction(syncingTimeAction);
    syncingTimeMenu->addAction(decreaseSyncTimeAction);

    settingsMenu = new QMenu("&Settings", this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(syncingModeMenu);
    settingsMenu->addMenu(syncingTimeMenu);
    settingsMenu->addAction(launchOnStartupAction);
    settingsMenu->addAction(showInTrayAction);
    settingsMenu->addAction(disableNotificationAction);
    settingsMenu->addAction(moveToTrashAction);
    settingsMenu->addAction(enableRememberFilesAction);
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
    connect(syncNowAction, SIGNAL(triggered()), this, SLOT(syncNow()));
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(automaticAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Automatic));
    connect(manualAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Manual));
    connect(increaseSyncTimeAction, &QAction::triggered, this, &MainWindow::increaseSyncTime);
    connect(decreaseSyncTimeAction, &QAction::triggered, this, &MainWindow::decreaseSyncTime);
    connect(launchOnStartupAction, &QAction::triggered, this, [this](){ setLaunchOnStartup(launchOnStartupAction->isChecked()); });
    connect(showInTrayAction, &QAction::triggered, this, [this](){ setTrayVisible(!showInTray); });
    connect(disableNotificationAction, &QAction::triggered, this, [this](){ notificationsEnabled = !notificationsEnabled; });
    connect(moveToTrashAction, &QAction::triggered, this, [this](){ moveToTrash = !moveToTrash; });
    connect(enableRememberFilesAction, &QAction::triggered, this, [this](){ rememberFilesEnabled = !rememberFilesEnabled; });
    connect(showAction, &QAction::triggered, this, std::bind(&MainWindow::trayIconActivated, this, QSystemTrayIcon::DoubleClick));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(&syncTimer, SIGNAL(timeout()), this, SLOT(sync()));
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

    // Loads saved pause states and checks folders for existence
    for (int i = 0; i < profiles.size(); i++)
    {
        profiles[i].paused = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), false).toBool();
        if (!profiles[i].paused) paused = false;

        for (auto &folder : profiles[i].folders)
        {
            folder.paused = settings.value(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), false).toBool();
            if (!folder.paused) paused = false;
            folder.exists = QFileInfo::exists(folder.path);

            if (notificationsEnabled && !folder.exists)
                trayIcon->showMessage("Couldn't find folder", folder.path, QSystemTrayIcon::Warning, 1000);
        }
    }

    if (rememberFilesEnabled) restoreData();
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);

    syncTimer.setSingleShot(true);
    updateTimer.setSingleShot(true);

    switchSyncingMode(static_cast<SyncingMode>(settings.value("SyncingMode", Automatic).toInt()));
    updateStatus();

    if (syncingMode == Automatic) syncNow();
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

    // Saves profiles/folders pause states
    for (int i = 0; i < profiles.size(); i++)
    {
        if (!profiles[i].toBeRemoved) settings.setValue(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), profiles[i].paused);

        for (auto &folder : profiles[i].folders)
            if (!folder.toBeRemoved)
                settings.setValue(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);
    }

    settings.setValue("Paused", paused);
    settings.setValue("Fullscreen", isMaximized());
    settings.setValue("HorizontalSplitter", hSizes);
    settings.setValue("SyncingMode", syncingMode);
    settings.setValue("ShowInTray", showInTray);
    settings.setValue("Notifications", notificationsEnabled);
    settings.setValue("MoveToTrash", moveToTrash);
    settings.setValue("RememberFiles", rememberFilesEnabled);
    settings.setValue("SyncTimeMultiplier", syncTimeMultiplier);

    if (rememberFilesEnabled) saveData();

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
void MainWindow::addFolder()
{
    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty()) return;

    int row = ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row();

    QString folderPath = QFileDialog::getExistingDirectory(this, "Browse For Folder", QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);
    if (!folderPath.isEmpty() && !folderPaths[row].contains(folderPath))
    {
        folderPaths[row].append(folderPath);
        profiles[row].folders.append(SyncFolder(paused));
        profiles[row].folders.last().path = folderPath.append("/").toUtf8();

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profileNames[row], folderPaths[row]);

        folderModel->setStringList(folderPaths[row]);
        updateStatus();
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
MainWindow::syncNow
===================
*/
void MainWindow::syncNow()
{
    syncNowTriggered = true;
    updateStatus();
    syncTimer.start(0);
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
    syncTimeMultiplier++;
    decreaseSyncTimeAction->setDisabled(false);
    updateNextSyncingTime();
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
MainWindow::sync
===================
*/
void MainWindow::sync(int profileNumber)
{
    if (profileNumber >= 0 && !queue.contains(profileNumber))
        queue.enqueue(profileNumber);

    if (busy) return;

    currentProfileNumber = profileNumber;

    busy = true;
    syncing = false;
    syncNowAction->setEnabled(false);
    moveToTrashAction->setEnabled(false);
    for (auto &action : syncingModeMenu->actions()) action->setEnabled(false);

#ifdef DEBUG
    std::chrono::high_resolution_clock::time_point syncTime;
    debugSetTime(syncTime);
#endif

    if (!paused || syncNowTriggered)
    {
        // Checks for changes
        for (int i = -1; auto &profile : profiles)
        {
            i++;
            if ((profileNumber >= 0 && profileNumber != i) || profile.toBeRemoved) continue;

            int activeFolders = 0;
            QElapsedTimer timer;
            timer.start();

            for (auto &folder : profile.folders)
            {
                folder.exists = QFileInfo::exists(folder.path);
                if ((syncingMode != Automatic || !folder.paused) && folder.exists && !folder.toBeRemoved) activeFolders++;
            }

            if ((syncingMode == Automatic && profile.paused) || activeFolders < 2) continue;

#ifdef DEBUG
            qDebug("===== Started syncing %s =====", qUtf8Printable(ui->syncProfilesView->model()->index(i, 0).data().toString()));
#endif

            for (auto &folder : profile.folders)
            {
                if (folder.toBeRemoved) continue;

                SET_TIME(startTime);

                QFuture<int> future = QtConcurrent::run([&](){ return getListOfFiles(folder); });
                while (!future.isFinished())
                {
                    if (updateApp()) return;
                }

                TIMESTAMP(startTime, "Found %d files in %s.", future.result(), qUtf8Printable(folder.path));
            }

            checkForChanges(profile);

            profile.syncTime = profile.syncTime ? (profile.syncTime + timer.elapsed()) / 2 : timer.elapsed();
        }

        syncNowTriggered = false;

#ifdef DEBUG
        int numOfFoldersToAdd = 0;
        int numOfFilesToAdd = 0;
        int numOfFoldersToRemove = 0;
        int numOfFilesToRemove = 0;

        for (int i = -1; auto &profile : profiles)
        {
            i++;

            for (auto &folder : profile.folders)
            {
                if (folder.paused && syncingMode == Automatic) continue;
                if ((profileNumber >= 0 && profileNumber != i) || profile.toBeRemoved) continue;

                numOfFoldersToAdd += folder.foldersToAdd.size();
                numOfFilesToAdd += folder.filesToAdd.size();
                numOfFoldersToRemove += folder.foldersToRemove.size();
                numOfFilesToRemove += folder.filesToRemove.size();
            }
        }

        qDebug("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
        qDebug("Folders to add: %d, Files to add: %d, Folders to remove: %d, Files to remove: %d", numOfFoldersToAdd, numOfFilesToAdd, numOfFoldersToRemove, numOfFilesToRemove);
#endif

        updateStatus();
        if (updateApp()) return;

        if (syncing)
        {
            // Synchronizes files/folders
            for (auto &profile : profiles)
            {
                for (auto &folder : profile.folders)
                {
                    QString rootPath = QStorageInfo(folder.path).rootPath();
                    bool shouldNotify = notificationList.contains(rootPath) ? !notificationList.value(rootPath)->isActive() : true;
                    QSet<QString> foldersToUpdate;

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
                            QDir().mkdir(foldersToCreate.top());
                            QString shortPath(foldersToCreate.top());
                            shortPath.remove(0, folder.path.size());
                            folder.files.insert(hash64(shortPath.toUtf8()), File(shortPath.toUtf8(), File::folder, QFileInfo(foldersToCreate.top()).lastModified()));
                            folder.foldersToAdd.remove(hash64(foldersToCreate.top().toUtf8()));
                            foldersToUpdate.insert(foldersToCreate.top());
                            foldersToCreate.pop();
                        }
                    };

                    // Sorted folders from top level to bottom level as we need to have the exact same file structure in trash
                    QVector<QString> sortedFoldersToRemove;
                    sortedFoldersToRemove.reserve(folder.foldersToRemove.size());
                    for (const auto &str : qAsConst(folder.foldersToRemove)) sortedFoldersToRemove.append(str);
                    std::sort(sortedFoldersToRemove.begin(), sortedFoldersToRemove.end(), [](const QString &a, const QString &b) -> bool { return a.size() < b.size(); });

                    // Folders to remove
                    for (auto it = sortedFoldersToRemove.begin(); it != sortedFoldersToRemove.end() && (syncingMode != Automatic || (!paused && !folder.paused));)
                    {
                        QString folderPath(folder.path);
                        folderPath.append(*it);
                        quint64 fileHash = hash64(it->toUtf8());
                        QString parentPath = QFileInfo(folderPath).path();

                        // We need to make sure that moveToTrash really moved a folder to trash as it can return true even though it failed to do so
                        QString pathInTrash;

                        QFuture<bool> future = QtConcurrent::run([&](){ return moveToTrash ? (QFile::moveToTrash(folderPath) && !pathInTrash.isEmpty()) : QDir(folderPath).removeRecursively(); });
                        while (!future.isFinished()) updateApp();

                        if (future.result() || !QDir().exists(folderPath))
                        {
                            folder.files.remove(fileHash);
                            folder.foldersToRemove.remove(hash64(it->toUtf8()));
                            it = sortedFoldersToRemove.erase(static_cast<QVector<QString>::const_iterator>(it));

                            if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                        }
                        else
                        {
                            ++it;
                        }

                        // Returns only if remembering files is enabled, as we can lose all made changes
                        if (updateApp() && rememberFilesEnabled) return;
                    }

                    // Files to remove
                    for (auto it = folder.filesToRemove.begin(); it != folder.filesToRemove.end() && (syncingMode != Automatic || (!paused && !folder.paused));)
                    {
                        QString filePath(folder.path);
                        filePath.append(*it);
                        quint64 fileHash = hash64(*it);
                        QString parentPath = QFileInfo(filePath).path();

                        // We need to make sure that moveToTrash really moved a file to trash as it can return true even though it failed to do so
                        QString pathInTrash;

                        if ((moveToTrash ? QFile::moveToTrash(filePath, &pathInTrash) && !pathInTrash.isEmpty() : QFile::remove(filePath)) || !QFileInfo::exists(filePath))
                        {
                            folder.files.remove(fileHash);
                            it = folder.filesToRemove.erase(static_cast<QHash<quint64, QByteArray>::const_iterator>(it));

                            if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                        }
                        else
                        {
                            ++it;
                        }

                        // Returns only if remembering files is enabled, as we can lose all made changes
                        if (updateApp() && rememberFilesEnabled) return;
                    }

                    // Folders to add
                    for (auto it = folder.foldersToAdd.begin(); it != folder.foldersToAdd.end() && (syncingMode != Automatic || (!paused && !folder.paused));)
                    {
                        QString folderPath(folder.path);
                        folderPath.append(*it);
                        quint64 fileHash = hash64(*it);

                        createParentFolders(QDir::cleanPath(folderPath));

                        if (QDir().mkdir(folderPath) || QFileInfo::exists(folderPath))
                        {
                            folder.files.insert(fileHash, File(*it, File::folder, QFileInfo(folderPath).lastModified()));
                            it = folder.foldersToAdd.erase(static_cast<QHash<quint64, QByteArray>::const_iterator>(it));

                            QString parentPath = QFileInfo(folderPath).path();
                            if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                        }
                        else
                        {
                            ++it;
                        }

                        // Returns only if remembering files is enabled, as we can lose all made changes
                        if (updateApp() && rememberFilesEnabled) return;
                    }

                    // Files to copy
                    for (auto it = folder.filesToAdd.begin(); it != folder.filesToAdd.end() && (syncingMode != Automatic || (!paused && !folder.paused));)
                    {
                        // Removes from the list if the source file doesn't exist
                        if (!QFileInfo::exists(it.value().second) || it.value().first.isEmpty() || it.value().second.isEmpty())
                        {
                            it = folder.filesToAdd.erase(static_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::const_iterator>(it));
                            continue;
                        }

                        QString filePath(folder.path);
                        filePath.append(it.value().first);
                        quint64 fileHash = hash64(it.value().first);
                        const File &file = folder.files.value(fileHash);

                        createParentFolders(QDir::cleanPath(filePath));

                        // Removes a file with the same filename first before copying if it exists
                        if (file.exists)
                        {
                            if (moveToTrash)
                            {
                                QFile::moveToTrash(filePath);
                            }
                            else
                            {
                                if (file.type == File::folder)
                                    QDir(filePath).removeRecursively();
                                else
                                    QFile::remove(filePath);
                            }
                        }

                        QFuture<bool> future = QtConcurrent::run([&](){ return QFile::copy(it.value().second, filePath); });
                        while (!future.isFinished()) updateApp();

                        if (future.result())
                        {
                            folder.files.insert(fileHash, File(it.value().first, File::file, QFileInfo(filePath).lastModified()));
                            it = folder.filesToAdd.erase(static_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::const_iterator>(it));

                            QString parentPath = QFileInfo(filePath).path();
                            if (QFileInfo::exists(parentPath)) foldersToUpdate.insert(parentPath);
                        }
                        else
                        {
                            // Not enough disk space notification
                            if (notificationsEnabled && shouldNotify && QStorageInfo(folder.path).bytesAvailable() < QFile(it.value().second).size())
                            {
                                if (!notificationList.contains(rootPath))
                                    notificationList.insert(rootPath, new QTimer()).value()->setSingleShot(true);

                                shouldNotify = false;
                                notificationList.value(rootPath)->start(NOTIFICATION_DELAY);
                                trayIcon->showMessage(QString("Not enough disk space on %1 (%2)").arg(QStorageInfo(folder.path).displayName(), rootPath), "", QSystemTrayIcon::Critical, 1000);
                            }

                            ++it;
                        }

                        // Returns only if remembering files is enabled, as we can lose all made changes
                        if (updateApp() && rememberFilesEnabled) return;
                    }

                    // Updates parent folders modified date as adding/removing files and folders change their modified date
                    for (auto &folderPath : foldersToUpdate)
                    {
                        quint64 folderHash = hash64(QByteArray(folderPath.toUtf8()).remove(0, folder.path.size()));
                        if (folder.files.contains(folderHash)) folder.files[folderHash].date = QFileInfo(folderPath).lastModified();
                    }
                }
            }
        }
    }

    // Optimizes memory usage
    for (int i = -1; auto &profile : profiles)
    {
        i++;
        if (profileNumber >= 0 && profileNumber != i) continue;

        for (auto &folder : profile.folders)
        {
            folder.files.squeeze();
            folder.foldersToAdd.squeeze();
            folder.filesToAdd.squeeze();
            folder.foldersToRemove.squeeze();
            folder.filesToRemove.squeeze();
        }
    }

    if (!queue.empty() && profileNumber >= 0) queue.dequeue();
    syncNowAction->setEnabled(true);
    moveToTrashAction->setEnabled(true);
    for (auto &action : syncingModeMenu->actions()) action->setEnabled(true);
    busy = false;

    updateStatus();
    updateNextSyncingTime();

    TIMESTAMP(syncTime, "Syncing is complete.");

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
    syncing = false;
    numOfFilesToSync = 0;

    // Syncing status
    for (int i = -1; auto &profile : profiles)
    {
        i++;
        profile.syncing = false;
        int existingFolders = 0;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if ((currentProfileNumber >= 0 && currentProfileNumber != i) || profile.toBeRemoved)
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

            if (busy && folder.exists && (syncingMode != Automatic || !folder.paused) && (!folder.foldersToAdd.isEmpty() || !folder.filesToAdd.isEmpty() || !folder.foldersToRemove.isEmpty() || !folder.filesToRemove.isEmpty()))
            {
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

            if (syncingMode == Automatic && profiles[i].paused)
            {
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            }
            else if (profiles[i].syncing || syncNowTriggered || queue.contains(i))
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

                if (syncingMode == Automatic && profiles[row].folders[i].paused)
                    folderModel->setData(index, iconPause, Qt::DecorationRole);
                else if (profiles[row].folders[i].syncing || syncNowTriggered || queue.contains(row))
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
        if (syncing || syncNowTriggered || !queue.empty())
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

    // Number of files left to sync
    if (busy)
    {
        for (auto &profile : profiles)
            for (auto &folder : profile.folders)
                if (folder.exists && (syncingMode == Manual || !folder.paused))
                    numOfFilesToSync += folder.foldersToAdd.size() + folder.filesToAdd.size() + folder.foldersToRemove.size() + folder.filesToRemove.size();
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

    quint64 time = 0;

    // Counts total syncing time of profiles with at least two active folders
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
    time <<= (syncTimeMultiplier - 1);
    if (time < SYNC_MIN_DELAY) time = SYNC_MIN_DELAY;

    if ((!busy && syncTimer.isActive()) || (!syncTimer.isActive() || static_cast<int>(time) < syncTimer.remainingTime()))
        syncTimer.start(time);

    int seconds = (time / 1000) % 60;
    int minutes = (time / 1000 / 60) % 60;
    int hours = (time / 1000 / 60 / 60);

    syncingTimeAction->setText(QString("Synchronize Every: %1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0')));
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

            if (syncingMode == Automatic)
            {
                if (profiles[row].paused)
                    menu.addAction(iconResume, "&Resume syncing profile", this, SLOT(pauseSelected()));
                else
                    menu.addAction(iconPause, "&Pause syncing profile", this, SLOT(pauseSelected()));
            }
            else if (syncingMode == Manual)
            {
                menu.addAction(iconSync, "&Synchronize profile", this, std::bind(&MainWindow::sync, const_cast<MainWindow *>(this), row))->setDisabled(queue.contains(row) || syncNowTriggered);
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
            if (syncingMode == Automatic)
            {
                if (profiles[ui->syncProfilesView->selectionModel()->selectedIndexes()[0].row()].folders[ui->folderListView->selectionModel()->selectedIndexes()[0].row()].paused)
                    menu.addAction(iconResume, "&Resume syncing folder", this, SLOT(pauseSelected()));
                else
                    menu.addAction(iconPause, "&Pause syncing folder", this, SLOT(pauseSelected()));
            }

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
            qsizetype filesSize;
            qsizetype foldersToAddSize;
            qsizetype filesToAddSize;
            qsizetype folderToRemoveSize;
            qsizetype filesToRemoveSize;
            QByteArray folderPath;

            stream >> folderPath;
            stream >> filesSize;

            folderPath.chop(1); // Removes a forward slash in the end of the path
            int folderIndex = folderPaths[profileIndex].indexOf(folderPath);
            bool exists = profileIndex >= 0 && folderIndex >= 0;

            for (qsizetype k = 0; k < filesSize; k++)
            {
                quint64 hash;
                QDateTime date;
                File::Type type;
                bool updated;
                bool exists;

                stream >> hash;
                stream >> date;
                stream >> type;
                stream >> updated;
                stream >> exists;

                if (exists)
                {
                    const_cast<File *>(profiles[profileIndex].folders[folderIndex].files.insert(hash, File(QByteArray(), type, date, updated, exists, true)).operator->())->path.squeeze();
                }
            }

            // Folders to add
            stream >> foldersToAddSize;

            for (qsizetype k = 0; k < foldersToAddSize; k++)
            {
                QByteArray path;
                stream >> path;

                if (exists)
                {
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToAdd.insert(hash64(path), path).operator->())->squeeze();
                }
            }

            // Files to add
            stream >> filesToAddSize;

            for (qsizetype k = 0; k < filesToAddSize; k++)
            {
                QByteArray to;
                QByteArray from;
                stream >> to;
                stream >> from;

                if (exists)
                {
                    const auto &it = profiles[profileIndex].folders[folderIndex].filesToAdd.insert(hash64(to), QPair<QByteArray, QByteArray>(to, from));
                    const_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::iterator &>(it).value().first.squeeze();
                    const_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::iterator &>(it).value().second.squeeze();
                }
            }

            // Folders to remove
            stream >> folderToRemoveSize;

            for (qsizetype k = 0; k < folderToRemoveSize; k++)
            {
                QByteArray path;
                stream >> path;

                if (exists)
                {
                    const_cast<QByteArray *>(profiles[profileIndex].folders[folderIndex].foldersToRemove.insert(hash64(path), path).operator->())->squeeze();
                }
            }

            // Files to remove
            stream >> filesToRemoveSize;

            for (qsizetype k = 0; k < filesToRemoveSize; k++)
            {
                QByteArray path;
                stream >> path;

                if (exists)
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
    if ((syncingMode == Automatic && folder.paused) || !folder.exists) return -1;

    int totalNumOfFiles = 0;

    // Resets file states
    for (auto &file : folder.files)
    {
        file.exists = false;
        file.updated = (file.onRestore) ? file.updated : false;
        file.onRestore = false;
    }

#ifndef USE_STD_FILESYSTEM
    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (syncingMode == Automatic && folder.paused) return -1;

        dir.next();

        QByteArray filePath(dir.fileInfo().filePath().toUtf8());
        filePath.remove(0, folder.path.size());
        File::Type type = dir.fileInfo().isDir() ? File::folder : File::file;
        quint64 fileHash = hash64(filePath);

        // If we have a file in our database already
        if (folder.files.contains(fileHash))
        {
            File &file = folder.files[fileHash];
            QDateTime fileDate(dir.fileInfo().lastModified());
            bool updated = file.updated;

            // Restores filepath if it was loaded from saved file data
            if (file.path.isEmpty())
            {
                file.path = filePath;
                file.path.squeeze();
            }

            // Quits if hash collision detected
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

            // Sets updated state
            if (type == File::folder)
            {
                updated = (file.date < fileDate);

                // Marks all parent folders as updated in case if the current folder was updated
                if (updated)
                {
                    QByteArray folderPath(dir.fileInfo().filePath().toUtf8());

                    while (folderPath.remove(folderPath.lastIndexOf("/"), folderPath.length()).length() > folder.path.length())
                    {
                        quint64 hash = hash64(QByteArray(folderPath).remove(0, folder.path.size()));
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
            const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, dir.fileInfo().lastModified())).operator->())->path.squeeze();
        }

        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#elif defined(USE_STD_FILESYSTEM)
#error FIX: An update required
    for (auto const &dir : std::filesystem::recursive_directory_iterator{std::filesystem::path{folder.path.toStdString()}})
    {
        if (folder.paused && syncingMode == Automatic) return;

        QString filePath(dir.path().string().c_str());
        filePath.remove(0, folder.path.size());
        filePath.replace("\\", "/");

        File::Type type = dir.is_directory() ? File::dir : File::file;
        quint64 fileHash = hash64(filePath);

        const_cast<File *>(folder.files.insert(fileHash, File(filePath, type, QDateTime(), false)).operator->())->path.squeeze();
        totalNumOfFiles++;
        if (shouldQuit || folder.toBeRemoved) return -1;
    }
#endif

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

    // Checks for added/modified files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists) continue;
            if (syncingMode == Automatic && folderIt->paused) break;

            for (QHash<quint64, File>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (syncingMode == Automatic && otherFolderIt->paused) break;

                const File &file = folderIt->files.value(otherFileIt.key());
                const File &otherFile = otherFileIt.value();
                bool newFile = (file.type == File::unknown);

                bool alreadyAdded = folderIt->filesToAdd.contains(otherFileIt.key());
                bool hasNewer = alreadyAdded && QFileInfo(folderIt->filesToAdd.value(otherFileIt.key()).second).lastModified() < otherFile.date;

                // Removes a file from to remove list if any file was updated
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
#ifdef Q_OS_LINUX
                ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated && otherFile.updated && file.date < otherFile.date)))) ||
#else
                ((file.type == File::file || file.type != otherFile.type) && file.exists && otherFile.exists && (((!file.updated && otherFile.updated) || (file.updated == otherFile.updated && file.date < otherFile.date)))) ||
#endif
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
                        const auto &it = folderIt->filesToAdd.insert(otherFileIt.key(), QPair<QByteArray, QByteArray>(otherFile.path, from));
                        const_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::iterator &>(it).value().first.squeeze();
                        const_cast<QHash<quint64, QPair<QByteArray, QByteArray>>::iterator &>(it).value().second.squeeze();
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

        for (QHash<quint64, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (syncingMode == Automatic && folderIt->paused) break;

            if (!fileIt.value().exists && !folderIt->foldersToAdd.contains(fileIt.key()) && !folderIt->filesToAdd.contains(fileIt.key()) &&
               ((fileIt.value().type == File::file && !folderIt->filesToRemove.contains(fileIt.key())) ||
               (fileIt.value().type == File::folder && !folderIt->foldersToRemove.contains(fileIt.key()))))
            {
                // Adds files from other folders for removal
                for (auto removeIt = profile.folders.begin(); removeIt != profile.folders.end(); ++removeIt)
                {
                    if (folderIt == removeIt || !removeIt->exists || (syncingMode == Automatic && removeIt->paused)) continue;

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
