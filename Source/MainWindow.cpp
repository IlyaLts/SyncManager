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
#include <QStringListModel>
#include <QSettings>
#include <QCloseEvent>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QInputDialog>
#include <QDesktopServices>
#include <QMimeData>

/*
===================
MainWindow::MainWindow
===================
*/
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    connect(&updateTimer, &QTimer::timeout, this, &MainWindow::updateStatus);

    syncThread = new QThread(this);
    syncWorker = new SyncWorker();

    syncWorker->moveToThread(syncThread);
    connect(syncThread, &QThread::started, syncWorker, [this](){ syncWorker->run(manager); });
    connect(syncWorker, &SyncWorker::finished, this, [this](){ syncDone(); });

    ui->setupUi(this);
    ui->centralWidget->setLayout(ui->mainLayout);
    setWindowTitle("Sync Manager");
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    profileModel = new DecoratedStringListModel;
    folderModel = new DecoratedStringListModel;
    ui->syncProfilesView->setModel(profileModel);
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
        manager.profiles().push_back(SyncProfile(name, profileIndexByName(name)));

        SyncProfile &profile = manager.profiles().back();
        profile.paused = manager.paused();
        profile.name = name;

        QStringList paths = profilesData.value(name).toStringList();
        paths.sort();

        for (auto &path : paths)
        {
            profile.folders.push_back(SyncFolder());
            profile.folders.back().paused = manager.paused();
            profile.folders.back().path = path.toUtf8();
        }
    }

    connect(ui->syncProfilesView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(profileClicked(QItemSelection,QItemSelection)));
    connect(ui->syncProfilesView->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)), SLOT(profileNameChanged(QModelIndex)));
    connect(ui->syncProfilesView, SIGNAL(deletePressed()), SLOT(removeProfile()));
    connect(ui->folderListView, &FolderListView::drop, this, &MainWindow::addFolder);
    connect(ui->folderListView, SIGNAL(deletePressed()), SLOT(removeFolder()));
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(&manager, &SyncManager::message, this, [this](QString title, QString message){ notify(title, message, QSystemTrayIcon::Critical); });
    connect(&manager, &SyncManager::profileSynced, this, &MainWindow::profileSynced);

    for (auto &profile : manager.profiles())
    {
        profile.syncTimer.setSingleShot(true);
        connect(&profile.syncTimer, &QChronoTimer::timeout, this, [&profile, this](){ sync(&const_cast<SyncProfile &>(profile), true); });
    }

    setupMenus();
    loadSettings();
    updateStrings();
    switchLanguage(language);

    for (auto &profile : manager.profiles())
    {
        switchDatabaseLocation(profile, profile.databaseLocation());
        updateMenuSyncTime(profile);
    }

    updateStatus();

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
    for (auto &profile : manager.profiles())
        profile.saveSettings();

    saveSettings();
    delete ui;

    if (syncThread->isRunning())
    {
        syncThread->requestInterruption();
        syncThread->quit();

        if (syncThread->isRunning())
        {
            syncThread->terminate();
            syncThread->wait();
        }
    }
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

    if (appInitiated)
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

        if (manager.busy() && !questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
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
        newName = tr(" (%1)").arg(i);
        newName.insert(0, tr("New profile"));
    }

    profileNames.append(newName);
    profileModel->setStringList(profileNames);
    folderModel->setStringList(QStringList());
    manager.profiles().push_back(SyncProfile(newName, profileIndexByName(newName)));
    manager.profiles().back().paused = manager.paused();
    manager.profiles().back().setupMenus(this);
    connectProfileMenu(manager.profiles().back());

    rebindProfiles();

    connect(&manager.profiles().back().syncTimer, &QChronoTimer::timeout, this, [this](){ sync(&manager.profiles().back(), true); });

    QSettings profileData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileData.setValue(newName, folderModel->stringList());

    // Avoids reloading a newly added profile as it's already loaded.
    ui->syncProfilesView->selectionModel()->blockSignals(true);
    ui->syncProfilesView->setCurrentIndex(ui->syncProfilesView->model()->index(ui->syncProfilesView->model()->rowCount() - 1, 0));
    ui->syncProfilesView->selectionModel()->blockSignals(false);

    ui->folderListView->selectionModel()->reset();
    ui->folderListView->update();
    updateProfileTooltip(manager.profiles().back());
    updateStatus();
    manager.updateNextSyncingTime(manager.profiles().back());
    manager.updateTimer(manager.profiles().back());
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
        SyncProfile *profile = profileByIndex(index);

        if (!profile)
            continue;

        disconnect(&profile->syncTimer, &QChronoTimer::timeout, this, nullptr);
        ui->syncProfilesView->model()->removeRow(index.row());
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

        if (!manager.busy())
            manager.profiles().remove(*profile);

        folderModel->setStringList(QStringList());
        manager.updateNextSyncingTime(*profile);
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
    SyncProfile *profile = profileByIndex(ui->syncProfilesView->currentIndex());

    if (!profile)
        return;

    for (auto &folder : profile->folders)
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
void MainWindow::profileNameChanged(const QModelIndex &index)
{
    QString newName = index.data(Qt::DisplayRole).toString();
    newName.remove('/');
    newName.remove('\\');

    QStringList profileNames;
    QStringList folderPaths;

    for (auto &profile : manager.profiles())
        profileNames.append(profile.name);

    SyncProfile *profile = profileByIndex(index);

    if (!profile)
        return;

    for (auto &folder : profile->folders)
        folderPaths.append(folder.path);

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(profile->name, Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(index, profile->name, Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(profile->name + QLatin1String("_profile"));
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("Paused"), profile->paused);
    settings.setValue(newName + QLatin1String("_profile/") + QLatin1String("LastSyncDate"), profile->lastSyncDate);

    for (auto &folder : profile->folders)
        settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);

    for (auto &folder : profile->folders)
        settings.setValue(newName + QLatin1String("_profile/") + folder.path + QLatin1String("_LastSyncDate"), folder.lastSyncDate);

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
    QStringList folders;

    if (!profile)
        return;

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
        QString title(tr("Browse For Folder"));
        QString folderPath = QFileDialog::getExistingDirectory(this, title, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (folderPath.isEmpty())
            return;

        folders.append(folderPath);
    }

    QStringList folderPaths;

    for (auto &folder : profile->folders)
        if (!folder.toBeRemoved)
            folderPaths.append(folder.path);

    // Checks if we already have a folder for synchronization in the list
    for (const auto &folderName : folders)
    {
        bool exists = false;

        for (const auto &path : folderPaths)
        {
            int n = path.size() > folderName.size() ? path.size() - 1 : folderName.size() - 1;

            if (SyncFolder *folder = profile->folderByPath(folderName))
            {
                if ((folder->caseSensitive && path.toStdString().compare(0, n, folderName.toStdString()) == 0) ||
                    (!folder->caseSensitive && path.toLower().toStdString().compare(0, n, folderName.toLower().toStdString()) == 0))
                {
                    exists = true;
                }
            }
        }

        if (!exists)
        {
            profile->folders.push_back(SyncFolder());
            profile->folders.back().paused = manager.paused();
            profile->folders.back().path = folderName.toUtf8();
            profile->folders.back().path.append("/");
            folderPaths.append(profile->folders.back().path);

            QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
            profilesData.setValue(profile->name, folderPaths);

            folderModel->setStringList(folderPaths);
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

        if (folder->syncing)
        {
            QString title(tr("Remove folder"));
            QString text(tr("The folder is currently syncing. Are you sure you want to remove it?"));

            if (!questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
                return;
        }

        folder->paused = true;
        folder->toBeRemoved = true;
        ui->folderListView->model()->removeRow(folderIndex.row());

        folder->removeDatabase();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile->name + QLatin1String("_profile/") + folder->path + QLatin1String("_Paused"));

        if (!manager.busy())
            profile->folders.remove(*folder);

        QStringList foldersPaths;

        for (auto &folder : profile->folders)
            foldersPaths.append(folder.path);

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profile->name, foldersPaths);

        manager.updateTimer(*profile);
        manager.updateNextSyncingTime(*profile);
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
    manager.setPaused(!manager.paused());

    for (auto &profile : manager.profiles())
    {
        profile.paused = manager.paused();

        for (auto &folder : profile.folders)
            folder.paused = manager.paused();

        if (profile.paused)
            profile.syncTimer.stop();
        else
            manager.updateTimer(profile);

        if (appInitiated)
            profile.saveSettings();
    }

    updateStatus();

    if (appInitiated)
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
            QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
            SyncProfile *profile = profileByIndex(profileIndex);

            if (!profile)
                return;

            for (auto &index : ui->folderListView->selectionModel()->selectedIndexes())
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

            if (appInitiated)
                profile->saveSettings();
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
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
                    manager.updateTimer(*profile);

                if (appInitiated)
                    profile->saveSettings();
            }
        }

        updateStatus();
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

    if ((!manager.busy() && questionBox(QMessageBox::Question, title, text, QMessageBox::No, this)) ||
        (manager.busy() && questionBox(QMessageBox::Warning, title, syncText, QMessageBox::No, this)))
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
        for (auto &profile : manager.profiles())
        {
            manager.updateNextSyncingTime(profile);
            manager.updateTimer(profile);
        }
    }

    updateStatus();

    if (appInitiated)
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

    if (appInitiated && mode == SyncProfile::DeletePermanently && mode != profile.deletionMode())
    {
        QString title(tr("Switch deletion mode to delete files permanently?"));
        QString text(tr("Are you sure? Beware: this could lead to data loss!"));

        if (!questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
            mode = profile.deletionMode();
    }

    profile.setDeletionMode(mode);
    profile.moveToTrashAction->setChecked(mode == SyncProfile::MoveToTrash);
    profile.versioningAction->setChecked(mode == SyncProfile::Versioning);
    profile.deletePermanentlyAction->setChecked(mode == SyncProfile::DeletePermanently);
    profile.versioningFormatMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);
    profile.versioningLocationMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);

    if (appInitiated)
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

    if (appInitiated)
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

    profile.locallyNextToFolderAction->setChecked(location== SyncProfile::LocallyNextToFolder);
    profile.customLocationAction->setChecked(location == SyncProfile::CustomLocation);
    profile.setVersioningLocation(location);

    if (location == SyncProfile::CustomLocation && appInitiated)
    {
        QString title(tr("Browse for Versioning Folder"));
        QString path = QFileDialog::getExistingDirectory(this, title, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (!path.isEmpty())
        {
            profile.setVersioningPath(path);
            profile.customLocationPathAction->setText(tr("Custom Location: ") + path);
        }
    }

    if (appInitiated)
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

    if (appInitiated)
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

    profile.saveDatabaseLocallyAction->setChecked(location == false);
    profile.saveDatabaseDecentralizedAction->setChecked(location == true);
    profile.setDatabaseLocation(location);

    if (appInitiated)
        profile.saveSettings();
}

