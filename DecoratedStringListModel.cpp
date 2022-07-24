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

#include "DecoratedStringListModel.h"
#include <QColor>

/*
===================
DecoratedStringListModel::data
===================
*/
QVariant DecoratedStringListModel::data(const QModelIndex &index, int role) const
{
    switch (role)
    {
    case Qt::DecorationRole: return rowIcons[index.row()];
    case Qt::BackgroundRole: return rowColors.contains(index.row()) ? rowColors[index.row()] : QColor("White");
    case Qt::ForegroundRole: return textColors[index.row()];
    }

    return QStringListModel::data(index, role);
}

/*
===================
DecoratedStringListModel::setData
===================
*/
bool DecoratedStringListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    switch (role)
    {
    case Qt::DecorationRole:
    {
        rowIcons[index.row()] = value.value<QVariant>();
        return true;
    }
    case Qt::BackgroundRole:
    {
        rowColors[index.row()] = value.value<QColor>();
        return true;
    }
    case Qt::ForegroundRole:
    {
        textColors[index.row()] = value.value<QColor>();
        return true;
    }
    }

    return QStringListModel::setData(index, value, role);
}
