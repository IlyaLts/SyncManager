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
#include "ProfileListView.h"
#include "DecoratedStringListModel.h"

/*
===================
ProfileListView::ProfileListView
===================
*/
ProfileListView::ProfileListView(QWidget *parent) : RemovableListView(parent)
{
}

/*
===================
ProfileListView::profileByIndex
===================
*/
SyncProfile *ProfileListView::profileByIndex(const QModelIndex &index)
{
    for (auto &profile : syncApp->manager()->profiles())
        if (profile.index() == index)
            return &profile;

    return nullptr;
}

/*
===================
ProfileListView::indexByProfile
===================
*/
QModelIndex ProfileListView::indexByProfile(const SyncProfile &profile)
{
    QAbstractItemModel *listModel = model();

    for (int i = 0; i < listModel->rowCount(); i++)
        if (listModel->index(i, 0) == profile.index())
            return listModel->index(i, 0);

    return QModelIndex();
}

/*
===================
ProfileListView::profileIndexByName
===================
*/
QModelIndex ProfileListView::profileIndexByName(const QString &name)
{
    QAbstractItemModel *listModel = model();

    for (int i = 0; i < listModel->rowCount(); i++)
        if (listModel->index(i, 0).data(Qt::DisplayRole).toString()  == name)
            return listModel->index(i, 0);

    return QModelIndex();
}