/*
===================
MainWindow::switchPriority
===================
*/
void MainWindow::switchPriority(QThread::Priority priority)
{
    idlePriorityAction->setChecked(priority == QThread::IdlePriority);
    lowestPriorityAction->setChecked(priority == QThread::LowestPriority);
    lowPriorityAction->setChecked(priority == QThread::LowPriority);
    normalPriorityAction->setChecked(priority == QThread::NormalPriority);
    highPriorityAction->setChecked(priority == QThread::HighPriority);
    highestPriorityAction->setChecked(priority == QThread::HighestPriority);
    timeCriticalPriorityAction->setChecked(priority == QThread::TimeCriticalPriority);

    if (syncThread->isRunning())
        syncThread->setPriority(priority);
}

/*
===================
MainWindow::increaseSyncTime
===================
*/
void MainWindow::increaseSyncTime(SyncProfile &profile)
{
    quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

    for (auto &profile : manager.profiles())
    {
        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
        {
            profile.increaseSyncTimeAction->setEnabled(false);
            return;
        }
    }

    manager.setSyncTimeMultiplier(profile, profile.syncTimeMultiplier() + 1);
    profile.decreaseSyncTimeAction->setEnabled(true);
    updateMenuSyncTime(profile);

    for (auto &profile : manager.profiles())
    {
        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
            profile.increaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateProfileTooltip(profile);
    }

    if (appInitiated)
        profile.saveSettings();
}

