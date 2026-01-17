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

#include "Application.h"
#include "SystemTray.h"
#include "MainWindow.h"
#include <QMenu>
#include <QAction>

/*
===================
SystemTray::SystemTray
===================
*/
SystemTray::SystemTray()
{
    m_iconDone.addFile(":/Images/TrayIconDone.png");
    m_iconDonePartial.addFile(":/Images/TrayIconDonePartial.png");
    m_iconIssue.addFile(":/Images/TrayIconIssue.png");
    m_iconPause.addFile(":/Images/TrayIconPause.png");
    m_iconSync.addFile(":/Images/TrayIconSync.png");
    m_iconWarning.addFile(":/Images/TrayIconWarning.png");

    m_showAction = new QAction("&" + syncApp->translate("Show"), this);
    m_quitAction = new QAction("&" + syncApp->translate("Quit"), this);

    m_trayIconMenu = new QMenu(nullptr);

#ifdef Q_OS_LINUX
    m_trayIconMenu->addAction(m_showAction);
#endif

    m_trayIconMenu->addAction(m_quitAction);

    setToolTip("Sync Manager");
    setContextMenu(m_trayIconMenu);
    setIcon(m_iconDone);

    connect(this, &QSystemTrayIcon::activated, this, &SystemTray::iconActivated);
    connect(m_showAction, &QAction::triggered, this, [this](){ iconActivated(QSystemTrayIcon::DoubleClick); });
    connect(m_quitAction, &QAction::triggered, syncApp, &Application::quit);
}

/*
===================
SystemTray::retranslate
===================
*/
void SystemTray::retranslate()
{
    m_showAction->setText("&" + syncApp->translate("Show"));
    m_quitAction->setText("&" + syncApp->translate("Quit"));
}

/*
===================
SystemTray::setIcon
===================
*/
void SystemTray::setIcon(const QIcon &icon)
{
    // Fixes flickering menu icon
    if (this->icon().cacheKey() != icon.cacheKey())
        QSystemTrayIcon::setIcon(icon);
}

/*
===================
SystemTray::addAction
===================
*/
void SystemTray::addAction(QAction *action)
{
    m_trayIconMenu->insertAction(m_trayIconMenu->actions()[0], action);
}

/*
===================
SystemTray::addMenu
===================
*/
void SystemTray::addMenu(QMenu *menu)
{
    m_trayIconMenu->insertMenu(m_trayIconMenu->actions()[0], menu);
}

/*
===================
SystemTray::addSeparator
===================
*/
void SystemTray::addSeparator()
{
    m_trayIconMenu->insertSeparator(m_trayIconMenu->actions()[0]);
}

/*
===================
SystemTray::iconActivated
===================
*/
void SystemTray::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason)
    {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:

#ifdef Q_OS_LINUX
    // Double click doesn't work on GNOME
    case QSystemTrayIcon::MiddleClick:
#endif

        syncApp->window()->show();
        break;
    default:
        break;
    }
}

/*
===================
SystemTray::notify

QSystemTrayIcon doesn't display messages when hidden.
A quick workaround is to temporarily show the tray, display the message, and then re-hide it.
===================
*/
void SystemTray::notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon)
{
    if (!isSystemTrayAvailable() || !syncApp->manager()->notificationsEnabled())
        return;

    bool visible = isVisible();

    if (!visible)
        show();

    showMessage(title, message, icon, std::numeric_limits<int>::max());

    if (!visible)
        hide();
}
