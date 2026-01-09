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
#include <QTimer>
#include <QMovie>
#include <QSplitter>
#include "ui_MainWindow.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DecoratedStringListModel;
class QItemSelection;
class QMimeData;
class UnhidableMenu;
class QPushButton;

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

    void retranslate();
    void loadSettings();
    void saveSettings() const;

public Q_SLOTS:

    void show();

protected:

    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private Q_SLOTS:

    void addProfile();
    void removeProfile();
    void profileClicked(const QItemSelection &selected, const QItemSelection &deselected);
    void profileNameChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);
    void addFolder(const QMimeData *mimeData = nullptr);
    void removeFolder();
    void pauseSyncing();
    void pauseSelected();
    void switchSyncingMode(SyncProfile &profile, SyncProfile::SyncingMode mode);
    void switchDeletionMode(SyncProfile &profile, SyncProfile::DeletionMode mode);
    void switchVersioningFormat(SyncProfile &profile, SyncProfile::VersioningFormat format);
    void switchVersioningLocation(SyncProfile &profile, SyncProfile::VersioningLocation location);
    void switchSyncingType(SyncProfile &profile, SyncFolder &folder, SyncFolder::Type type);
    void switchDatabaseLocation(SyncProfile &profile, SyncProfile::DatabaseLocation location);
    void increaseSyncTime(SyncProfile &profile);
    void decreaseSyncTime(SyncProfile &profile);
    void switchLanguage(QLocale::Language language);
    void updateLanguageMenu();
    void toggleLaunchOnStartup();
    void toggleShowInTray();
    void toggleNotification();
    void toggleCheckForUpdates();
    void setMaximumTransferRateUsage();
    void setMaximumCpuUsage();
    void setFixedInterval(SyncProfile &profile);
    void setVersioningPostfix(SyncProfile &profile);
    void setVersioningPattern(SyncProfile &profile);
    void setVersioningLocationPath(SyncProfile &profile);
    void setFileMinSize(SyncProfile &profile);
    void setFileMaxSize(SyncProfile &profile);
    void setMovedFileMinSize(SyncProfile &profile);
    void setDeltaCopyingMinSize(SyncProfile &profile);
    void setIncludeList(SyncProfile &profile);
    void setExcludeList(SyncProfile &profile);
    void toggleIgnoreHiddenFiles(SyncProfile &profile);
    void toggleDetectMoved(SyncProfile &profile);
    void showContextMenu(const QPoint &pos);
    void sync(SyncProfile *profile, bool hidden = false);
    void syncDone();
    void profileSynced(SyncProfile *profile);
    void updateAvailable();

private:

    void rebindProfiles();
    void connectProfileMenu(SyncProfile &profile);
    void disconnectProfileMenu(SyncProfile &profile);
    void updateStatus();
    void updateMenuMaxDiskTransferRate();
    void updateMenuSyncTime(const SyncProfile &profile);
    void updateProfileTooltip(const SyncProfile &profile);
    void setupMenus();
    void updateLaunchOnStartupState();
    SyncProfile *profileByIndex(const QModelIndex &index);
    QModelIndex indexByProfile(const SyncProfile &profile);
    QModelIndex profileIndexByName(const QString &name);

    Ui::MainWindow *ui;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;

    QIcon iconAdd;
    QIcon iconDone;
    QIcon iconDonePartial;
    QIcon iconPause;
    QIcon iconRemove;
    QIcon iconResume;
    QIcon iconSettings;
    QIcon iconSync;
    QIcon iconWarning;
    QIcon iconTwoWay;
    QIcon iconOneWay;
    QIcon iconOneWayUpdate;

    QMovie animSync;

    QAction *syncNowAction;
    QAction *pauseSyncingAction;
    QAction *maximumDiskTransferRateAction;
    QAction *maximumCpuUsageAction;
    QList<QAction *> languageActions;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *checkForUpdatesAction;
    QAction *userManualAction;
    QAction *reportBugAction;
    QAction *versionAction;

    UnhidableMenu *settingsMenu;
    UnhidableMenu *performanceMenu;
    UnhidableMenu *languageMenu;

    QPushButton *updateAvailableButton;

    QTimer updateTimer;
};

#endif // MAINWINDOW_H
