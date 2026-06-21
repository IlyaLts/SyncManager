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

#ifndef PROFILELISTVIEW_H
#define PROFILELISTVIEW_H

#include "RemovableListView.h"

class SyncProfile;

/*
===========================================================

    ProfileListView

===========================================================
*/
class ProfileListView : public RemovableListView
{
    Q_OBJECT

public:

    explicit ProfileListView(QWidget *parent = nullptr);

    SyncProfile *profileByIndex(const QModelIndex &index);
    QModelIndex indexByProfile(const SyncProfile &profile);
    QModelIndex profileIndexByName(const QString &name);
};

#endif // PROFILELISTVIEW_H
