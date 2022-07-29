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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QFileSystemWatcher>
#include <QList>
#include <QSet>
#include <QMap>
#include <QHash>
#include <QTimer>
#include <QDateTime>

#define SETTINGS_FILENAME "Settings.ini"
#define PROFILES_FILENAME "Profiles.ini"

#define RESPOND_TIME 50

// In a couple times slower than QDirIterator
//#define USE_STD_FILESYSTEM

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DecoratedStringListModel;
class QItemSelection;

struct File
{
    enum Type
    {
        none,
        file,
        dir
    } type = none;

    File(){}
    File(QString path, Type type, QDateTime time, bool updated = false) : type(type), path(path), date(time), exists(true), updated(updated){}

    QString path;
    QDateTime date;
    bool exists = false;
    bool updated = false;
};

struct Folder
{
    QString path;
    QHash<uint, File> files;
    QSet<QString> foldersToAdd;
    QMap<QString, QString> filesToAdd;
    QSet<QString> filesToRemove;

    bool exists = true;
    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
};

struct Profile
{
    QList<Folder> folders;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
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

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private Q_SLOTS:

    void addProfile();
    void removeProfile();
    void profileClicked(const QItemSelection &selected, const QItemSelection &deselected);
    void profileNameChanged(const QModelIndex &index);
    void addFolder();
    void removeFolder();
    void syncNow();
    void pauseSyncing();
    void pauseSelected();
    void quit();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void switchSyncingMode(SyncingMode mode);
    void update();
    void updateStatus();
    void updateNextSyncingTime();
    bool updateAppIfNeeded();
    void showProfileContextMenu(const QPoint &pos) const;
    void showFolderContextMenu(const QPoint &pos) const;

private:

    void GetListOfFiles(Folder &folder);
    void checkForChanges(Profile &profile);

    Ui::MainWindow *ui;

    QList<Profile> profiles;

    DecoratedStringListModel *profileModel;
    DecoratedStringListModel *folderModel;
    QStringList profileNames;
    QList<QStringList> foldersPath;

    QIcon iconAdd;
    QIcon iconDone;
    QIcon iconPause;
    QIcon iconRemove;
    QIcon iconResume;
    QIcon iconSync;
    QIcon iconWarning;
    QIcon trayIconDone;
    QIcon trayIconIssue;
    QIcon trayIconPause;
    QIcon trayIconSync;
    QIcon trayIconWarning;

    QAction *syncNowAction;
    QAction *pauseSyncingAction;
    QAction *automaticAction;
    QAction *manualAction;
    QAction *quitAction;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QMenu *syncingModeMenu;

    bool busy = false;
    bool paused = false;
    bool syncing = false;
    bool syncNowTriggered = false;
    bool shouldQuit = false;
    int numOfFilesToSync = 0;

    QTimer updateTimer;
    QTimer respondTimer;
};

#endif // MAINWINDOW_H
