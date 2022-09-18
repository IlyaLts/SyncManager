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
    QSharedMemory sharedMemory("SyncManagerLaunched");

    // Prevention of multiple instances
    if (!sharedMemory.create(1))
    {
        QMessageBox::warning(NULL, "Couldn't launch!", "The app is already launched and cannot be launched as a second instance.");
        app.exit(-1);
        return -1;
    }

    MainWindow window;

    if (QCoreApplication::arguments().contains("launchOnStartup", Qt::CaseInsensitive))
    {
#ifdef Q_OS_WIN
        QFile::link(QCoreApplication::applicationFilePath(), QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/Startup/SyncManager.lnk");
#elif defined(Q_OS_LINUX)
        QString path(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/SyncManager.desktop");

        QFile::remove(path);
        QFile shortcut(path);
        if (shortcut.open(QIODevice::WriteOnly))
        {
            QTextStream stream(&shortcut);
            stream << "[Desktop Entry]\n";
            stream << "Type=Application\n";
            stream << "Exec=" + QCoreApplication::applicationDirPath() + "/SyncManager minimize\n";
            stream << "Hidden=false\n";
            stream << "NoDisplay=false\n";
            stream << "Terminal=false\n";
            stream << "Name=Sync Manager\n";
            stream << "Icon=" + QCoreApplication::applicationDirPath() + "/SyncManager.png\n";
        }

        // Somehow doesn't work on Linux
        //QFile::link(QCoreApplication::applicationFilePath(), path);
#endif
    }
    if (QCoreApplication::arguments().contains("reset", Qt::CaseInsensitive))
    {
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + PROFILES_FILENAME, QSettings::IniFormat).clear();
        QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat).clear();
    }

    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        if (!QCoreApplication::arguments().contains("minimize", Qt::CaseInsensitive)) window.show();
        QApplication::setQuitOnLastWindowClosed(false);
    }
    else
    {
        window.show();
    }

    return app.exec();
}
