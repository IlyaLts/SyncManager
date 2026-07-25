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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "SyncManager.h"
#include "SyncProfile.h"
#include "MenuBar.h"
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
class ProfileMenu;

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

    ProfileMenu *profileMenu(SyncProfile *profile) { return profileMenus.value(profile); }
    void removeProfileMenu(SyncProfile *profile);

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
    void switchSyncingType(SyncFolder &folder, SyncFolder::Type type);
    void showProfileContextMenu(const QPoint &pos);
    void showFolderContextMenu(const QPoint &pos);
    void sync(SyncProfile *profile, bool hidden = false);
    void syncDone();
    void profileSynced(SyncProfile *profile);

private:

    void rebindProfiles();
    void updateStatus();
    void updateProfilesStatus();
    void updateFoldersStatus();
    void updatePauseState();
    void updateIcons();
    void updateWindowTitle();
    void updateProfileTooltip(const SyncProfile &profile);
    void setupMenus();

    QMap<SyncProfile *, ProfileMenu *> profileMenus;
    MenuBar *menuBar;

    Ui::MainWindow *ui;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;

    QIcon iconAdd;
    QIcon iconDone;
    QIcon iconDonePartial;
    QIcon iconPause;
    QIcon iconRemove;
    QIcon iconResume;
    QIcon iconSync;
    QIcon iconWarning;
    QIcon iconTwoWay;
    QIcon iconOneWay;
    QIcon iconOneWayUpdate;

    QMovie animSync;
    QTimer updateTimer;
};

#endif // MAINWINDOW_H
