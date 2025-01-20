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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "SyncManager.h"
#include "SyncProfile.h"
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMovie>

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
    void switchDeletionMode(SyncManager::DeletionMode mode);
    void increaseSyncTime();
    void decreaseSyncTime();
    void switchLanguage(QLocale::Language language);
    void toggleLaunchOnStartup();
    void toggleShowInTray();
    void toggleNotification();
    void setDatabaseLocation(SyncManager::DatabaseLocation location);
    void toggleIgnoreHiddenFiles();
    void toggleDetectMoved();
    void showContextMenu(const QPoint &pos) const;
    void sync(SyncProfile *profile, bool hidden = false);
    void profileSynced(SyncProfile *profile);

private:

    void notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon);
    bool updateApp();
    void updateStatus();
    void updateMenuSyncTime();
    void updateProfileTooltip(const SyncProfile &profile);
    void readSettings();
    void saveSettings() const;
    void setupMenus();
    void retranslate();
    int profileIndex(const SyncProfile &profile);

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
    QAction *saveDatabaseLocallyAction;
    QAction *saveDatabaseDecentralizedAction;
    QList<QAction *> languageActions;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *ignoreHiddenFilesAction;
    QAction *detectMovedFilesAction;
    QAction *showAction;
    QAction *quitAction;
    QAction *version;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    UnhidableMenu *settingsMenu;
    UnhidableMenu *syncingModeMenu;
    UnhidableMenu *syncingTimeMenu;
    UnhidableMenu *deletionModeMenu;
    UnhidableMenu *databaseLocationMenu;
    UnhidableMenu *languageMenu;

    QLocale::Language language;
    QTimer updateTimer;
    bool showInTray;
    bool appInitiated = false;
};

#endif // MAINWINDOW_H
