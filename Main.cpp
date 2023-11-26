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

#include "MainWindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QSharedMemory>
#include <QMessageBox>
#include <QFile>

/*
===================
main
===================
*/
int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(SyncManager);
    QApplication app(argc, argv);
    QSharedMemory sharedMemory("SyncManagerRunning");

    // Default style (Fusion) is too buggy
#ifdef Q_OS_LINUX
    QApplication::setStyle(QStyleFactory::create("Windows"));
#endif

    // Prevention of multiple instances
    if (!sharedMemory.create(1))
    {
        QMessageBox::warning(NULL, "Couldn't launch!", "The app is already launched and cannot be launched as a second instance.");
        return -1;
    }

    if (QCoreApplication::arguments().contains("reset", Qt::CaseInsensitive))
    {
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat).clear();
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat).clear();
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + DATA_FILENAME);
    }

    MainWindow window;

    if (QCoreApplication::arguments().contains("launchOnStartup", Qt::CaseInsensitive))
        window.setLaunchOnStartup(true);

    window.show();
    return app.exec();
}
