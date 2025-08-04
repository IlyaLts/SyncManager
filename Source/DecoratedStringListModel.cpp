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

#include "DecoratedStringListModel.h"
#include <QColor>
#include <QStringList>

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
    case Qt::ToolTipRole: return toolTips[index.row()];
    }

    return QStandardItemModel::data(index, role);
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
    case Qt::ToolTipRole:
    {
        toolTips[index.row()] = value.value<QString>();
        return true;
    }
    }

    return QStandardItemModel::setData(index, value, role);
}

/*
===================
DecoratedStringListModel::index
===================
*/
QModelIndex DecoratedStringListModel::index(int row, const QModelIndex &parent) const
{
    return QStandardItemModel::index(row, 0, parent);
}

/*
===================
DecoratedStringListModel::setStringList
===================
*/
void DecoratedStringListModel::setStringList(const QStringList &list)
{
    clear();

    for (const QString &string : list)
        appendRow(new QStandardItem(string));
}

/*
===================
DecoratedStringListModel::StringList
===================
*/
QStringList DecoratedStringListModel::stringList()
{
    QStringList stringList;
    int rowCount = QStandardItemModel::rowCount();

    for (int row = 0; row < rowCount; ++row)
    {
        QModelIndex modelIndex = QStandardItemModel::index(row, 0);

        if (!modelIndex.isValid())
            continue;

        stringList.append(data(modelIndex, Qt::DisplayRole).toString());
    }

    return stringList;
}
