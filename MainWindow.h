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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "SyncManager.h"
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMovie>

#define SETTINGS_FILENAME   "Settings.ini"
#define PROFILES_FILENAME   "Profiles.ini"
#define UPDATE_DELAY        40
#define TRAY_MESSAGE_TIME   1000

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DecoratedStringListModel;
class QItemSelection;
class QMimeData;
class UnhidableMenu;

/*
===========================================================

    MainWindow

===========================================================
*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public Q_SLOTS:

    void show();
    void setTrayVisible(bool visible);
    void setLaunchOnStartup(bool enable);

protected:

    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:

    void addProfile();
    void removeProfile();
    void profileClicked(const QItemSelection &selected, const QItemSelection &deselected);
    void profileNameChanged(const QModelIndex &index);
    void addFolder(const QMimeData *mimeData = nullptr);
    void removeFolder();
    void pauseSyncing();
    void pauseSelected();
    void quit();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void switchSyncingMode(SyncManager::SyncingMode mode);
    void increaseSyncTime();
    void decreaseSyncTime();
    void updateSyncTime();
    void updateLastSyncTime(SyncProfile *profile);
    void switchDeletionMode(SyncManager::DeletionMode mode);
    void updateStatus();
    bool updateApp();
    void showContextMenu(const QPoint &pos) const;
    void doSync(int profileNumber = -1);

private:

    SyncManager manager;

    Ui::MainWindow *ui;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;

    QIcon iconAdd;
    QIcon iconDone;
    QIcon iconPause;
    QIcon iconRemove;
    QIcon iconResume;
    QIcon iconSettings;
    QIcon iconSync;
    QIcon iconWarning;
    QIcon trayIconDone;
    QIcon trayIconIssue;
    QIcon trayIconPause;
    QIcon trayIconSync;
    QIcon trayIconWarning;

    QMovie animSync;

    QAction *syncNowAction;
    QAction *pauseSyncingAction;
    QAction *automaticAction;
    QAction *manualAction;
    QAction *increaseSyncTimeAction;
    QAction *syncingTimeAction;
    QAction *decreaseSyncTimeAction;
    QAction *moveToTrashAction;
    QAction *versioningAction;
    QAction *deletePermanentlyAction;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *enableRememberFilesAction;
    QAction *detectMovedFilesAction;
    QAction *showAction;
    QAction *quitAction;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    UnhidableMenu *settingsMenu;
    UnhidableMenu *syncingModeMenu;
    UnhidableMenu *syncingTimeMenu;
    UnhidableMenu *deletionModeMenu;

    bool showInTray;
    QTimer updateTimer;
};

#endif // MAINWINDOW_H