/*
===================
MainWindow::decreaseSyncTime
===================
*/
void MainWindow::decreaseSyncTime(SyncProfile &profile)
{
    manager.setSyncTimeMultiplier(profile, profile.syncTimeMultiplier() - 1);
    updateMenuSyncTime(profile);

    for (auto &profile : manager.profiles())
    {
        quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

        // If exceeds the maximum value of an qint64
        if (profile.syncEvery < max)
            profile.increaseSyncTimeAction->setEnabled(true);

        if (profile.syncTimeMultiplier() <= 1)
            profile.decreaseSyncTimeAction->setEnabled(false);

        manager.updateTimer(profile);
        updateProfileTooltip(profile);
    }

    if (appInitiated)
        profile.saveSettings();
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
    updateStrings();

    if (appInitiated)
        saveSettings();

    for (auto &profile : manager.profiles())
    {
        profile.updateStrings();
        updateMenuSyncTime(profile);
    }
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

    if (appInitiated)
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

    if (appInitiated)
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

    if (appInitiated)
        saveSettings();
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
    int usage;

    if (!intInputDialog(this, title, text, usage, manager.maxCpuUsage, 1, 100))
        return;

    manager.maxCpuUsage = usage;
    maximumCpuUsageAction->setText("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(manager.maxCpuUsage));
    saveSettings();
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

    if (!intInputDialog(this, title, text, size, profile.syncIntervalFixed() / 1000, 0))
        return;

    profile.setSyncIntervalFixed(size * 1000);
    updateMenuSyncTime(profile);

    if (appInitiated)
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

    if (!textInputDialog(this, title, text, postfix, postfix))
        return;

    profile.setVersioningFolder(postfix);
    profile.versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(profile.versioningFolder()));

    if (appInitiated)
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

    if (!textInputDialog(this, title, text, pattern, pattern))
        return;

    profile.setVersioningPattern(pattern);
    profile.versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(profile.versioningPattern()));

    if (appInitiated)
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

    if (!intInputDialog(this, title, text, size, profile.fileMinSize(), 0))
        return;

    if (size && profile.fileMaxSize() && static_cast<quint64>(size) > profile.fileMaxSize())
        size = profile.fileMaxSize();

    profile.setFileMinSize(size);
    profile.fileMinSizeAction->setText("&" + tr("Minimum File Size: %1 bytes").arg(profile.fileMinSize()));

    if (appInitiated)
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

    if (!intInputDialog(this, title, text, size, profile.fileMaxSize(), 0))
        return;

    if (size && static_cast<quint64>(size) < profile.fileMinSize())
        size = profile.fileMinSize();

    profile.setFileMaxSize(size);
    profile.fileMaxSizeAction->setText("&" + tr("Maximum File Size: %1 bytes").arg(profile.fileMaxSize()));

    if (appInitiated)
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

    if (!intInputDialog(this, title, text, size, profile.movedFileMinSize(), 0))
        return;

    profile.setMovedFileMinSize(size);
    profile.movedFileMinSizeAction->setText("&" + tr("Minimum Size for a Moved File: %1 bytes").arg(profile.movedFileMinSize()));

    if (appInitiated)
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

    if (!textInputDialog(this, title, text, includeString, includeString))
        return;

    QStringList includeList = includeString.split(";");

    for (auto &include : includeList)
        include = include.trimmed();

    includeString = includeList.join("; ");
    profile.setIncludeList(includeList);
    profile.includeAction->setText("&" + tr("Include: %1").arg(includeString));

    if (appInitiated)
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

    if (!textInputDialog(this, title, text, excludeString, excludeString))
        return;

    QStringList excludeList = excludeString.split(";");

    for (auto &exclude : excludeList)
        exclude = exclude.trimmed();

    excludeString = excludeList.join("; ");
    profile.setExcludeList(excludeList);
    profile.excludeAction->setText("&" + tr("Exclude: %1").arg(excludeString));

    if (appInitiated)
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

    if (appInitiated)
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

    if (appInitiated)
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
                action->setDisabled(manager.queue().contains(profile));
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

    if (!manager.busy())
    {
        animSync.start();

        if (!syncThread->isRunning())
        {
            syncThread->start();
            syncThread->setPriority(QThread::LowestPriority);
            updateTimer.start(UPDATE_DELAY);
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
    syncThread->quit();
    updateTimer.stop();
    animSync.stop();

    updateStatus();
    manager.purgeRemovedProfiles();
}

/*
===================
MainWindow::profileSynced
===================
*/
void MainWindow::profileSynced(SyncProfile *profile)
{
    manager.updateTimer(*profile);
    updateMenuSyncTime(*profile);
    updateProfileTooltip(*profile);
    saveSettings();
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
        for (auto &profile : manager.profiles())
        {
            if (profileModel->index(i).data(Qt::DisplayRole).toString()  == profile.name)
                profile.index = profileModel->index(i);
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
    connect(profile.saveDatabaseLocallyAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Locally); });
    connect(profile.saveDatabaseDecentralizedAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Decentralized); });
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
    connect(profile.saveDatabaseLocallyAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Locally); });
    connect(profile.saveDatabaseDecentralizedAction, &QAction::triggered, this, [this, &profile](){ switchDatabaseLocation(profile, SyncProfile::Decentralized); });
    connect(profile.fileMinSizeAction, &QAction::triggered, this, [this, &profile](){ setFileMinSize(profile); });
    connect(profile.fileMaxSizeAction, &QAction::triggered, this, [this, &profile](){ setFileMaxSize(profile); });
    connect(profile.movedFileMinSizeAction, &QAction::triggered, this, [this, &profile](){ setMovedFileMinSize(profile); });
    connect(profile.includeAction, &QAction::triggered, this, [this, &profile](){ setIncludeList(profile); });
    connect(profile.excludeAction, &QAction::triggered, this, [this, &profile](){ setExcludeList(profile); });
    connect(profile.ignoreHiddenFilesAction, &QAction::triggered, this, [this, &profile](){ toggleIgnoreHiddenFiles(profile); });
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
        for (size_t i = 0; i < manager.profiles().size(); i++)
        {
            QModelIndex index = profileModel->index(i);
            SyncProfile *profile = profileByIndex(index);

            if (!profile)
                continue;

            if (profile->toBeRemoved)
                continue;

            if (profile->paused)
                profileModel->setData(index, iconPause, Qt::DecorationRole);
            else if (profile->syncing || (!profile->syncHidden && manager.queue().contains(profile)))
                profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
            else if (!profile->hasExistingFolders() && !profile->folders.empty())
                profileModel->setData(index, iconRemove, Qt::DecorationRole);
            else if (profile->hasMissingFolders())
                profileModel->setData(index, iconWarning, Qt::DecorationRole);
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
                    QModelIndex index = folderModel->index(i);
                    SyncFolder *folder = profile->folderByIndex(index);

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

                    folderModel->setData(index, *icon, SyncTypeRole);

                    if (folder->paused)
                        folderModel->setData(index, iconPause, Qt::DecorationRole);
                    else if (folder->syncing || (manager.queue().contains(profile) && !manager.syncing() && !profile->syncHidden))
                        folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
                    else if (!folder->exists)
                        folderModel->setData(index, iconRemove, Qt::DecorationRole);
                    else
                        folderModel->setData(index, iconDone, Qt::DecorationRole);

                    ui->folderListView->update(index);
                }
            }
        }
    }

    bool paused = manager.paused();

    // Pause status
    for (const auto &profile : manager.profiles())
    {
        if (profile.toBeRemoved)
            continue;

        if (manager.paused())
            paused = true;

        if (!manager.paused())
        {
            paused = false;
            break;
        }
    }

    manager.setPaused(paused);

    // Tray & Icon
    if (manager.inPausedState())
    {
        trayIcon->setIcon(trayIconPause);
        setWindowIcon(trayIconPause);

        // Fixes flickering menu bar
        if (pauseSyncingAction->icon().cacheKey() != iconResume.cacheKey())
            pauseSyncingAction->setIcon(iconResume);

        pauseSyncingAction->setText("&" + tr("Resume Syncing"));
    }
    else
    {
        if (manager.syncing() || manager.hasManualSyncProfile())
        {
            if (trayIcon->icon().cacheKey() != trayIconSync.cacheKey())
                trayIcon->setIcon(trayIconSync);

            if (windowIcon().cacheKey() != trayIconSync.cacheKey())
                setWindowIcon(trayIconSync);
        }
        else if (manager.warning())
        {
            if (manager.issue())
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

        pauseSyncingAction->setText("&" + tr("Pause Syncing"));
    }

    // Title
    if (manager.filesToSync())
    {
        trayIcon->setToolTip(tr("Sync Manager - %1 files to synchronize").arg(manager.filesToSync()));
        setWindowTitle(tr("Sync Manager - %1 files to synchronize").arg(manager.filesToSync()));
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
void MainWindow::updateMenuSyncTime(SyncProfile &profile)
{
    if (profile.syncingMode() == SyncProfile::Manual)
        return;

    quint64 syncEvery = 0;
    QString text(tr("Synchronize Every"));
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

    quint64 seconds = (syncEvery / 1000) % 60;
    quint64 minutes = (syncEvery / 1000 / 60) % 60;
    quint64 hours = (syncEvery / 1000 / 60 / 60) % 24;
    quint64 days = (syncEvery / 1000 / 60 / 60 / 24);

    text.append(": ");

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
        QString text = tr("Last synchronization: %1.").arg(time) + nextSyncText;
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
                folderModel->setData(folderModel->index(i), tr("The folder is currently unavailable."), Qt::ToolTipRole);
            }
            else if (!folder.lastSyncDate.isNull())
            {
                QString time(syncApp->toLocalizedDateTime(folder.lastSyncDate, dateFormat));
                QString text = QString("Last synchronization: %1.").arg(time) + nextSyncText;
                folderModel->setData(folderModel->index(i), text, Qt::ToolTipRole);
            }
            else
            {
                folderModel->setData(folderModel->index(i), tr("Haven't been synchronized yet."), Qt::ToolTipRole);
            }

            i++;
        }
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

    manager.maxCpuUsage = static_cast<quint32>(settings.value("MaximumCpuUsage", 100).toInt());
    switchPriority(static_cast<QThread::Priority>(settings.value("Priority", QThread::NormalPriority).toInt()));
    language = static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt());
    showInTray = settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool();
    manager.enableNotifications(QSystemTrayIcon::supportsMessages() && settings.value("Notifications", true).toBool());

    for (int i = 0; i < profileModel->rowCount(); i++)
    {
        QModelIndex index = profileModel->index(i);
        QString profileKeyPath(index.data(Qt::DisplayRole).toString() + QLatin1String("_profile/"));
        SyncProfile *profile = profileByIndex(index);

        if (!profile)
            continue;

        // Loads saved pause states and checks if synchronization folders exist
        profile->paused = settings.value(profileKeyPath + QLatin1String("Paused"), false).toBool();

        if (!profile->paused)
            manager.setPaused(false);

        // Loads last sync dates for all profiles
        profile->lastSyncDate = settings.value(profileKeyPath + QLatin1String("LastSyncDate")).toDateTime();
        profile->syncTime = settings.value(profile->name + QLatin1String("_profile/") + QLatin1String("SyncTime"), 0).toULongLong();

        for (auto &folder : profile->folders)
        {
            folder.paused = settings.value(profileKeyPath + folder.path + QLatin1String("_Paused"), false).toBool();
            folder.syncType = static_cast<SyncFolder::SyncType>(settings.value(profileKeyPath + folder.path + QLatin1String("_SyncType"), SyncFolder::TWO_WAY).toInt());

            if (!folder.paused)
                manager.setPaused(false);

            folder.exists = QFileInfo::exists(folder.path);
            folder.lastSyncDate = settings.value(profileKeyPath + folder.path + QLatin1String("_LastSyncDate")).toDateTime();
        }
    }

    showInTrayAction->setChecked(showInTray);
    disableNotificationAction->setChecked(!manager.notificationsEnabled());

    for (auto &profile : manager.profiles())
    {
        profile.databaseLocationMenu->setEnabled(profile.databaseLocation());
        profile.ignoreHiddenFilesAction->setChecked(profile.ignoreHiddenFiles());
        profile.detectMovedFilesAction->setChecked(profile.detectMovedFiles());

        QString profileKeyname(profile.name + QLatin1String("_profile/"));

        switchSyncingMode(profile, static_cast<SyncProfile::SyncingMode>(settings.value(profileKeyname + "SyncingMode", SyncProfile::AutomaticAdaptive).toInt()));
        switchDeletionMode(profile, static_cast<SyncProfile::DeletionMode>(settings.value(profileKeyname + "DeletionMode", SyncProfile::MoveToTrash).toInt()));
        switchVersioningFormat(profile, static_cast<SyncProfile::VersioningFormat>(settings.value(profileKeyname + "VersioningFormat", SyncProfile::FolderTimestamp).toInt()));
        switchVersioningLocation(profile, static_cast<SyncProfile::VersioningLocation>(settings.value(profileKeyname + "VersioningLocation", SyncProfile::LocallyNextToFolder).toInt()));

        manager.updateNextSyncingTime(profile);
    }

    for (auto &profile : manager.profiles())
    {
        quint64 max = std::numeric_limits<qint64>::max() - QDateTime::currentDateTime().toMSecsSinceEpoch();

        // If exceeds the maximum value of an qint64
        if (profile.syncEvery >= max)
            profile.increaseSyncTimeAction->setEnabled(false);

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
    settings.setValue("MaximumCpuUsage", manager.maxCpuUsage);
    settings.setValue("Priority", priority);
    settings.setValue("Language", language);
    settings.setValue("ShowInTray", showInTray);
    settings.setValue("Paused", manager.paused());
    settings.setValue("Notifications", manager.notificationsEnabled());
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
    iconTwoWay.addFile(":/Images/IconTwoWay.png");
    iconOneWay.addFile(":/Images/IconOneWay.png");
    iconOneWayUpdate.addFile(":/Images/IconOneWayUpdate.png");
    trayIconDone.addFile(":/Images/TrayIconDone.png");
    trayIconIssue.addFile(":/Images/TrayIconIssue.png");
    trayIconPause.addFile(":/Images/TrayIconPause.png");
    trayIconSync.addFile(":/Images/TrayIconSync.png");
    trayIconWarning.addFile(":/Images/TrayIconWarning.png");
    animSync.setFileName(":/Images/AnimSync.gif");

    syncNowAction = new QAction(iconSync, "&" + tr("Sync Now"), this);
    pauseSyncingAction = new QAction(iconPause, "&" + tr("Pause Syncing"), this);
    maximumCpuUsageAction = new QAction("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(manager.maxCpuUsage), this);
    idlePriorityAction = new QAction("&" + tr("Idle Priority"), this);
    lowestPriorityAction = new QAction("&" + tr("Lowest Priority"), this);
    lowPriorityAction = new QAction("&" + tr("Low Priority"), this);
    normalPriorityAction = new QAction("&" + tr("Normal Priority"), this);
    highPriorityAction = new QAction("&" + tr("High Priority"), this);
    highestPriorityAction = new QAction("&" + tr("Highest P0riority"), this);
    timeCriticalPriorityAction = new QAction("&" + tr("Time Critical Priority"), this);

    for (int i = 0; i < Application::languageCount(); i++)
    {
        languageActions.append(new QAction(tr(languages[i].name), this));
        languageActions[i]->setIcon(*(new QIcon(languages[i].flagPath)));
    }

    launchOnStartupAction = new QAction("&" + tr("Launch on Startup"), this);
    showInTrayAction = new QAction("&" + tr("Show in System Tray"));
    disableNotificationAction = new QAction("&" + tr("Disable Notifications"), this);
    showAction = new QAction("&" + tr("Show"), this);
    quitAction = new QAction("&" + tr("Quit"), this);
    reportBugAction = new QAction("&" + tr("Report a Bug"), this);
    versionAction = new QAction(tr("Version: %1").arg(SYNCMANAGER_VERSION), this);

    versionAction->setDisabled(true);

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setCheckable(true);

    idlePriorityAction->setCheckable(true);
    lowestPriorityAction->setCheckable(true);
    lowPriorityAction->setCheckable(true);
    normalPriorityAction->setCheckable(true);
    highPriorityAction->setCheckable(true);
    highestPriorityAction->setCheckable(true);
    timeCriticalPriorityAction->setCheckable(true);
    launchOnStartupAction->setCheckable(true);
    showInTrayAction->setCheckable(true);
    disableNotificationAction->setCheckable(true);

    updateLaunchOnStartupState();

    languageMenu = new UnhidableMenu("&" + tr("Language"), this);

    for (int i = 0; i < Application::languageCount(); i++)
        languageMenu->addAction(languageActions[i]);

    priorityMenu = new UnhidableMenu("&" + tr("Priority"), this);
    priorityMenu->addAction(idlePriorityAction);
    priorityMenu->addAction(lowestPriorityAction);
    priorityMenu->addAction(lowPriorityAction);
    priorityMenu->addAction(normalPriorityAction);
    priorityMenu->addAction(highPriorityAction);
    priorityMenu->addAction(highestPriorityAction);
    priorityMenu->addAction(timeCriticalPriorityAction);

    performanceMenu = new UnhidableMenu("&" + tr("Performance"), this);
    performanceMenu->addAction(maximumCpuUsageAction);
    performanceMenu->addMenu(priorityMenu);

#ifndef Q_OS_WIN
    priorityMenu->setVisible(false);
#endif

    settingsMenu = new UnhidableMenu("&" + tr("Settings"), this);
    settingsMenu->setIcon(iconSettings);
    settingsMenu->addMenu(performanceMenu);
    settingsMenu->addMenu(languageMenu);
    settingsMenu->addAction(launchOnStartupAction);
    settingsMenu->addAction(showInTrayAction);
    settingsMenu->addAction(disableNotificationAction);
    settingsMenu->addSeparator();
    settingsMenu->addAction(reportBugAction);
    settingsMenu->addAction(versionAction);

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

    for (auto &profile : manager.profiles())
    {
        profile.loadSettings();
        profile.setupMenus(this);
    }

    connect(syncNowAction, &QAction::triggered, this, [this](){ sync(nullptr); });
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(maximumCpuUsageAction, SIGNAL(triggered()), this, SLOT(setMaximumCpuUsage()));
    connect(idlePriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::IdlePriority); });
    connect(lowestPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::LowestPriority); });
    connect(lowPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::LowPriority); });
    connect(normalPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::NormalPriority); });
    connect(highPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::HighPriority); });
    connect(highestPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::HighestPriority); });
    connect(timeCriticalPriorityAction, &QAction::triggered, this, [this](){ switchPriority(QThread::TimeCriticalPriority); });

    for (int i = 0; i < Application::languageCount(); i++)
        connect(languageActions[i], &QAction::triggered, this, [i, this](){ switchLanguage(languages[i].language); });

    connect(launchOnStartupAction, &QAction::triggered, this, &MainWindow::toggleLaunchOnStartup);
    connect(showInTrayAction, &QAction::triggered, this, &MainWindow::toggleShowInTray);
    connect(disableNotificationAction, &QAction::triggered, this, &MainWindow::toggleNotification);
    connect(showAction, &QAction::triggered, this, [this](){ trayIconActivated(QSystemTrayIcon::DoubleClick); });
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(reportBugAction, &QAction::triggered, this, [this](){ QDesktopServices::openUrl(QUrl(BUG_TRACKER_LINK)); });

    for (auto &profile : manager.profiles())
        connectProfileMenu(profile);
}

