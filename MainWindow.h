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
    void switchVersioningFormat(VersioningFormat format);
    void switchVersioningLocation(VersioningLocation location, bool init = false);
    void switchSyncingType(SyncFolder &folder, SyncFolder::SyncType type);
    void increaseSyncTime();
    void decreaseSyncTime();
    void switchLanguage(QLocale::Language language);
    void toggleLaunchOnStartup();
    void toggleShowInTray();
    void toggleNotification();
    void setDatabaseLocation(SyncManager::DatabaseLocation location);
    void setVersioningPostfix();
    void setVersioningPattern();
    void setFileMinSize();
    void setFileMaxSize();
    void setMovedFileMinSize();
    void setIncludeList();
    void setExcludeList();
    void toggleIgnoreHiddenFiles();
    void toggleDetectMoved();
    void showContextMenu(const QPoint &pos);
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
    SyncProfile *profileByIndex(const QModelIndex &index);
    QModelIndex profileIndex(const SyncProfile &profile);
    QModelIndex profileIndexByName(const QString &name);

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
    QIcon iconTwoWay;
    QIcon iconOneWay;
    QIcon iconOneWayUpdate;
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
    QAction *fileTimestampBeforeAction;
    QAction *fileTimestampAfterAction;
    QAction *folderTimestampAction;
    QAction *lastVersionAction;
    QAction *versioningPostfixAction;
    QAction *versioningPatternAction;
    QAction *locallyNextToFolderAction;
    QAction *customLocationAction;
    QAction *customLocationPathAction;
    QAction *saveDatabaseLocallyAction;
    QAction *saveDatabaseDecentralizedAction;
    QAction *fileMinSizeAction;
    QAction *fileMaxSizeAction;
    QAction *movedFileMinSizeAction;
    QAction *includeAction;
    QAction *excludeAction;
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
    UnhidableMenu *versioningFormatMenu;
    UnhidableMenu *versioningLocationMenu;
    UnhidableMenu *databaseLocationMenu;
    UnhidableMenu *filteringMenu;
    UnhidableMenu *languageMenu;

    QLocale::Language language;
    QTimer updateTimer;
    bool showInTray;
    bool appInitiated = false;
};

#endif // MAINWINDOW_H
