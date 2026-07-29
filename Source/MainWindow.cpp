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

#include "Application.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "DecoratedStringListModel.h"
#include "UnhidableMenu.h"
#include "FolderListView.h"
#include "MenuProxyStyle.h"
#include "Common.h"
#include "ProfileMenu.h"
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

    profileModel = new DecoratedStringListModel(this);
    folderModel = new DecoratedStringListModel(this);
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
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.beginGroup("Profiles");
    QStringList profileNames = settings.childKeys();

    // Deprecated profile data location
    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Profiles.ini", QSettings::IniFormat);
    bool oldProfileLocation = false;

    if (profileNames.isEmpty())
    {
        oldProfileLocation = true;
        profileNames = profilesData.allKeys();
    }
    else
    {
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Profiles.ini");
    }

    profileNames.sort();
    profileModel->setStringList(profileNames);

    for (auto &name : profileNames)
    {
        syncApp->manager()->profiles().emplace_back(name, ui->syncProfilesView->profileIndexByName(name));
        SyncProfile &profile = syncApp->manager()->profiles().back();
        profile.setPaused(syncApp->manager()->paused());

        QStringList paths;

        if (oldProfileLocation)
            paths = profilesData.value(name).toStringList();
        else
            paths = settings.value(name).toStringList();

        paths.sort();

        for (auto &path : paths)
            profile.folders().emplace_back(&profile, path.toUtf8());

        connect(&profile, &SyncProfile::syncingModeChanged, this, &MainWindow::updateStatus);
        connect(&profile, &SyncProfile::syncingTimeChanged, this, [this, &profile](){ updateProfileTooltip(profile);});
    }

    settings.endGroup();

    connect(ui->syncProfilesView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::profileClicked);
    connect(ui->syncProfilesView->model(), &QAbstractItemModel::dataChanged, this, &MainWindow::profileNameChanged);
    connect(ui->syncProfilesView, &RemovableListView::deletePressed, this, &MainWindow::removeProfile);
    connect(ui->folderListView, &FolderListView::drop, this, &MainWindow::addFolder);
    connect(ui->folderListView, &RemovableListView::deletePressed, this, &MainWindow::removeFolder);
    connect(ui->syncProfilesView, &RemovableListView::customContextMenuRequested, this, &MainWindow::showProfileContextMenu);
    connect(ui->folderListView, &FolderListView::customContextMenuRequested, this, &MainWindow::showFolderContextMenu);
    connect(syncApp->manager(), &SyncManager::profileSynced, this, &MainWindow::profileSynced);

    setupMenus();
    loadSettings();
    retranslate();

    for (auto &profile : syncApp->manager()->profiles())
    {
        connect(&profile.syncTimer(), &QChronoTimer::timeout, this, [&profile, this](){ sync(&const_cast<SyncProfile &>(profile), true); });

        ProfileMenu *menu = profileMenus.value(&profile);
        menu->switchDatabaseLocation(profile.databaseLocation());
        menu->updateSyncTime();
    }

    updateStatus();
    menuBar->updateStates();

    for (const auto &profile : syncApp->manager()->profiles())
        for (const auto &folder : profile.folders())
            if (!folder.exists())
                syncApp->tray()->notify(tr("Couldn't find folder"), folder.path(), QSystemTrayIcon::Warning);
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
    ui->SyncLabel->setText(tr("Synchronization profiles:"));
    ui->foldersLabel->setText(tr("Folders to synchronize:"));

    menuBar->retranslate();
    syncApp->tray()->retranslate();
    updateStatus();

    for (auto &profile : syncApp->manager()->profiles())
    {
        ProfileMenu *menu = profileMenus.value(&profile);
        menu->retranslate();
        menu->updateSyncTime();
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

    syncApp->setMaxCpuUsage(settings.value("MaximumCpuUsage", 100).toUInt());
    syncApp->setLanguage(static_cast<QLocale::Language>(settings.value("Language", QLocale::system().language()).toInt()));
    syncApp->setTrayVisible(settings.value("ShowInTray", QSystemTrayIcon::isSystemTrayAvailable()).toBool());
    syncApp->setCheckForUpdates(settings.value("CheckForUpdates", true).toBool());

    for (int i = 0; i < profileModel->rowCount(); i++)
    {
        QModelIndex index = profileModel->indexByRow(i);
        QString profileKeyPath(index.data(Qt::DisplayRole).toString() + QLatin1String("_profile/"));
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(index);

        if (!profile)
            continue;

        QString profileKeyname(profile->name() + QLatin1String("_profile/"));

        ProfileMenu *menu = profileMenus.value(profile);

        menu->switchSyncingMode(static_cast<SyncProfile::SyncingMode>(settings.value(profileKeyname + "SyncingMode", SyncProfile::AutomaticAdaptive).toInt()));
        menu->switchDeletionMode(static_cast<SyncProfile::DeletionMode>(settings.value(profileKeyname + "DeletionMode", SyncProfile::MoveToTrash).toInt()));
        menu->switchVersioningFormat(static_cast<SyncProfile::VersioningFormat>(settings.value(profileKeyname + "VersioningFormat", SyncProfile::FolderTimestamp).toInt()));
        menu->switchVersioningLocation(static_cast<SyncProfile::VersioningLocation>(settings.value(profileKeyname + "VersioningLocation", SyncProfile::LocallyNextToFolder).toInt()));
        menu->switchDatabaseLocation(static_cast<SyncProfile::DatabaseLocation>(settings.value(profileKeyname + "DatabaseLocation", SyncProfile::Decentralized).toInt()));

        // Loads saved pause states and checks if synchronization folders exist
        profile->setPaused(settings.value(profileKeyPath + QLatin1String("Paused"), false).toBool());

        if (!profile->paused())
            syncApp->manager()->setPaused(false);

        for (auto &folder : profile->folders())
        {
            folder.loadSettings();

            if (!folder.paused())
                syncApp->manager()->setPaused(false);
        }

        updateProfileTooltip(*profile);
    }

    menuBar->updateStates();
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
MainWindow::removeProfileMenu
===================
*/
void MainWindow::removeProfileMenu(SyncProfile *profile)
{
    ProfileMenu *menu = profileMenus.take(profile);

    if (menu)
        menu->deleteLater();
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
        profileNames.append(profile.name());

    for (int i = 2; profileNames.contains(newName); i++)
    {
        newName = tr(" (%1)").arg(i);
        newName.insert(0, tr("New profile"));
    }

    profileNames.append(newName);
    profileModel->setStringList(profileNames);
    folderModel->setStringList(QStringList());
    SyncProfile &profile = syncApp->manager()->profiles().emplace_back(newName, ui->syncProfilesView->profileIndexByName(newName));
    profile.setPaused(syncApp->manager()->paused());
    rebindProfiles();

    profileMenus.insert(&profile, new ProfileMenu(this, &profile));

    connect(&profile, &SyncProfile::syncingModeChanged, this, &MainWindow::updateStatus);
    connect(&profile, &SyncProfile::syncingTimeChanged, this, [this, &profile](){ updateProfileTooltip(profile);});

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.beginGroup("Profiles");
    settings.setValue(newName, folderModel->stringList());
    settings.endGroup();

    // Avoids reloading a newly added profile as it's already loaded.
    ui->syncProfilesView->selectionModel()->blockSignals(true);
    ui->syncProfilesView->setCurrentIndex(ui->syncProfilesView->model()->index(ui->syncProfilesView->model()->rowCount() - 1, 0));
    ui->syncProfilesView->selectionModel()->blockSignals(false);

    ui->folderListView->selectionModel()->reset();
    ui->folderListView->update();
    updateProfileTooltip(profile);
    updateStatus();
    profile.updateNextSyncingTime();
    profile.updateTimer();
    profile.saveSettings();
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
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(index);

        if (!profile)
            continue;

        QString title(tr("Remove profile"));
        QString text;
        bool removeDatabase = false;

        if (profile->syncing())
            text.assign(tr("The profile is currently syncing. Are you sure you want to remove it?"));
        else
            text.assign(tr("Are you sure you want to remove the profile?"));

        if (!syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            continue;

        title = tr("Remove databases");
        text = tr("Do you want to remove databases?");

        if (syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            removeDatabase = true;

        ui->syncProfilesView->model()->removeRow(index.row());
        rebindProfiles();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile->name() + QLatin1String("_profile"));
        settings.beginGroup("Profiles");
        settings.remove(profile->name());
        settings.endGroup();

        if (removeDatabase)
            for (auto &folder : profile->folders())
                folder.removeDatabase();

        profile->remove();
        ProfileMenu *menu = profileMenus.take(profile);

        if (menu)
            menu->deleteLater();

        if (!syncApp->manager()->busy())
            syncApp->manager()->profiles().remove(*profile);

        folderModel->setStringList(QStringList());
        profile->updateNextSyncingTime();
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
    SyncProfile *profile = ui->syncProfilesView->profileByIndex(ui->syncProfilesView->currentIndex());

    if (!profile)
        return;

    for (const auto &folder : profile->folders())
        folderPaths.append(folder.path());

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
        profileNames.append(profile.name());

    SyncProfile *profile = ui->syncProfilesView->profileByIndex(topLeft);

    if (!profile)
        return;

    if (newName == profile->name())
        return;

    for (const auto &folder : profile->folders())
        folderPaths.append(folder.path());

    // Sets its name back to original if there's the profile name that already exists
    if (newName.compare(profile->name(), Qt::CaseInsensitive) && (newName.isEmpty() || profileNames.contains(newName, Qt::CaseInsensitive)))
    {
        ui->syncProfilesView->model()->setData(topLeft, profile->name(), Qt::DisplayRole);
        return;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString oldProfilePrefix = profile->name() + QLatin1String("_profile");
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

    settings.beginGroup("Profiles");
    settings.remove(profile->name());
    settings.setValue(newName, folderPaths);
    settings.endGroup();

    profile->setName(newName);
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
    SyncProfile *profile = ui->syncProfilesView->profileByIndex(index);
    QStringList existedFolders;
    QStringList foldersToAdd;

    if (!profile)
        return;

    for (const auto &folder : profile->folders())
        if (!folder.toBeRemoved())
            existedFolders.append(folder.path());

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
                if (existedFolderPath.compare(newFolderPath, folder->caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
                {
                    exists = true;
                    break;
                }
            }
        }

        if (!exists)
        {
            SyncFolder &folder = profile->folders().emplace_back(profile, newFolderPath.toUtf8());
            folder.saveSettings();
            existedFolders.append(folder.path());

            QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
            settings.beginGroup("Profiles");
            settings.setValue(profile->name(), existedFolders);
            settings.endGroup();

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
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(profileIndex);
        SyncFolder *folder = profile->folderByIndex(folderIndex);

        if (!profile || !folder)
            continue;

        QString title(tr("Remove folder"));
        QString text;
        bool removeDatabase = false;

        if (folder->syncing())
            text.assign(tr("The folder is currently syncing. Are you sure you want to remove it?"));
        else
            text.assign(tr("Are you sure you want to remove the folder?"));

        if (!syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            return;

        title = tr("Remove database");
        text = tr("Do you want to remove database?");

        if (syncApp->questionBox(QMessageBox::Question, title, text, QMessageBox::Yes, this))
            removeDatabase = true;

        folder->setPaused(true);
        folder->remove();
        ui->folderListView->model()->removeRow(folderIndex.row());

        if (removeDatabase)
            folder->removeDatabase();

        folder->removeSettings();

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profile->name() + QLatin1String("_profile/") + folder->path() + QLatin1String("_Paused"));

        if (!syncApp->manager()->busy())
            profile->folders().remove(*folder);

        QStringList foldersPaths;

        for (const auto &folder : profile->folders())
            foldersPaths.append(folder.path());

        settings.beginGroup("Profiles");
        settings.setValue(profile->name(), foldersPaths);
        settings.endGroup();

        profile->updateTimer();
        profile->updateNextSyncingTime();
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
        profile.setPaused(syncApp->manager()->paused());

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
            SyncProfile *profile = ui->syncProfilesView->profileByIndex(profileIndex);

            if (!profile)
                return;

            const QModelIndexList selectedIndexes = ui->folderListView->selectionModel()->selectedIndexes();

            for (const auto &index : selectedIndexes)
            {
                SyncFolder *folder = profile->folderByIndex(index);

                if (!folder)
                    continue;

                folder->setPaused(!folder->paused());
            }

            if (syncApp->initiated())
                profile->saveSettings();
        }
        // Profiles are selected
        else if (ui->syncProfilesView->hasFocus())
        {
            const QModelIndexList selectedIndexes = ui->syncProfilesView->selectionModel()->selectedIndexes();

            for (const auto &index : selectedIndexes)
            {
                SyncProfile *profile = ui->syncProfilesView->profileByIndex(index);

                if (!profile)
                    return;

                profile->setPaused(!profile->paused());

                if (syncApp->initiated())
                    profile->saveSettings();
            }
        }

        updateStatus();
    }
}

/*
===================
MainWindow::switchSyncingType
===================
*/
void MainWindow::switchSyncingType(SyncFolder &folder, SyncFolder::Type type)
{
    folder.setType(type);
    updateStatus();

    if (syncApp->initiated())
        folder.profile().saveSettings();
}

/*
===================
MainWindow::showProfileContextMenu
===================
*/
void MainWindow::showProfileContextMenu(const QPoint &pos)
{
    static QMenu menu;
    menu.clear();

    menu.addAction(iconAdd, "&" + tr("Add a new profile"), this, &MainWindow::addProfile);

    if (!ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
    {
        QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(profileIndex);

        if (!profile)
            return;

        if (profile->paused())
        {
            menu.addAction(iconResume, "&" + tr("Resume syncing profile"), this, &MainWindow::pauseSelected);
        }
        else
        {
            menu.addAction(iconPause, "&" + tr("Pause syncing profile"), this, &MainWindow::pauseSelected);

            QAction *action = menu.addAction(iconSync, "&" + tr("Synchronize profile"), this, [=, this](){ sync(profile, false); });
            action->setDisabled(syncApp->manager()->queue().contains(profile));
        }

        menu.addAction(iconRemove, "&" + tr("Remove profile"), this, &MainWindow::removeProfile);

        menu.addSeparator();
        profileMenus.value(profile)->exportMenu(&menu);
    }

    menu.popup(ui->syncProfilesView->mapToGlobal(pos));

}

/*
===================
MainWindow::showFolderContextMenu
===================
*/
void MainWindow::showFolderContextMenu(const QPoint &pos)
{
    static QMenu menu;
    menu.clear();

    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        return;

    menu.addAction(iconAdd, "&" + tr("Add a new folder"), this, [this]() { addFolder(); });

    if (!ui->folderListView->selectionModel()->selectedIndexes().isEmpty())
    {
        QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedIndexes()[0];
        QModelIndex folderIndex = ui->folderListView->selectionModel()->selectedIndexes()[0];
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(profileIndex);
        SyncFolder *folder = profile->folderByIndex(folderIndex);

        if (!profile || !folder)
            return;

        if (folder->paused())
            menu.addAction(iconResume, "&" + tr("Resume syncing folder"), this, &MainWindow::pauseSelected);
        else
            menu.addAction(iconPause, "&" + tr("Pause syncing folder"), this, &MainWindow::pauseSelected);

        menu.addAction(iconRemove, "&" + tr("Remove folder"), this, &MainWindow::removeFolder);

        if (folder->partiallySynchronized() && !folder->unsyncedList().isEmpty())
        {
            QString menuTitle(tr("Show unsynchronized files"));
            QString title(tr("Couldn't synchronize the following files"));

            menu.addSeparator();
            menu.addAction(iconWarning, "&" + menuTitle, this, [title, folder](){ syncApp->textDialog(title, folder->unsyncedList()); });
        }

        menu.addSeparator();

        if (folder->type() != SyncFolder::TWO_WAY)
            menu.addAction(iconTwoWay, "&" + tr("Switch to two-way synchronization"), this, [profile, folder, this](){ switchSyncingType(*folder, SyncFolder::TWO_WAY); });

        if (folder->type() != SyncFolder::ONE_WAY)
            menu.addAction(iconOneWay, "&" + tr("Switch to one-way synchronization"), this, [profile, folder, this](){ switchSyncingType(*folder, SyncFolder::ONE_WAY); });

        if (folder->type() != SyncFolder::ONE_WAY_UPDATE)
            menu.addAction(iconOneWayUpdate, "&" + tr("Switch to one-way update synchronization"), this, [profile, folder, this](){ switchSyncingType(*folder, SyncFolder::ONE_WAY_UPDATE); });
    }

    menu.popup(ui->folderListView->mapToGlobal(pos));
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

        profile->setSyncHidden(hidden);
    }
    else
    {
        for (auto &profile : syncApp->manager()->profiles())
            profile.setSyncHidden(false);
    }

    syncApp->manager()->addToQueue(profile);

    if (!syncApp->manager()->busy())
    {
        animSync.start();

        if (!syncApp->syncThread()->isRunning())
        {
            syncApp->syncThread()->start();
            updateTimer.start(UpdateTime);
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
    profile->updateTimer();
    profileMenus.value(profile)->updateSyncTime();
    updateProfileTooltip(*profile);
    syncApp->saveSettings();
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
            if (profileModel->indexByRow(i).data(Qt::DisplayRole).toString()  == profile.name())
                profile.setIndex(profileModel->indexByRow(i));
        }
    }
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    syncApp->manager()->updateStatus();
    menuBar->updateSyncState();

    if (isVisible())
    {
        updateProfilesStatus();
        updateFoldersStatus();
    }

    updatePauseState();
    updateIcons();
    updateWindowTitle();
}

/*
===================
MainWindow::updateProfilesStatus
===================
*/
void MainWindow::updateProfilesStatus()
{
    SyncManager *manager = syncApp->manager();

    for (size_t i = 0; i < manager->profiles().size(); i++)
    {
        QModelIndex index = profileModel->indexByRow(i);
        SyncProfile *profile = ui->syncProfilesView->profileByIndex(index);

        if (!profile)
            continue;

        if (profile->toBeRemoved())
            continue;

        int posInQueue = manager->queue().indexOf(profile);

        if (posInQueue == 0)
            profileModel->setData(index, tr("Syncing"), QueueStatusRole);
        else if (posInQueue > 0)
            profileModel->setData(index, tr("In queue") + QString(" (%1)").arg(posInQueue), QueueStatusRole);
        else
            profileModel->setData(index, QString(""), QueueStatusRole);

        if (profile->paused())
            profileModel->setData(index, iconPause, Qt::DecorationRole);
        else if (profile->syncing() || (!profile->syncHidden() && manager->queue().contains(profile)))
            profileModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
        else if (profile->hasInsufficientFolders())
            profileModel->setData(index, iconRemove, Qt::DecorationRole);
        else if (profile->hasMissingFolders())
            profileModel->setData(index, iconWarning, Qt::DecorationRole);
        else if (profile->partiallySynchronized())
            profileModel->setData(index, iconDonePartial, Qt::DecorationRole);
        else
            profileModel->setData(index, iconDone, Qt::DecorationRole);

        ui->syncProfilesView->update(index);
    }
}

/*
===================
MainWindow::updateFoldersStatus
===================
*/
void MainWindow::updateFoldersStatus()
{
    SyncManager *manager = syncApp->manager();

    if (ui->syncProfilesView->selectionModel()->selectedIndexes().isEmpty())
        return;

    QModelIndex profileIndex = ui->syncProfilesView->selectionModel()->selectedRows()[0];
    SyncProfile *profile = ui->syncProfilesView->profileByIndex(profileIndex);

    if (!profile)
        return;

    for (int i = 0; i < folderModel->rowCount(); i++)
    {
        QModelIndex index = folderModel->indexByRow(i);
        const SyncFolder *folder = profile->folderByIndex(index);

        if (!folder)
            continue;

        if (folder->toBeRemoved())
            continue;

        QIcon *icon = nullptr;

        if (folder->type() == SyncFolder::TWO_WAY)
            icon = &iconTwoWay;
        else if (folder->type() == SyncFolder::ONE_WAY)
            icon = &iconOneWay;
        else if (folder->type() == SyncFolder::ONE_WAY_UPDATE)
            icon = &iconOneWayUpdate;

        if (icon)
            folderModel->setData(index, *icon, SyncTypeRole);

        if (folder->paused())
            folderModel->setData(index, iconPause, Qt::DecorationRole);
        else if (folder->syncing() || (manager->queue().contains(profile) && !manager->syncing() && !profile->syncHidden()))
            folderModel->setData(index, QIcon(animSync.currentPixmap()), Qt::DecorationRole);
        else if (!folder->exists())
            folderModel->setData(index, iconRemove, Qt::DecorationRole);
        else if (folder->partiallySynchronized())
            folderModel->setData(index, iconDonePartial, Qt::DecorationRole);
        else
            folderModel->setData(index, iconDone, Qt::DecorationRole);

        ui->folderListView->update(index);
    }
}

/*
===================
MainWindow::updatePauseState
===================
*/
void MainWindow::updatePauseState()
{
    SyncManager *manager = syncApp->manager();
    bool paused = manager->paused();

    for (const auto &profile : manager->profiles())
    {
        if (profile.toBeRemoved())
            continue;

        if (manager->paused())
            paused = true;

        if (!manager->paused())
        {
            paused = false;
            break;
        }
    }

    manager->setPaused(paused);
}

/*
===================
MainWindow::updateIcons
===================
*/
void MainWindow::updateIcons()
{
    SystemTray *tray = syncApp->tray();
    SyncManager *manager = syncApp->manager();

    if (manager->inPausedState())
    {
        tray->setIcon(tray->iconPause());
        setWindowIcon(tray->icon());
        menuBar->togglePauseButton(true);
        manager->setPaused(true);
    }
    else
    {
        if (manager->syncing() || manager->hasManualSyncProfile())
        {
            tray->setIcon(tray->iconSync());
            setWindowIcon(tray->iconSync());
        }
        else if (manager->issue())
        {
            tray->setIcon(tray->iconIssue());
            setWindowIcon(tray->iconIssue());
        }
        else if (manager->warning())
        {
            tray->setIcon(tray->iconWarning());
            setWindowIcon(tray->iconWarning());
        }
        else
        {
            bool incomplete = false;

            for (auto &profile : manager->profiles())
                if (profile.partiallySynchronized())
                    incomplete = true;

            if (incomplete)
            {
                tray->setIcon(tray->iconDonePartial());
                setWindowIcon(tray->iconDonePartial());
            }
            else
            {
                tray->setIcon(tray->iconDone());
                setWindowIcon(tray->iconDone());
            }
        }

        menuBar->togglePauseButton(false);
        manager->setPaused(false);
    }
}

/*
===================
MainWindow::updateWindowTitle
===================
*/
void MainWindow::updateWindowTitle()
{
    SyncManager *manager = syncApp->manager();

    if (manager->filesToSync())
    {
        syncApp->tray()->setToolTip(tr("Sync Manager - %1 files to synchronize").arg(manager->filesToSync()));
        setWindowTitle(tr("Sync Manager - %1 files to synchronize").arg(manager->filesToSync()));
    }
    else
    {
        syncApp->tray()->setToolTip("Sync Manager");
        setWindowTitle("Sync Manager");
    }
}

/*
===================
MainWindow::updateProfileTooltip
===================
*/
void MainWindow::updateProfileTooltip(const SyncProfile &profile)
{
    QModelIndex index = ui->syncProfilesView->indexByProfile(profile);
    QString nextSyncText;
    QString dateFormat("dddd, MMMM d, yyyy h:mm:ss AP");

    if (!index.isValid())
        return;

    if (profile.syncingMode() != SyncProfile::Manual)
    {
        nextSyncText.append("\n" + tr("Next Synchronization: "));
        QDateTime dateTime = profile.lastSyncDate();
        dateTime += std::chrono::duration<quint64, std::milli>(profile.syncEvery());
        nextSyncText.append(syncApp->toLocalizedDateTime(dateTime, dateFormat));
        nextSyncText.append(".");
    }

    if (!profile.countExistingFolders())
    {
        profileModel->setData(index, tr("The profile has no folders available."), Qt::ToolTipRole);
    }
    else if (!profile.lastSyncDate().isNull())
    {
        QString time(syncApp->toLocalizedDateTime(profile.lastSyncDate(), dateFormat));
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
        for (int i = 0; auto &folder : profile.folders())
        {
            if (!folder.exists())
            {
                folderModel->setData(folderModel->indexByRow(i), tr("The folder is currently unavailable."), Qt::ToolTipRole);
            }
            else if (!folder.lastSyncDate().isNull())
            {
                QString time(syncApp->toLocalizedDateTime(folder.lastSyncDate(), dateFormat));
                QString text = tr("Last synchronization: %1.").arg(time) + nextSyncText;

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
    iconSync.addFile(":/Images/IconSync.png");
    iconWarning.addFile(":/Images/IconWarning.png");
    iconTwoWay.addFile(":/Images/IconTwoWay.png");
    iconOneWay.addFile(":/Images/IconOneWay.png");
    iconOneWayUpdate.addFile(":/Images/IconOneWayUpdate.png");
    animSync.setFileName(":/Images/AnimSync.gif");

    menuBar = new MenuBar(this);
    setMenuBar(menuBar);
    menuBar->exportMenus(*syncApp->tray());
    MenuProxyStyle *style = new MenuProxyStyle;
    style->setParent(menuBar);
    menuBar->setStyle(style);

#ifndef Q_OS_WIN
    QString styleSheet;

    // Fixes the issue of overly small menu buttons in the menu bar
    styleSheet.append("QMenuBar { padding: 5px 0px 5px 0px; } QMenuBar::item { padding: 5px 10px 5px 10px; border: none; }");

    // Fixes a disappearing icon when you click on its menu on Linux while using the Fusion style
    styleSheet.append("QMenuBar::item:selected { background: #e3e3e3; } QMenuBar::item:pressed { background: #e3e3e3; }");

    m_menuBar->setStyleSheet(styleSheet);

    // Makes profiles/folders items slightly bigger
    ui->syncProfilesView->setStyleSheet("QListView::item { padding: 3px;}");
    ui->folderListView->setStyleSheet("QListView::item { padding: 3px; }");
#endif

    for (auto &profile : syncApp->manager()->profiles())
        profileMenus.insert(&profile, new ProfileMenu(this, &profile));

    connect(menuBar, &MenuBar::syncNowTriggered, this, [this](){ sync(nullptr); });
    connect(menuBar, &MenuBar::pauseTriggered, this, &MainWindow::pauseSyncing);
}
