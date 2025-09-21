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

#include "ProfileStyleDelegate.h"

/*
===================
ProfileStyleDelegate::paint
===================
*/
void ProfileStyleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QString queueStatusText = index.model()->data(index, QueueStatusRole).toString();

    QStyledItemDelegate::paint(painter, option, index);

    if (!queueStatusText.isEmpty())
    {
        QRect textRect = option.rect;
        textRect.setRight(textRect.right() - 5);

        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, queueStatusText);
    }
}

/*
===================
ProfileStyleDelegate::sizeHint
===================
*/
QSize ProfileStyleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize baseSize = QStyledItemDelegate::sizeHint(option, index);

    if (!index.model()->data(index, QueueStatusRole).value<QString>().isEmpty())
    {
        QFontMetrics fm(option.font);
        QString queueStatusText = index.data(QueueStatusRole).toString();
        baseSize.setWidth(baseSize.width() + option.decorationSize.width() + fm.boundingRect(queueStatusText).width());
    }

    return baseSize;
}
