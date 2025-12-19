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

#include "Application.h"
#include "MainWindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QSharedMemory>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <synchapi.h>
#endif

/*
===================
main
===================
*/
int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(SyncManager);
    Application app(argc, argv);
    QSharedMemory sharedMemory("SyncManagerRunning");

    // If the app crashes on Linux, the shared memory segment survives the crash,
    // preventing the app from further launching. To fix this we need to attach
    // the process to the existing shared memory segment and then detach it immediately,
    // This triggers the Linux kernel to release the shared memory segment, allowing the app to launch again.
    sharedMemory.attach();
    sharedMemory.detach();

    // Prevention of multiple instances
    if (!sharedMemory.create(1))
    {
        QString title = app.translate("MainWindow", "Couldn't launch!");
        QString text = app.translate("MainWindow", "The app is already launched and cannot be launched as a second instance.");
        QMessageBox::warning(NULL, title, text);
        return -1;
    }

// Used by Inno setup to prevent the user from installing new versions of an application while
// the application is still running, and to prevent the user from uninstalling a running application.
#ifdef Q_OS_WIN
    CreateMutexA(NULL, FALSE, "SyncManagerMutex");
#endif

    if (QCoreApplication::arguments().contains("reset", Qt::CaseInsensitive))
    {
        QString localDataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

        QSettings(localDataPath + "/" + PROFILES_FILENAME, QSettings::IniFormat).clear();
        QSettings(localDataPath + "/" + SETTINGS_FILENAME, QSettings::IniFormat).clear();
    }

    // Used by installers to make it launch on startup
    if (QCoreApplication::arguments().contains("launchOnStartup", Qt::CaseInsensitive))
        app.setLaunchOnStartup(true);

    app.init();
    return app.exec();
}
