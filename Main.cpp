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

#include "MainWindow.h"
#include <QApplication>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QSharedMemory>
#include <QMessageBox>

/*
===================
main
===================
*/
int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(SyncManager);
    QApplication app(argc, argv);

    // Prevention of multiple instances
    if (!QSharedMemory("SyncManagerLaunched").create(1))
    {
        QMessageBox::warning(NULL, "Couldn't launch!", "The app is already launched and cannot be launched as a second instance.");
        return -1;
    }

    if (QCoreApplication::arguments().contains("reset", Qt::CaseInsensitive))
    {
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat).clear();
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat).clear();
    }

    MainWindow window;
    if (QCoreApplication::arguments().contains("launchOnStartup", Qt::CaseInsensitive)) window.setLaunchOnStartup(true);

    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
        if (!settings.value(QLatin1String("LaunchMinimized"), true).toBool()) window.show();
        QApplication::setQuitOnLastWindowClosed(false);
    }
    else
    {
        window.show();
    }

    return app.exec();
}
