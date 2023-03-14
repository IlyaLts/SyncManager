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

#ifndef DECORATEDSTRINGLISTMODEL_H
#define DECORATEDSTRINGLISTMODEL_H

#include <QStringListModel>

/*
===========================================================

    DecoratedStringListModel

===========================================================
*/
class DecoratedStringListModel : public QStringListModel
{
public:

    explicit DecoratedStringListModel(QObject* parent = nullptr) : QStringListModel(parent){}

    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

private:

    QMap<int, QVariant> rowIcons;
    QMap<int, QColor> rowColors;
    QMap<int, QColor> textColors;
};

#endif // DECORATEDSTRINGLISTMODEL_H