/*
===================
MainWindow::updateStrings
===================
*/
void MainWindow::updateStrings()
{
    syncNowAction->setText("&" + tr("Sync Now"));
    pauseSyncingAction->setText("&" + tr("Pause Syncing"));
    maximumCpuUsageAction->setText("&" + tr("Maximum CPU Usage") + QString(": %1%").arg(manager.maxCpuUsage));

    for (int i = 0; i < Application::languageCount(); i++)
        languageActions[i]->setText(tr(languages[i].name));

    launchOnStartupAction->setText("&" + tr("Launch on Startup"));
    showInTrayAction->setText("&" + tr("Show in System Tray"));
    disableNotificationAction->setText("&" + tr("Disable Notifications"));
    showAction->setText("&" + tr("Show"));
    quitAction->setText("&" + tr("Quit"));
    reportBugAction->setText("&" + tr("Report a Bug"));
    versionAction->setText(tr("Version: %1").arg(SYNCMANAGER_VERSION));

    performanceMenu->setTitle("&" + tr("Performance"));
    languageMenu->setTitle("&" + tr("Language"));
    settingsMenu->setTitle("&" + tr("Settings"));

    syncNowAction->setToolTip("&" + tr("Sync Now"));
    pauseSyncingAction->setToolTip("&" + tr("Pause Syncing"));
    ui->SyncLabel->setText(tr("Synchronization profiles:"));
    ui->foldersLabel->setText(tr("Folders to synchronize:"));

    updateStatus();

    for (auto &profile : manager.profiles())
    {
        updateMenuSyncTime(profile);
        profile.updateStrings();
        updateProfileTooltip(profile);
    }
}

/*
===================
MainWindow::updateLaunchOnStartupState
===================
*/
void MainWindow::updateLaunchOnStartupState()
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
MainWindow::profileByIndex
===================
*/
SyncProfile *MainWindow::profileByIndex(const QModelIndex &index)
{
    for (auto &profile : manager.profiles())
        if (profile.index == index)
            return &profile;

    return nullptr;
}

/*
===================
MainWindow::profileIndex
===================
*/
QModelIndex MainWindow::profileIndex(const SyncProfile &profile)
{
    for (int i = 0; i < profileModel->rowCount(); i++)
        if (profileModel->index(i) == profile.index)
            return profileModel->index(i);

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
        if (profileModel->index(i).data(Qt::DisplayRole).toString()  == name)
            return profileModel->index(i);

    return QModelIndex();
}
