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

#include "FolderStyleDelegate.h"

/*
===================
FolderStyleDelegate::paint
===================
*/
void FolderStyleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QIcon syncTypeIcon = index.model()->data(index, SyncTypeRole).value<QIcon>();

    QStyledItemDelegate::paint(painter, option, index);

    if (!syncTypeIcon.isNull())
    {
        int iconSize = option.decorationSize.width() * 1.2f;
        QRect syncTypeRect = option.rect;

        syncTypeRect.setRight(syncTypeRect.right() - 5);
        syncTypeRect.setLeft(syncTypeRect.right() - iconSize);
        syncTypeRect.setWidth(iconSize);
        syncTypeRect.setHeight(iconSize);
        syncTypeRect.moveCenter(QPoint(syncTypeRect.center().x(), option.rect.center().y()));

        syncTypeIcon.paint(painter, syncTypeRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    }
}

/*
===================
FolderStyleDelegate::sizeHint
===================
*/
QSize FolderStyleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize baseSize = QStyledItemDelegate::sizeHint(option, index);

    if (!index.model()->data(index, SyncTypeRole).value<QIcon>().isNull())
        baseSize.setWidth(baseSize.width() + option.decorationSize.width());

    return baseSize;
}
