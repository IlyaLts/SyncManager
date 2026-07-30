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

#include "AboutDialog.h"
#include "Application.h"
#include <QLabel>

/*
===================
AboutDialog::AboutDialog
===================
*/
AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(syncApp->translate("About"));

    QVBoxLayout *layout = new QVBoxLayout(this);

    QPixmap logo(":/Images/Icon.ico");
    logo = logo.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QLabel *logoLabel = new QLabel(this);
    logoLabel->setPixmap(logo);

    QLabel *version = new QLabel("<span style='font-size: 22pt;'>SyncManager</span><br>"
                                 "<span style='font-size: 12pt;'>" + syncApp->translate("Version") +
                                 QString(": %1").arg(SYNCMANAGER_VERSION), this);

    QLabel *about = new QLabel(syncApp->translate("Lightweight Synchronization Manager"));

    QLabel *description = new QLabel(syncApp->translate("If you find this tool valuable and "
                                        "have some thoughts or suggestions for "
                                        "new features, please feel free to ") +
                                        "<a href='" BUG_TRACKER_URL "'>" +
                                        syncApp->translate("share them") + "</a>.");

    logoLabel->setAlignment(Qt::AlignCenter);
    version->setAlignment(Qt::AlignCenter);
    about->setAlignment(Qt::AlignCenter);
    description->setOpenExternalLinks(true);
    description->setWordWrap(true);

    layout->addWidget(logoLabel);
    layout->addWidget(version);
    layout->addWidget(about);
    layout->addSpacing(20);
    layout->addWidget(description);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(layout);
}
