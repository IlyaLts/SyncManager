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

#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QSystemTrayIcon>

class QAction;

/*
===========================================================

    SystemTray

===========================================================
*/
class SystemTray : public QSystemTrayIcon
{
    Q_OBJECT

public:

    SystemTray();

    void retranslate();
    void setIcon(const QIcon &icon);
    void addAction(QAction *action);
    void addMenu(QMenu *menu);
    void addSeparator();

    QIcon iconDone() const { return m_iconDone; };
    QIcon iconDonePartial() const { return m_iconDonePartial; };
    QIcon iconIssue() const { return m_iconIssue; };
    QIcon iconPause() const { return m_iconPause; };
    QIcon iconSync() const { return m_iconSync; };
    QIcon iconWarning() const { return m_iconWarning; };

public Q_SLOTS:

    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon);

private:

    QIcon m_iconDone;
    QIcon m_iconDonePartial;
    QIcon m_iconIssue;
    QIcon m_iconPause;
    QIcon m_iconSync;
    QIcon m_iconWarning;

    QMenu *m_trayIconMenu = nullptr;

    QAction *m_showAction = nullptr;
    QAction *m_quitAction = nullptr;
};

#endif // SYSTEMTRAY_H
