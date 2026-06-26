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

#ifndef PROFILEMENU_H
#define PROFILEMENU_H

#include "SyncProfile.h"

/*
===========================================================

    ProfileMenu

===========================================================
*/
class ProfileMenu : public QWidget
{
    Q_OBJECT

public:

    ProfileMenu(QWidget *parent, SyncProfile *profile);

    void setup(QWidget *parent = nullptr);
    void updateStates();
    void retranslate();
    void exportMenu(QMenu *menu);
    void enable(bool enable);
    void updateSyncTime();

public Q_SLOTS:

    void switchSyncingMode(SyncProfile::SyncingMode mode);
    void setDeltaCopying(bool enable);
    void increaseSyncTime();
    void decreaseSyncTime();
    void setFixedInterval();
    void toggleDetectMoved();
    void switchDeletionMode(SyncProfile::DeletionMode mode);
    void switchVersioningFormat(SyncProfile::VersioningFormat format);
    void setVersioningPostfix();
    void setVersioningPattern();
    void switchVersioningLocation(SyncProfile::VersioningLocation location);
    void setVersioningLocationPath();
    void switchDatabaseLocation(SyncProfile::DatabaseLocation location);
    void setFileMinSize();
    void setFileMaxSize();
    void setMovedFileMinSize();
    void setDeltaCopyingMinSize();
    void setIncludeList();
    void setExcludeList();
    void toggleIgnoreHiddenFiles();

private:

    SyncProfile *profile = nullptr;

    QAction *manualAction;
    QAction *automaticAdaptiveAction;
    QAction *automaticFixedAction;
    QAction *detectMovedFilesAction;
    QAction *deltaCopyingAction;
    QAction *increaseSyncTimeAction;
    QAction *syncingTimeAction;
    QAction *decreaseSyncTimeAction;
    QAction *fixedSyncingTimeAction;
    QAction *moveToTrashAction;
    QAction *versioningAction;
    QAction *deletePermanentlyAction;
    QAction *fileTimestampBeforeAction;
    QAction *fileTimestampAfterAction;
    QAction *folderTimestampAction;
    QAction *lastVersionAction;
    QAction *versioningPostfixAction;
    QAction *versioningPatternAction;
    QAction *locallyNextToFolderAction;
    QAction *customLocationAction;
    QAction *customLocationPathAction;
    QAction *databaseLocallyAction;
    QAction *databaseDecentralizedAction;
    QAction *fileMinSizeAction;
    QAction *fileMaxSizeAction;
    QAction *movedFileMinSizeAction;
    QAction *deltaCopyingMinSizeAction;
    QAction *includeAction;
    QAction *excludeAction;
    QAction *ignoreHiddenFilesAction;

    UnhidableMenu *syncingModeMenu;
    UnhidableMenu *deletionModeMenu;
    UnhidableMenu *versioningFormatMenu;
    UnhidableMenu *versioningLocationMenu;
    UnhidableMenu *databaseLocationMenu;
    UnhidableMenu *filteringMenu;
};

#endif // PROFILEMENU_H
