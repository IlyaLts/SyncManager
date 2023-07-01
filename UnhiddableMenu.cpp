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

#include <QKeyEvent>
#include "UnhiddableMenu.h"

/*
===================
UnhiddableMenu::keyPressEvent
===================
*/
void UnhiddableMenu::keyPressEvent(QKeyEvent *event)
{
    QAction *action = activeAction();

    if ((event->key() == Qt::Key_Return) && action && action->isEnabled())
    {
        action->setEnabled(false);
        QMenu::keyReleaseEvent(event);
        action->setEnabled(true);
        action->trigger();
    }
    else
    {
        QMenu::keyPressEvent(event);
    }
}


/*
===================
UnhiddableMenu::mouseReleaseEvent
===================
*/
void UnhiddableMenu::mouseReleaseEvent(QMouseEvent *event)
{
    QAction *action = activeAction();

    if (action && action->isEnabled())
    {
        action->setEnabled(false);
        QMenu::mouseReleaseEvent(event);
        action->setEnabled(true);
        action->trigger();
    }
    else
    {
        QMenu::mouseReleaseEvent(event);
    }
}
