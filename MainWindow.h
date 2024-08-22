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
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QMovie>
#include <QMessageBox>

#define SYNCMANAGER_VERSION     "1.6.3"
#define SETTINGS_FILENAME       "Settings.ini"
#define PROFILES_FILENAME       "Profiles.ini"
#define UPDATE_DELAY            40

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class DecoratedStringListModel;
class QItemSelection;
class QMimeData;
class UnhidableMenu;

extern QTranslator translator;

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
    void switchDeletionMode(SyncManager::DeletionMode mode);
    void increaseSyncTime();
    void decreaseSyncTime();
    void switchLanguage(QLocale::Language language);
    void toggleLaunchOnStartup();
    void toggleShowInTray();
    void toggleNotification();
    void toggleRememberFiles();
    void toggleDetectMoved();
    void updateSyncTime();
    void updateLastSyncTime(SyncProfile *profile);
    void updateStatus();
    bool updateApp();
    void showContextMenu(const QPoint &pos) const;
    void sync(int profileNumber = -1);
    void saveSettings() const;
    void notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon);

private:

    void retranslate();
    bool questionBox(QMessageBox::Icon icon, const QString &title, const QString &text, QMessageBox::StandardButton button = QMessageBox::Yes);

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
    QAction *chineseAction;
    QAction *englishAction;
    QAction *frenchAction;
    QAction *germanAction;
    QAction *hindiAction;
    QAction *italianAction;
    QAction *japaneseAction;
    QAction *portugueseAction;
    QAction *russianAction;
    QAction *spanishAction;
    QAction *ukrainianAction;
    QAction *launchOnStartupAction;
    QAction *showInTrayAction;
    QAction *disableNotificationAction;
    QAction *enableRememberFilesAction;
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
    UnhidableMenu *languageMenu;

    QLocale::Language language;
    QLocale locale;
    QTimer updateTimer;
    bool showInTray;
    bool appInitiated = false;
};

#endif // MAINWINDOW_H
