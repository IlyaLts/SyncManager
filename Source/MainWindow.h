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
#include "SyncWorker.h"
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMovie>
#include <QThread>

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
    void switchSyncingMode(SyncProfile &profile, SyncProfile::SyncingMode mode);
    void switchDeletionMode(SyncProfile &profile, SyncProfile::DeletionMode mode);
    void switchVersioningFormat(SyncProfile &profile, SyncProfile::VersioningFormat format);
    void switchVersioningLocation(SyncProfile &profile, SyncProfile::VersioningLocation location);
    void switchSyncingType(SyncProfile &profile, SyncFolder &folder, SyncFolder::SyncType type);
    void switchDatabaseLocation(SyncProfile &profile, SyncProfile::DatabaseLocation location);
    void switchPriority(QThread::Priority priority);
    void increaseSyncTime(SyncProfile &profile);
    void decreaseSyncTime(SyncProfile &profile);
    void switchLanguage(QLocale::Language language);
    void toggleLaunchOnStartup();
    void toggleShowInTray();
    void toggleNotification();
    void setMaximumTransferRateUsage();
    void setMaximumCpuUsage();
    void setFixedInterval(SyncProfile &profile);
    void setVersioningPostfix(SyncProfile &profile);
    void setVersioningPattern(SyncProfile &profile);
    void setVersioningLocationPath(SyncProfile &profile);
    void setFileMinSize(SyncProfile &profile);
    void setFileMaxSize(SyncProfile &profile);
    void setMovedFileMinSize(SyncProfile &profile);
    void setIncludeList(SyncProfile &profile);
    void setExcludeList(SyncProfile &profile);
    void toggleIgnoreHiddenFiles(SyncProfile &profile);
    void toggleDetectMoved(SyncProfile &profile);
    void showContextMenu(const QPoint &pos);
    void sync(SyncProfile *profile, bool hidden = false);
    void syncDone();
    void profileSynced(SyncProfile *profile);

private:

    void rebindProfiles();
    void connectProfileMenu(SyncProfile &profile);
    void disconnectProfileMenu(SyncProfile &profile);
    void notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon);
    void updateStatus();
    void updateMenuMaxDiskTransferRate();
    void updateMenuSyncTime(SyncProfile &profile);
    void updateProfileTooltip(const SyncProfile &profile);
    void loadSettings();
    void saveSettings() const;
    void setupMenus();
    void updateStrings();
    void updateLaunchOnStartupState();
    SyncProfile *profileByIndex(const QModelIndex &index);
    QModelIndex profileIndex(const SyncProfile &profile);
    QModelIndex profileIndexByName(const QString &name);

    SyncManager manager;
    Ui::MainWindow *ui;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;

    QList<QAction *> countryIcons;
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
    QAction *maximumDiskTransferRateAction;
    QAction *maximumCpuUsageAction;
    QAction *idlePriorityAction;
    QAction *lowestPriorityAction;
    QAction *lowPriorityAction;
    QAction *normalPriorityAction;
    QAction *highPriorityAction;
    QAction *highestPriorityAction;
    QAction *timeCriticalPriorityAction;
    QList<QAction *> languageActions;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *showAction;
    QAction *quitAction;
    QAction *reportBugAction;
    QAction *versionAction;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    UnhidableMenu *settingsMenu;
    UnhidableMenu *performanceMenu;
    UnhidableMenu *priorityMenu;
    UnhidableMenu *languageMenu;

    QThread::Priority priority = QThread::NormalPriority;
    QThread *syncThread;
    SyncWorker *syncWorker;
    QLocale::Language language;
    QTimer updateTimer;
    bool showInTray;
    bool appInitiated = false;
};

#endif // MAINWINDOW_H
