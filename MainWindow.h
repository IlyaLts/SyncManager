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

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QFileSystemWatcher>
#include <QList>
#include <QSet>
#include <QMap>
#include <QHash>
#include <QQueue>
#include <QTimer>
#include <QDateTime>
#include <QMovie>

#define SYNCMANAGER_VERSION "1.5"
#define SETTINGS_FILENAME "Settings.ini"
#define PROFILES_FILENAME "Profiles.ini"
#define DATA_FILENAME "Data.dat"

#define UPDATE_DELAY 40
#define SYNC_MIN_DELAY 1000
#define NOTIFICATION_DELAY 300000

// In a couple times slower than QDirIterator
//#define USE_STD_FILESYSTEM

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DecoratedStringListModel;
class QItemSelection;
class QMimeData;
class UnhidableMenu;

using hash64_t = quint64;

struct File
{
    enum Type : qint8
    {
        unknown,
        file,
        folder
    };

    File(){}
    File(QByteArray path, Type type, QDateTime time, bool updated = false, bool exists = true, bool onRestore = false) : path(path), date(time), type(type), updated(updated), exists(exists), onRestore(onRestore){}

    QByteArray path;
    QDateTime date;
    Type type = unknown;
    bool updated = false;
    bool exists = false;
    bool onRestore = false;
    bool newlyAdded = false;
    bool moved = false;
    bool movedSource = false;
};

struct SyncFolder
{
    explicit SyncFolder(bool paused) : paused(paused){}

    QByteArray path;
    QHash<hash64_t, File> files;
    QHash<hash64_t, QPair<QByteArray, QByteArray>> foldersToRename;
    QHash<hash64_t, QPair<QByteArray, QByteArray>> filesToMove;
    QHash<hash64_t, QByteArray> foldersToAdd;
    QHash<hash64_t, QPair<QPair<QByteArray, QByteArray>, QDateTime>> filesToAdd;
    QHash<hash64_t, QByteArray> foldersToRemove;
    QHash<hash64_t, QByteArray> filesToRemove;

    QHash<hash64_t, qint64> sizeList;

    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
};

struct SyncProfile
{
    explicit SyncProfile(bool paused) : paused(paused){}

    QList<SyncFolder> folders;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
    quint64 syncTime = 0;
    QDateTime lastSyncDate;
};

/*
===========================================================

    MainWindow

===========================================================
*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:

    enum SyncingMode
    {
        Automatic,
        Manual
    } syncingMode;

    enum DeletionMode
    {
        MoveToTrash,
        MoveToVersionFolder,
        DeletePermanently
    } deletionMode;

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
    void switchSyncingMode(MainWindow::SyncingMode mode);
    void increaseSyncTime();
    void decreaseSyncTime();
    void switchDeletionMode(DeletionMode mode);
    void sync(int profileNumber = -1);
    void updateStatus();
    void updateNextSyncingTime();
    bool updateApp();
    void showContextMenu(const QPoint &pos) const;

private:

    void saveData() const;
    void restoreData();
    int getListOfFiles(SyncFolder &folder);
    void checkForChanges(SyncProfile &profile);

    Ui::MainWindow *ui;

    QQueue<int> queue;
    QList<SyncProfile> profiles;
    QMap<QString, QTimer *> notificationList;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;
    QStringList profileNames;
    QList<QStringList> folderPaths;

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
    QAction *moveToVersionFolderAction;
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

    bool busy = false;
    bool paused = false;
    bool syncing = false;
    bool syncHidden = false;
    bool shouldQuit = false;
    bool showInTray = true;
    bool notifications = true;
    bool rememberFiles = false;
    bool detectMovedFiles = false;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    bool caseSensitiveSystem = false;
#else
    bool caseSensitiveSystem = true;
#endif

    int numOfFilesToSync = 0;
    int syncTimeMultiplier = 1;
    int syncEvery = 0;
    QSet<hash64_t> usedDevices;
    QString versionFolder;
    QString versionPattern;

    QTimer syncTimer;
    QTimer updateTimer;
};

#endif // MAINWINDOW_H
