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

#ifndef UNHIDABLEMENU_H
#define UNHIDABLEMENU_H

#include <QMenu>

/*
===========================================================

    UnhiddableMenu

===========================================================
*/
class UnhidableMenu : public QMenu
{
    Q_OBJECT

public:

    explicit UnhidableMenu(QWidget *parent = nullptr) : QMenu(parent){}
    explicit UnhidableMenu(const QString &title, QWidget *parent = nullptr) : QMenu(title, parent){}

    void keyPressEvent(QKeyEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

};

#endif // UNHIDABLEMENU_H
