/*
===============================================================================
    Copyright (C) 2022 Ilya Lyakhovets
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
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QDirIterator>
#include <QTimer>
#include <QStack>

#ifdef DEBUG_TIMESTAMP
#include <chrono>
#endif

#ifdef USE_STD_FILESYSTEM
#include <filesystem>
#endif

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
    resize(QSize(settings.value("Width", 800).toInt(), settings.value("Height", 400).toInt()));
    setWindowState(settings.value("Fullscreen", false).toBool() ? Qt::WindowMaximized : Qt::WindowActive);

    QList<int> hSizes;
    QVariantList hList = settings.value("HorizontalSplitter", QVariantList({ui->syncProfilesLayout->minimumSize().width(), 999999})).value<QVariantList>();
    for (auto &variant : hList) hSizes.append(variant.toInt());
    ui->horizontalSplitter->setSizes(hSizes);
    ui->horizontalSplitter->setStretchFactor(0, 0);
    ui->horizontalSplitter->setStretchFactor(1, 1);

    profileModel = new DecoratedStringListModel;
    folderModel = new DecoratedStringListModel;
    ui->syncProfilesView->setModel(profileModel);
    ui->folderListView->setModel(folderModel);

    iconAdd.addFile(":/Images/IconAdd.png");
    iconDone.addFile(":/Images/IconDone.png");
    iconPause.addFile(":/Images/IconPause.png");
    iconRemove.addFile(":/Images/IconRemove.png");
    iconResume.addFile(":/Images/IconResume.png");
    iconSync.addFile(":/Images/IconSync.png");
    iconWarning.addFile(":/Images/IconWarning.png");
    trayIconDone.addFile(":/Images/TrayIconDone.png");
    trayIconIssue.addFile(":/Images/TrayIconIssue.png");
    trayIconPause.addFile(":/Images/TrayIconPause.png");
    trayIconSync.addFile(":/Images/TrayIconSync.png");
    trayIconWarning.addFile(":/Images/TrayIconWarning.png");

    syncNowAction = new QAction(iconSync, "&Sync Now", this);
    pauseSyncingAction = new QAction(iconPause, "&Pause Syncing", this);
    automaticAction = new QAction("&Automatic", this);
    manualAction = new QAction("&Manual", this);
    quitAction = new QAction("&Quit", this);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);

    syncingModeMenu = new QMenu("&Syncing Mode", this);
    syncingModeMenu->addAction(automaticAction);
    syncingModeMenu->addAction(manualAction);

    trayIconMenu = new QMenu(this);
    trayIconMenu->addAction(syncNowAction);
    trayIconMenu->addAction(pauseSyncingAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addMenu(syncingModeMenu);
    trayIconMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip("Sync Manager");
    trayIcon->setIcon(trayIconDone);
    trayIcon->show();

    // Loads synchronization list
    QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
    profileNames = profilesData.allKeys();
    profileModel->setStringList(profileNames);

    for (auto &name : profileNames)
    {
        profiles.append(Profile());
        foldersPath.append(profilesData.value(name).toStringList());

        for (auto &path : foldersPath.last())
        {
            profiles.last().folders.append(Folder());
            profiles.last().folders.last().path = path;
        }
    }

    connect(ui->syncProfilesView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(profileClicked(QItemSelection,QItemSelection)));
    connect(ui->syncProfilesView->model(), SIGNAL(dataChanged(QModelIndex,QModelIndex,QList<int>)), SLOT(profileNameChanged(QModelIndex)));
    connect(ui->syncProfilesView, SIGNAL(deletePressed()), SLOT(removeProfile()));
    connect(syncNowAction, SIGNAL(triggered()), this, SLOT(syncNow()));
    connect(pauseSyncingAction, SIGNAL(triggered()), this, SLOT(pauseSyncing()));
    connect(automaticAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Automatic));
    connect(manualAction, &QAction::triggered, this, std::bind(&MainWindow::switchSyncingMode, this, Manual));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(&syncTimer, SIGNAL(timeout()), this, SLOT(sync()));
    connect(ui->syncProfilesView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    connect(ui->folderListView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));

    bool notifications = settings.value("Notifications", true).toBool();
    paused = true;

    // Loads saved pause states for profiles/folers
    for (int i = 0; i < profiles.size(); i++)
    {
        profiles[i].paused = settings.value(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), false).toBool();
        if (!profiles[i].paused) paused = false;

        for (auto &folder : profiles[i].folders)
        {
            folder.paused = settings.value(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), false).toBool();
            if (!folder.paused) paused = false;
            folder.exists = QFileInfo::exists(folder.path);

            if (notifications && !folder.exists)
                trayIcon->showMessage("Broken profile folder", QString("Couldn't find %1 folder").arg(folder.path), QSystemTrayIcon::Warning, 1000);
        }
    }

    switchSyncingMode(static_cast<SyncingMode>(settings.value("SyncingMode", Automatic).toInt()));
    updateStatus();
    syncTimer.setSingleShot(true);
    syncTimer.start(0);
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

    settings.setValue("HorizontalSplitter", hSizes);
    settings.setValue("Fullscreen", isMaximized());

    if (!isMaximized())
    {
        settings.setValue("Width", size().width());
        settings.setValue("Height", size().height());
    }

    settings.setValue("Notifications", true);
    settings.setValue("SyncingMode", syncingMode);

    // Saves profiles/folders pause states
    for (int i = 0; i < profiles.size(); i++)
    {
        if (!profiles[i].toBeRemoved) settings.setValue(profileNames[i] + QLatin1String("_profile/") + QLatin1String("Paused"), profiles[i].paused);

        for (auto &folder : profiles[i].folders)
                if (!folder.toBeRemoved) settings.setValue(profileNames[i] + QLatin1String("_profile/") + folder.path + QLatin1String("_Paused"), folder.paused);
    }

    delete ui;
}

/*
===================
MainWindow::closeEvent
===================
*/
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Hides the window instead of closing as it can appear out of the screen after disconnecting a screen.
    hide();
    event->ignore();
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

    profiles.append(Profile());
    profileNames.append(newName);
    foldersPath.append(QStringList());
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

    for (auto &index : ui->syncProfilesView->selectionModel()->selectedIndexes())
    {
        ui->syncProfilesView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profileNames[index.row()] + QLatin1String("_profile"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.remove(profileNames[index.row()]);

        profiles[index.row()].paused = true;
        profiles[index.row()].toBeRemoved = true;
        for (auto &folder : profiles[index.row()].folders) folder.paused = true;
        profileNames.removeAt(index.row());
        foldersPath.removeAt(index.row());
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

    folderModel->setStringList(foldersPath[ui->syncProfilesView->currentIndex().row()]);
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

    // Sets its name back to original if there's the project name that already exists
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
    profilesData.setValue(newName, foldersPath[row]);

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

    QString filename = QFileDialog::getExistingDirectory(this, "Browse For Folder", QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);
    if (!filename.isEmpty() && !foldersPath[row].contains(filename))
    {
        foldersPath[row].append(filename);
        profiles[row].folders.append(Folder());
        profiles[row].folders.last().path = filename;

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profileNames[row], foldersPath[row]);

        folderModel->setStringList(foldersPath[row]);
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
        foldersPath[profileRow].removeAt(index.row());
        ui->folderListView->model()->removeRow(index.row());

        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        settings.remove(profileNames[profileRow] + QLatin1String("_profile/") + profiles[profileRow].folders[index.row()].path + QLatin1String("_Paused"));

        QSettings profilesData(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat);
        profilesData.setValue(profileNames[profileRow], foldersPath[profileRow]);
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

            for (auto &folder : profiles[profileRow].folders)
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
    shouldQuit = true;
    qApp->quit();
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

        // Fixes wrong window position after hiding the window
#ifdef Q_OS_LINUX
        if (isHidden()) move(pos().x() + (frameSize().width() - size().width()), pos().y() + (frameSize().height() - size().height()));
#endif

        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        show();
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

    switch (mode)
    {
    case Automatic:
    {
        pauseSyncingAction->setVisible(true);
        automaticAction->setChecked(true);
        updateNextSyncingTime();
        break;
    }
    case Manual:
    {
        pauseSyncingAction->setVisible(false);
        manualAction->setChecked(true);
        syncTimer.stop();
        break;
    }
    }

    updateStatus();
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

    busy = true;
    syncing = false;
    syncNowAction->setEnabled(false);
    for (auto &action : syncingModeMenu->actions()) action->setEnabled(false);

#ifdef DEBUG_TIMESTAMP
    auto startTime = std::chrono::high_resolution_clock::now();
    int numOfFoldersToAdd = 0;
    int numOfFilesToAdd = 0;
    int numOfFilesToRemove = 0;
#endif

    if (!paused || syncNowTriggered)
    {
        // Checks for changes
        for (int i = -1; auto &profile : profiles)
        {
            i++;
            if (profileNumber >= 0 && profileNumber != i) continue;

            int activeFolders = 0;

            for (auto &folder : profile.folders)
            {
                folder.exists = QFileInfo::exists(folder.path);
                if ((!folder.paused || syncingMode != Automatic) && folder.exists) activeFolders++;
            }

            if ((profile.paused && syncingMode == Automatic) || activeFolders < 2) continue;

            for (auto &folder : profile.folders)
                GetListOfFiles(folder);

            checkForChanges(profile);

#ifdef DEBUG_TIMESTAMP
            for (auto &folder : profile.folders)
            {
                numOfFoldersToAdd += folder.foldersToAdd.size();
                numOfFilesToAdd += folder.filesToAdd.size();
                numOfFilesToRemove += folder.filesToRemove.size();
            }
#endif
        }

        syncNowTriggered = false;

#ifdef DEBUG_TIMESTAMP
        qDebug("Folders to add: %d, Files to add: %d, Files/Folders to remove: %d", numOfFoldersToAdd, numOfFilesToAdd, numOfFilesToRemove);
#endif

        updateStatus();
        QApplication::processEvents();

        if (syncing)
        {
            // Synchronizes files/folders
            for (auto &profile : profiles)
            {
                for (auto &folder : profile.folders)
                {                    
                    QSet<QString> foldersToUpdate;

                    auto createParentFolders = [&](QString path)
                    {
                        QStack<QString> createFolders;

                        while ((path = QFileInfo(path).path()).length() > folder.path.length())
                        {
                            if (QDir(path).exists()) break;
                            createFolders.append(path);
                        }

                        while (!createFolders.isEmpty())
                        {
                            QDir().mkdir(createFolders.top());
                            QString shortPath(createFolders.top());
                            shortPath.remove(0, folder.path.size());
                            folder.files.insert(qHash(shortPath), File(shortPath, File::dir, QFileInfo(createFolders.top()).lastModified()));
                            folder.foldersToAdd.remove(createFolders.top());
                            foldersToUpdate.insert(createFolders.top());
                            createFolders.pop();
                        }
                    };

                    // Folders to add
                    for (auto it = folder.foldersToAdd.begin(); it != folder.foldersToAdd.end() && !paused && (!folder.paused || syncingMode != Automatic);)
                    {
                        QString folderPath(folder.path);
                        folderPath.append(*it);
                        size_t fileHash = qHash(*it);

                        createParentFolders(QDir::cleanPath(folderPath));

                        if (QDir().mkdir(folderPath) || QFileInfo::exists(folderPath))
                        {
                            folder.files.insert(fileHash, File(*it, File::dir, QFileInfo(folderPath).lastModified()));
                            it = folder.foldersToAdd.erase(static_cast<QSet<QString>::const_iterator>(it));

                            QString path = QFileInfo(folderPath).path();
                            if (QFileInfo::exists(path)) foldersToUpdate.insert(path);
                        }
                        else
                        {
                            ++it;
                        }

                        if (updateAppIfNeeded()) return;
                    }

                    // Files to copy
                    for (auto it = folder.filesToAdd.begin(); it != folder.filesToAdd.end() && !paused && (!folder.paused || syncingMode != Automatic);)
                    {
                        // Removes from the list if the source file doesn't exist
                        if (!QFileInfo::exists(it.value()))
                        {
                            it = folder.filesToAdd.erase(static_cast<QMap<QString, QString>::const_iterator>(it));
                            continue;
                        }

                        QString filePath(folder.path);
                        filePath.append(it.key());
                        size_t fileHash = qHash(it.key());

                        createParentFolders(QDir::cleanPath(filePath));

                        // Removes a file with the same filename first before copying if it exists
                        if (folder.files.value(fileHash).exists)
                            QFile::remove(filePath);

                        if (QFile::copy(it.value(), filePath))
                        {
                            folder.files.insert(fileHash, File(it.key(), File::file, QFileInfo(filePath).lastModified()));
                            it = folder.filesToAdd.erase(static_cast<QMap<QString, QString>::const_iterator>(it));

                            QString path = QFileInfo(filePath).path();
                            if (QFileInfo::exists(path)) foldersToUpdate.insert(path);
                        }
                        else
                        {
                            ++it;
                        }

                        if (updateAppIfNeeded()) return;
                    }

                    // Files/folders to remove
                    for (auto it = folder.filesToRemove.begin(); it != folder.filesToRemove.end() && !paused && (!folder.paused || syncingMode != Automatic);)
                    {
                        QString filename(folder.path);
                        filename.append(*it);
                        size_t fileHash = qHash(*it);

                        QString path = QFileInfo(filename).path();

                        if ((QFileInfo(filename).isDir() ? QDir(filename).removeRecursively() : QFile::remove(filename)) || !QFileInfo::exists(filename))
                        {
                            folder.files.remove(fileHash);
                            it = folder.filesToRemove.erase(static_cast<QSet<QString>::const_iterator>(it));

                            if (QFileInfo::exists(path)) foldersToUpdate.insert(path);
                        }
                        else
                        {
                            ++it;
                        }

                        if (updateAppIfNeeded()) return;
                    }

                    // Updates parent folders modified date as adding/removing files and folders change their modified date
                    for (auto &folderPath : foldersToUpdate)
                    {
                        size_t folderHash = qHash(QString(folderPath).remove(0, folder.path.size()));
                        if (folder.files.contains(folderHash)) folder.files[folderHash].date = QFileInfo(folderPath).lastModified();
                    }
                }
            }
        }
    }

    // Removes profiles/folders if they were removed from lists
    for (auto profileIt = profiles.begin(); profileIt != profiles.end();)
    {
        // Profiles
        if (profileIt->toBeRemoved)
        {
            profileIt = profiles.erase(static_cast<QList<Profile>::const_iterator>(profileIt));
            continue;
        }

        // Folders
        for (auto folderIt = profileIt->folders.begin(); folderIt != profileIt->folders.end();)
        {
            if (folderIt->toBeRemoved)
                folderIt = profileIt->folders.erase(static_cast<QList<Folder>::const_iterator>(folderIt));
            else
                folderIt++;
        }

        profileIt++;
    }

    if (!queue.empty()) queue.dequeue();
    busy = false;
    syncNowAction->setEnabled(true);
    for (auto &action : syncingModeMenu->actions()) action->setEnabled(true);
    updateStatus();
    updateNextSyncingTime();

    if (!queue.empty()) sync(queue.head());

#ifdef DEBUG_TIMESTAMP
    std::chrono::high_resolution_clock::time_point launchTime(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(launchTime.time_since_epoch());
    qDebug("%lld ms - Sync complete time.", ml.count());
#endif
}

/*
===================
MainWindow::updateStatus
===================
*/
void MainWindow::updateStatus()
{
    bool isThereIssue = false;
    syncing = false;

    // Syncing statuses
    for (auto &profile : profiles)
    {
        profile.syncing = false;

        for (auto &folder : profile.folders)
        {
            folder.syncing = false;

            if (busy && folder.exists && !folder.paused && (!folder.foldersToAdd.isEmpty() || !folder.filesToAdd.isEmpty() || !folder.filesToRemove.isEmpty()))
            {
                syncing = true;
                profile.syncing = true;
                folder.syncing = true;
            }
        }
    }

    // Profile list
    for (int i = 0, j = 0; i < profiles.size(); i++)
    {
        if (profiles[i].toBeRemoved) continue;

        QModelIndex index = profileModel->index(j, 0);

        if (profiles[i].paused && syncingMode == Automatic)
        {
            profileModel->setData(index, iconPause, Qt::DecorationRole);
        }
        else if (profiles[i].syncing || syncNowTriggered || queue.contains(i))
        {
            profileModel->setData(index, iconSync, Qt::DecorationRole);
        }
        else
        {
            profileModel->setData(index, iconDone, Qt::DecorationRole);

            // Shows a warning icon if at least one folder doesn't exist
            for (auto &folder : profiles[i].folders)
            {
                if (!folder.exists)
                {
                    isThereIssue = true;
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

            if (profiles[row].folders[i].paused && syncingMode == Automatic)
                folderModel->setData(index, iconPause, Qt::DecorationRole);
            else if (profiles[row].folders[i].syncing || syncNowTriggered || queue.contains(row))
                folderModel->setData(index, iconSync, Qt::DecorationRole);
            else if (!profiles[row].folders[i].exists)
                folderModel->setData(index, iconRemove, Qt::DecorationRole);
            else
                folderModel->setData(index, iconDone, Qt::DecorationRole);

            ui->folderListView->update(index);
            j++;
        }
    }

    // Pause status
    paused = profiles.isEmpty() ? false : true;

    for (auto &profile : profiles)
        if (!profile.paused || profile.toBeRemoved)
            paused = false;

    // Tray & Icon
    if (paused && syncingMode == Automatic)
    {
        trayIcon->setIcon(trayIconPause);
        setWindowIcon(trayIconPause);
        pauseSyncingAction->setIcon(iconResume);
        pauseSyncingAction->setText("&Resume Syncing");
    }
    else
    {
        if (syncing || syncNowTriggered || !queue.empty())
        {
            trayIcon->setIcon(trayIconSync);
            setWindowIcon(trayIconSync);
        }
        else if (isThereIssue)
        {
            trayIcon->setIcon(trayIconWarning);
            setWindowIcon(trayIconWarning);
        }
        else
        {
            trayIcon->setIcon(trayIconDone);
            setWindowIcon(trayIconDone);
        }

        pauseSyncingAction->setIcon(iconPause);
        pauseSyncingAction->setText("&Pause Syncing");
    }

    // Number of files to sync left
    numOfFilesToSync = 0;

    if (busy)
    {
        for (auto &profile : profiles)
            for (auto &folder : profile.folders)
                if (folder.exists && (!folder.paused || syncingMode == Manual))
                    numOfFilesToSync += folder.filesToAdd.size() + folder.filesToRemove.size() + folder.foldersToAdd.size();
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

    int numOfFiles = 0;

    for (auto &profile : profiles)
        for (auto &folder : profile.folders)
            numOfFiles += folder.files.size();

    if (!busy && numOfFiles < syncTimer.remainingTime())
        syncTimer.start(numOfFiles < 1000 ? 1000 : numOfFiles);
}

/*
===================
MainWindow::updateNextSyncingTime
===================
*/
void MainWindow::updateNextSyncingTime()
{
    if (busy || syncingMode == Manual) return;

    int numOfActiveFiles = 0;

    // Counts the current number of active files in not paused and existing folders
    for (auto &profile : profiles)
    {
        if (profile.paused) continue;

        int activeFolders = 0;

        for (auto &folder : profile.folders)
            if (!folder.paused && folder.exists)
                activeFolders++;

        if (activeFolders >= 2)
        {
            for (auto &folder : profile.folders)
                if (!folder.paused)
                    numOfActiveFiles += folder.files.size();
        }
    }

    if (!syncTimer.isActive() || numOfActiveFiles < syncTimer.remainingTime())
        syncTimer.start(numOfActiveFiles < 1000 ? 1000 : numOfActiveFiles);
}

/*
===================
MainWindow::updateAppIfNeeded

Makes the app responsible if it takes too much time to process
===================
*/
bool MainWindow::updateAppIfNeeded()
{
    if (updateTimer.remainingTime() <= 0)
    {
        updateStatus();
        QApplication::processEvents();
        updateTimer.start(UPDATE_TIME);
    }

    return shouldQuit;
}

/*
===================
MainWindow::showContextMenu
===================
*/
void MainWindow::showContextMenu(const QPoint &pos) const
{
    QMenu menu;

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
                menu.addAction(iconSync, "&Synchronize profile", this, std::bind(&MainWindow::sync, const_cast<MainWindow *>(this), row))->setDisabled(queue.contains(row));
            }

            menu.addAction(iconRemove, "&Remove profile", this, SLOT(removeProfile()));
        }

        menu.exec(ui->syncProfilesView->mapToGlobal(pos));
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

        menu.exec(ui->folderListView->mapToGlobal(pos));
    }
}

/*
===================
MainWindow::GetListOfFiles
===================
*/
void MainWindow::GetListOfFiles(Folder &folder)
{
    if ((folder.paused && syncingMode == Automatic) || !folder.exists) return;

    for (auto &file : folder.files)
    {
        file.exists = false;
        file.updated = false;
    }

#ifdef DEBUG_TIMESTAMP
    auto startTime = std::chrono::high_resolution_clock::now();
    int totalNumOfFiles = 0;
#endif

#ifndef USE_STD_FILESYSTEM
    QDirIterator dir(folder.path, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);

    while (dir.hasNext())
    {
        if (folder.paused && syncingMode == Automatic) return;

        dir.next();

        QString fileName(dir.fileInfo().filePath());
        fileName.remove(0, folder.path.size());
        File::Type type = dir.fileInfo().isDir() ? File::dir : File::file;
        size_t fileHash = qHash(fileName);

        if (folder.files.contains(fileHash))
        {
            bool updated = false;
            QString parentPath(dir.fileInfo().path());
            File &file = folder.files[fileHash];

            QDateTime fileDate(dir.fileInfo().lastModified());

            if (type == File::dir)
            {
                updated = (file.date < fileDate);

                // Marks all parent folders as updated in case if our folder was updated
                if (updated)
                {
                    QString folderPath(dir.fileInfo().filePath());

                    while (folderPath.remove(folderPath.lastIndexOf("/"), 999999).length() > folder.path.length())
                    {
                        size_t hash = qHash(QString(folderPath).remove(0, folder.path.size()));

                        if (!folder.files.value(hash).updated)
                            folder.files[hash].updated = true;
                        else
                            break;

                        if (updateAppIfNeeded()) return;
                    }
                }
            }
#ifdef Q_OS_LINUX
            else if (type == File::file && file.date < fileDate)
            {
                updated = true;
            }
#endif

            // Marks a file/folder as updated if its parent folder was updated
            if (parentPath.length() > folder.path.length() && folder.files.value(qHash(parentPath.remove(0, folder.path.size()))).updated)
                updated = true;

            file.date = fileDate;
            file.updated = updated;
            file.exists = true;
        }
        else
        {
            folder.files.insert(fileHash, File(fileName, type, dir.fileInfo().lastModified()));
        }

#ifdef DEBUG_TIMESTAMP
            totalNumOfFiles++;
#endif

        if (updateAppIfNeeded()) return;
    }
#elif defined(USE_STD_FILESYSTEM)
    for (auto const &dir : std::filesystem::recursive_directory_iterator{std::filesystem::path{folder.path.toStdString()}})
    {
        if (folder.paused && syncingMode == Automatic) return;

        QString filePath(dir.path().string().c_str());
        filePath.remove(0, folder.path.size());
        filePath.replace("\\", "/");

        File::Type type = std::filesystem::is_directory(dir.path()) ? File::dir : File::file;
        size_t fileHash = qHash(filePath);

        folder.files.insert(fileHash, File(filePath, type, QDateTime(), false)); // FIX: date and updated flag

#ifdef DEBUG_TIMESTAMP
        totalNumOfFiles++;
#endif

        if (updateAppIfNeeded()) return;
    }
#endif

#ifdef DEBUG_TIMESTAMP
    std::chrono::high_resolution_clock::time_point launchTime(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(launchTime.time_since_epoch());
    qDebug("%lld ms - Found %d files at %s)", ml.count(), totalNumOfFiles, qUtf8Printable(folder.path));
#endif
}

/*
===================
MainWindow::checkForChanges
===================
*/
void MainWindow::checkForChanges(Profile &profile)
{
    if ((profile.paused && syncingMode == Automatic) || profile.folders.size() < 2) return;

#ifdef DEBUG_TIMESTAMP
    auto startTime = std::chrono::high_resolution_clock::now();
#endif

    // Checks for added/modified files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        if (!folderIt->exists) continue;

        for (auto otherFolderIt = profile.folders.begin(); otherFolderIt != profile.folders.end(); ++otherFolderIt)
        {
            if (folderIt == otherFolderIt || !otherFolderIt->exists) continue;
            if (folderIt->paused && syncingMode == Automatic) break;

            for (QHash<uint, File>::iterator otherFileIt = otherFolderIt->files.begin(); otherFileIt != otherFolderIt->files.end(); ++otherFileIt)
            {
                if (otherFolderIt->paused && syncingMode == Automatic) break;

                const File &file = folderIt->files.value(otherFileIt.key());

                // Adds a newer version of a file from other backup folders if exists
#ifdef Q_OS_LINUX
                if (file.exists && file.type != File::dir && file.date < otherFileIt.value().date && otherFileIt.value().updated)
#else
                if (file.exists && file.type != File::dir && file.date < otherFileIt.value().date)
#endif
                {
                    QString from(otherFolderIt->path);
                    from.append(otherFileIt.value().path);

                    folderIt->filesToAdd.insert(otherFileIt.value().path, from);
                }
                // Adds a new file/folder from other backup folders or if it has a new version of a file/folder and our copy file/folder with the same name was removed.
                // Files use their last modification date for figuring out which one is newer, and folders use the updated flag instead as
                // folders can contain different last modification date based on changes of its directories.
                else if ((!file.type ||
#ifdef Q_OS_LINUX
                          (file.type == File::file && !file.exists && (file.date < otherFileIt.value().date && otherFileIt.value().updated)) ||
#else
                          (file.type == File::file && !file.exists && (file.date < otherFileIt.value().date || otherFileIt.value().updated)) ||
#endif
                          (file.type == File::dir && !file.exists && otherFileIt.value().updated)) &&
                         !otherFolderIt->filesToRemove.contains(QString(otherFolderIt->path).append(otherFileIt.value().path)))
                {
                    if (otherFileIt.value().type == File::dir)
                    {
                        folderIt->foldersToAdd.insert(otherFileIt.value().path);
                    }
                    else
                    {
                        QString from(otherFolderIt->path);
                        from.append(otherFileIt.value().path);

                        folderIt->filesToAdd.insert(otherFileIt.value().path, from);
                    }
                }

                if (updateAppIfNeeded()) return;
            }
        }
    }

#ifdef DEBUG_TIMESTAMP
    std::chrono::high_resolution_clock::time_point launchTime(std::chrono::high_resolution_clock::now() - startTime);
    auto ml = std::chrono::duration_cast<std::chrono::milliseconds>(launchTime.time_since_epoch());
    qDebug("%lld ms - Checked for added/modified files and folders", ml.count());
    startTime = std::chrono::high_resolution_clock::now();
#endif

    // Checks for removed files and folders
    for (auto folderIt = profile.folders.begin(); folderIt != profile.folders.end(); ++folderIt)
    {
        for (QHash<uint, File>::iterator fileIt = folderIt->files.begin() ; fileIt != folderIt->files.end();)
        {
            if (folderIt->paused && syncingMode == Automatic) break;

            // Removes files/folders if it doesn't exist on disk and in adding lists
            if (!fileIt.value().exists && !folderIt->foldersToAdd.contains(fileIt.value().path) && !folderIt->filesToAdd.contains(fileIt.value().path))
            {
                for (auto removeIt = profile.folders.begin(); removeIt != profile.folders.end(); ++removeIt)
                {
                    if (folderIt == removeIt || (removeIt->paused && syncingMode == Automatic)) continue;

                    removeIt->filesToRemove.insert(fileIt.value().path);
                }

                fileIt = folderIt->files.erase(static_cast<QHash<uint, File>::const_iterator>(fileIt));
            }
            else
            {
                ++fileIt;
            }

            if (updateAppIfNeeded()) return;
        }
    }

#ifdef DEBUG_TIMESTAMP
    std::chrono::high_resolution_clock::time_point launchTime2(std::chrono::high_resolution_clock::now() - startTime);
    ml = std::chrono::duration_cast<std::chrono::milliseconds>(launchTime2.time_since_epoch());
    qDebug("%lld ms - Checked for removed files", ml.count());
#endif
}
