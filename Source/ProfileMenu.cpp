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
#include "ProfileMenu.h"
#include "UnhidableMenu.h"
#include <QAction>
#include <QStandardPaths>
#include <QFileDialog>

/*
===================
ProfileMenu::ProfileMenu
===================
*/
ProfileMenu::ProfileMenu(QWidget *parent, SyncProfile *profile) : QWidget(parent), profile(profile)
{
    setup(this);

    ignoreHiddenFilesAction->setChecked(profile->ignoreHiddenFiles());
    detectMovedFilesAction->setChecked(profile->detectMovedFiles());

    updateSyncTime();
}

/*
===================
ProfileMenu::setup
===================
*/
void ProfileMenu::setup(QWidget *parent)
{
    manualAction = new QAction("&" + syncApp->translate("Manual"), parent);
    automaticAdaptiveAction = new QAction("&" + syncApp->translate("Automatic (Adaptive)"), parent);
    automaticFixedAction = new QAction("&" + syncApp->translate("Automatic (Fixed)"), parent);
    detectMovedFilesAction = new QAction("&" + syncApp->translate("Detect Renamed and Moved Files"), parent);
    deltaCopyingAction = new QAction("&" + syncApp->translate("File Delta Copying") + " (Beta)", parent);
    increaseSyncTimeAction = new QAction("&" + syncApp->translate("Increase"), parent);
    syncingTimeAction = new QAction(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncEvery())), parent);
    decreaseSyncTimeAction = new QAction("&" + syncApp->translate("Decrease"), parent);
    fixedSyncingTimeAction = new QAction("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncIntervalFixed())), parent);
    moveToTrashAction = new QAction("&" + syncApp->translate("Move Files to Trash"), parent);
    versioningAction = new QAction("&" + syncApp->translate("Versioning"), parent);
    deletePermanentlyAction = new QAction("&" + syncApp->translate("Delete Files Permanently"), parent);
    fileTimestampBeforeAction = new QAction("&" + syncApp->translate("File Timestamp (Before Extension)"), parent);
    fileTimestampAfterAction = new QAction("&" + syncApp->translate("File Timestamp (After Extension)"), parent);
    folderTimestampAction = new QAction("&" + syncApp->translate("Folder Timestamp"), parent);
    lastVersionAction = new QAction("&" + syncApp->translate("Last Version"), parent);
    versioningPostfixAction = new QAction(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(profile->versioningFolder()), parent);
    versioningPatternAction = new QAction(QString("&" + syncApp->translate("Pattern: %1")).arg(profile->versioningPattern()), parent);
    locallyNextToFolderAction = new QAction("&" + syncApp->translate("Locally Next to Folder"), parent);
    customLocationAction = new QAction("&" + syncApp->translate("Custom Location"), parent);
    customLocationPathAction = new QAction(syncApp->translate("Custom Location: ") + profile->versioningPath(), parent);
    databaseLocallyAction = new QAction("&" + syncApp->translate("Locally (On the local machine)"), parent);
    databaseDecentralizedAction = new QAction("&" + syncApp->translate("Decentralized (Inside synchronization folders)"), parent);
    fileMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum File Size: %1")).arg(formatSize((profile->fileMinSize()))), parent);
    fileMaxSizeAction = new QAction(QString("&" + syncApp->translate("Maximum File Size: %1")).arg(formatSize((profile->fileMaxSize()))), parent);
    movedFileMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum Size for a Moved File: %1")).arg(formatSize((profile->movedFileMinSize()))), parent);
    deltaCopyingMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum Size for Delta Copying: %1")).arg(formatSize((profile->deltaCopyingMinSize()))), parent);
    includeAction = new QAction(QString("&" + syncApp->translate("Include: %1")).arg(profile->includeList().join("; ")), parent);
    excludeAction = new QAction(QString("&" + syncApp->translate("Exclude: %1")).arg(profile->excludeList().join("; ")), parent);
    ignoreHiddenFilesAction = new QAction("&" + syncApp->translate("Ignore Hidden Files"), parent);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(profile->syncTimeMultiplier() <= 1);

    increaseSyncTimeAction->setEnabled(true);
    decreaseSyncTimeAction->setEnabled(profile->syncTimeMultiplier() > 1);

    manualAction->setCheckable(true);
    automaticAdaptiveAction->setCheckable(true);
    automaticFixedAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);
    deltaCopyingAction->setCheckable(true);
    deletePermanentlyAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    versioningAction->setCheckable(true);
    fileTimestampBeforeAction->setCheckable(true);
    fileTimestampAfterAction->setCheckable(true);
    folderTimestampAction->setCheckable(true);
    lastVersionAction->setCheckable(true);
    locallyNextToFolderAction->setCheckable(true);
    customLocationAction->setCheckable(true);
    databaseLocallyAction->setCheckable(true);
    databaseDecentralizedAction->setCheckable(true);
    ignoreHiddenFilesAction->setCheckable(true);

    syncingModeMenu = new UnhidableMenu("&" + syncApp->translate("Syncing Mode"), parent);
    syncingModeMenu->addAction(manualAction);
    syncingModeMenu->addAction(automaticAdaptiveAction);
    syncingModeMenu->addAction(automaticFixedAction);

    syncingModeMenu->addSeparator();
    syncingModeMenu->addAction(increaseSyncTimeAction);
    syncingModeMenu->addAction(syncingTimeAction);
    syncingModeMenu->addAction(decreaseSyncTimeAction);
    syncingModeMenu->addAction(fixedSyncingTimeAction);

    syncingModeMenu->addSeparator();
    syncingModeMenu->addAction(detectMovedFilesAction);
    syncingModeMenu->addAction(deltaCopyingAction);

    deletionModeMenu = new UnhidableMenu("&" + syncApp->translate("Deletion Mode"), parent);
    deletionModeMenu->addAction(moveToTrashAction);
    deletionModeMenu->addAction(versioningAction);
    deletionModeMenu->addAction(deletePermanentlyAction);

    versioningFormatMenu = new UnhidableMenu("&" + syncApp->translate("Versioning Format"), parent);
    versioningFormatMenu->addAction(fileTimestampBeforeAction);
    versioningFormatMenu->addAction(fileTimestampAfterAction);
    versioningFormatMenu->addAction(folderTimestampAction);
    versioningFormatMenu->addAction(lastVersionAction);
    versioningFormatMenu->addSeparator();
    versioningFormatMenu->addAction(versioningPostfixAction);
    versioningFormatMenu->addAction(versioningPatternAction);

    versioningLocationMenu = new UnhidableMenu("&" + syncApp->translate("Versioning Location"), parent);
    versioningLocationMenu->addAction(locallyNextToFolderAction);
    versioningLocationMenu->addAction(customLocationAction);
    versioningLocationMenu->addSeparator();
    versioningLocationMenu->addAction(customLocationPathAction);

    databaseLocationMenu = new UnhidableMenu("&" + syncApp->translate("Database Location"), parent);
    databaseLocationMenu->addAction(databaseLocallyAction);
    databaseLocationMenu->addAction(databaseDecentralizedAction);

    filteringMenu = new UnhidableMenu("&" + syncApp->translate("Filtering"), parent);
    filteringMenu->addAction(fileMinSizeAction);
    filteringMenu->addAction(fileMaxSizeAction);
    filteringMenu->addAction(movedFileMinSizeAction);
    filteringMenu->addAction(deltaCopyingMinSizeAction);
    filteringMenu->addAction(includeAction);
    filteringMenu->addAction(excludeAction);
    filteringMenu->addSeparator();
    filteringMenu->addAction(ignoreHiddenFilesAction);

    updateStates();

    connect(manualAction, &QAction::triggered, this, [this](){ switchSyncingMode(SyncProfile::Manual); });
    connect(deltaCopyingAction, &QAction::triggered, this, &ProfileMenu::setDeltaCopying);
    connect(automaticAdaptiveAction, &QAction::triggered, this, [this](){ switchSyncingMode(SyncProfile::AutomaticAdaptive); });
    connect(automaticFixedAction, &QAction::triggered, this, [this](){ switchSyncingMode(SyncProfile::AutomaticFixed); });
    connect(increaseSyncTimeAction, &QAction::triggered, this, [this](){ increaseSyncTime(); });
    connect(decreaseSyncTimeAction, &QAction::triggered, this, [this](){ decreaseSyncTime(); });
    connect(fixedSyncingTimeAction, &QAction::triggered, this, [this](){ setFixedInterval(); });
    connect(detectMovedFilesAction, &QAction::triggered, this, [this](){ toggleDetectMoved(); });
    connect(moveToTrashAction, &QAction::triggered, this, [this](){ switchDeletionMode(SyncProfile::MoveToTrash); });
    connect(versioningAction, &QAction::triggered, this, [this](){ switchDeletionMode(SyncProfile::Versioning); });
    connect(deletePermanentlyAction, &QAction::triggered, this, [this](){ switchDeletionMode(SyncProfile::DeletePermanently); });
    connect(fileTimestampBeforeAction, &QAction::triggered, this, [this](){ switchVersioningFormat(SyncProfile::FileTimestampBefore); });
    connect(fileTimestampAfterAction, &QAction::triggered, this, [this](){ switchVersioningFormat(SyncProfile::FileTimestampAfter); });
    connect(folderTimestampAction, &QAction::triggered, this, [this](){ switchVersioningFormat(SyncProfile::FolderTimestamp); });
    connect(lastVersionAction, &QAction::triggered, this, [this](){ switchVersioningFormat(SyncProfile::LastVersion); });
    connect(versioningPostfixAction, &QAction::triggered, this, [this](){ setVersioningPostfix(); });
    connect(versioningPatternAction, &QAction::triggered, this, [this](){ setVersioningPattern(); });
    connect(locallyNextToFolderAction, &QAction::triggered, this, [this](){ switchVersioningLocation(SyncProfile::LocallyNextToFolder); });
    connect(customLocationAction, &QAction::triggered, this, [this](){ switchVersioningLocation(SyncProfile::CustomLocation); });
    connect(customLocationPathAction, &QAction::triggered, this, [this](){ setVersioningLocationPath(); });
    connect(databaseLocallyAction, &QAction::triggered, this, [this](){ switchDatabaseLocation(SyncProfile::Locally); });
    connect(databaseDecentralizedAction, &QAction::triggered, this, [this](){ switchDatabaseLocation(SyncProfile::Decentralized); });
    connect(fileMinSizeAction, &QAction::triggered, this, [this](){ setFileMinSize(); });
    connect(fileMaxSizeAction, &QAction::triggered, this, [this](){ setFileMaxSize(); });
    connect(movedFileMinSizeAction, &QAction::triggered, this, [this](){ setMovedFileMinSize(); });
    connect(deltaCopyingMinSizeAction, &QAction::triggered, this, [this](){ setDeltaCopyingMinSize(); });
    connect(includeAction, &QAction::triggered, this, [this](){ setIncludeList(); });
    connect(excludeAction, &QAction::triggered, this, [this](){ setExcludeList(); });
    connect(ignoreHiddenFilesAction, &QAction::triggered, this, [this](){ toggleIgnoreHiddenFiles(); });

    // The simplest way to fix unclickable menu icons
    lower();
}


/*
===================
ProfileMenu::updateStates
===================
*/
void ProfileMenu::updateStates()
{
    manualAction->setChecked(profile->syncingMode() == SyncProfile::Manual);
    automaticAdaptiveAction->setChecked(profile->syncingMode() == SyncProfile::AutomaticAdaptive);
    automaticFixedAction->setChecked(profile->syncingMode() == SyncProfile::AutomaticFixed);
    detectMovedFilesAction->setChecked(profile->detectMovedFiles());
    deltaCopyingAction->setChecked(profile->deltaCopying());
    syncingTimeAction->setVisible(profile->syncingMode() == SyncProfile::AutomaticAdaptive);
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncEvery())));
    fixedSyncingTimeAction->setVisible(profile->syncingMode() == SyncProfile::AutomaticFixed);
    fixedSyncingTimeAction->setText("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncIntervalFixed())));
    moveToTrashAction->setChecked(profile->deletionMode() == SyncProfile::MoveToTrash);
    versioningAction->setChecked(profile->deletionMode() == SyncProfile::Versioning);
    deletePermanentlyAction->setChecked(profile->deletionMode() == SyncProfile::DeletePermanently);
    fileTimestampBeforeAction->setChecked(profile->versioningFormat() == SyncProfile::FileTimestampBefore);
    fileTimestampAfterAction->setChecked(profile->versioningFormat() == SyncProfile::FileTimestampAfter);
    folderTimestampAction->setChecked(profile->versioningFormat() == SyncProfile::FolderTimestamp);
    lastVersionAction->setChecked(profile->versioningFormat() == SyncProfile::LastVersion);
    versioningPostfixAction->setText(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(profile->versioningFolder()));
    versioningPatternAction->setText(QString("&" + syncApp->translate("Pattern: %1")).arg(profile->versioningPattern()));
    locallyNextToFolderAction->setChecked(profile->versioningLocation() == SyncProfile::LocallyNextToFolder);
    customLocationAction->setChecked(profile->versioningLocation() == SyncProfile::CustomLocation);
    customLocationPathAction->setText(syncApp->translate("Custom Location: ") + profile->versioningPath());
    databaseLocallyAction->setChecked(profile->databaseLocation() == SyncProfile::Locally);
    databaseDecentralizedAction->setChecked(profile->databaseLocation() == SyncProfile::Decentralized);
    fileMinSizeAction->setText("&" + syncApp->translate("Minimum File Size: %1").arg(formatSize(profile->fileMinSize())));
    fileMaxSizeAction->setText("&" + syncApp->translate("Maximum File Size: %1").arg(formatSize(profile->fileMaxSize())));
    movedFileMinSizeAction->setText("&" + syncApp->translate("Minimum Size for a Moved File: %1").arg(formatSize(profile->movedFileMinSize())));
    deltaCopyingMinSizeAction->setVisible(profile->deltaCopying());
    deltaCopyingMinSizeAction->setText("&" + syncApp->translate("Minimum Size for Delta Copying: %1").arg(formatSize(profile->deltaCopyingMinSize())));
    includeAction->setText("&" + syncApp->translate("Include: %1").arg(profile->includeList().join("; ")));
    excludeAction->setText("&" + syncApp->translate("Exclude: %1").arg(profile->excludeList().join("; ")));
    ignoreHiddenFilesAction->setChecked(profile->ignoreHiddenFiles());

    versioningFormatMenu->setVisible(profile->deletionMode() == SyncProfile::Versioning);
    versioningLocationMenu->setVisible(profile->deletionMode() == SyncProfile::Versioning);
    filteringMenu->setVisible(profile->deletionMode() == SyncProfile::Versioning);
}

/*
===================
ProfileMenu::retranslate
===================
*/
void ProfileMenu::retranslate()
{
    manualAction->setText("&" + syncApp->translate("Manual"));
    automaticAdaptiveAction->setText("&" + syncApp->translate("Automatic (Adaptive)"));
    automaticFixedAction->setText("&" + syncApp->translate("Automatic (Fixed)"));
    detectMovedFilesAction->setText("&" + syncApp->translate("Detect Renamed and Moved Files"));
    deltaCopyingAction->setText("&" + syncApp->translate("File Delta Copying") + " (Beta)");
    increaseSyncTimeAction->setText("&" + syncApp->translate("Increase"));
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncEvery())));
    decreaseSyncTimeAction->setText("&" + syncApp->translate("Decrease"));
    fixedSyncingTimeAction->setText("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(profile->syncIntervalFixed())));
    moveToTrashAction->setText("&" + syncApp->translate("Move Files to Trash"));
    versioningAction->setText("&" + syncApp->translate("Versioning"));
    deletePermanentlyAction->setText("&" + syncApp->translate("Delete Files Permanently"));
    fileTimestampBeforeAction->setText("&" + syncApp->translate("File Timestamp (Before Extension)"));
    fileTimestampAfterAction->setText("&" + syncApp->translate("File Timestamp (After Extension)"));
    folderTimestampAction->setText("&" + syncApp->translate("Folder Timestamp"));
    lastVersionAction->setText("&" + syncApp->translate("Last Version"));
    versioningPostfixAction->setText(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(profile->versioningFolder()));
    versioningPatternAction->setText(QString("&" + syncApp->translate("Pattern: %1")).arg(profile->versioningPattern()));
    locallyNextToFolderAction->setText("&" + syncApp->translate("Locally Next to Folder"));
    customLocationAction->setText("&" + syncApp->translate("Custom Location"));
    customLocationPathAction->setText(syncApp->translate("Custom Location: ") + profile->versioningPath());
    databaseLocallyAction->setText("&" + syncApp->translate("Locally (On the local machine)"));
    databaseDecentralizedAction->setText("&" + syncApp->translate("Decentralized (Inside synchronization folders)"));
    fileMinSizeAction->setText(QString("&" + syncApp->translate("Minimum File Size: %1")).arg(formatSize(profile->fileMinSize())));
    fileMaxSizeAction->setText(QString("&" + syncApp->translate("Maximum File Size: %1")).arg(formatSize(profile->fileMaxSize())));
    movedFileMinSizeAction->setText(QString("&" + syncApp->translate("Minimum Size for a Moved File: %1")).arg(formatSize(profile->movedFileMinSize())));
    deltaCopyingMinSizeAction->setText(QString("&" + syncApp->translate("Minimum Size for Delta Copying: %1")).arg(formatSize(profile->deltaCopyingMinSize())));
    includeAction->setText(QString("&" + syncApp->translate("Include: %1")).arg(profile->includeList().join("; ")));
    excludeAction->setText(QString("&" + syncApp->translate("Exclude: %1")).arg(profile->excludeList().join("; ")));
    ignoreHiddenFilesAction->setText("&" + syncApp->translate("Ignore Hidden Files"));
    syncingModeMenu->setTitle("&" + syncApp->translate("Syncing Mode"));
    deletionModeMenu->setTitle("&" + syncApp->translate("Deletion Mode"));
    versioningFormatMenu->setTitle("&" + syncApp->translate("Versioning Format"));
    versioningLocationMenu->setTitle("&" + syncApp->translate("Versioning Location"));
    databaseLocationMenu->setTitle("&" + syncApp->translate("Database Location"));
    filteringMenu->setTitle("&" + syncApp->translate("Filtering"));
}

/*
===================
ProfileMenu::exportMenu
===================
*/
void ProfileMenu::exportMenu(QMenu *menu)
{
    menu->addMenu(syncingModeMenu);
    menu->addMenu(deletionModeMenu);
    menu->addMenu(versioningFormatMenu);
    menu->addMenu(versioningLocationMenu);
    menu->addMenu(databaseLocationMenu);
    menu->addMenu(filteringMenu);
}

/*
===================
ProfileMenu::enable
===================
*/
void ProfileMenu::enable(bool enable)
{
    for (auto &action : syncingModeMenu->actions())
        action->setEnabled(enable);

    for (auto &action : deletionModeMenu->actions())
        action->setEnabled(enable);

    for (auto &action : versioningFormatMenu->actions())
        action->setEnabled(enable);

    for (auto &action : versioningLocationMenu->actions())
        action->setEnabled(enable);

    for (auto &action : databaseLocationMenu->actions())
        action->setEnabled(enable);

    for (auto &action : filteringMenu->actions())
        action->setEnabled(enable);
}

/*
===================
ProfileMenu::updateSyncTime
===================
*/
void ProfileMenu::updateSyncTime()
{
    quint64 time = 0;
    QAction *action = nullptr;

    if (profile->syncingMode() == SyncProfile::AutomaticAdaptive)
    {
        time = profile->syncEvery();
        action = syncingTimeAction;
    }
    else if (profile->syncingMode() == SyncProfile::AutomaticFixed)
    {
        time = profile->syncIntervalFixed();
        action = fixedSyncingTimeAction;
    }
    else
    {
        return;
    }

    action->setText(QString(tr("Synchronize Every") + ": ").append(formatTime(time)));

    // If exceeds the maximum value of an quint64
    if (time >= SyncManager::maxInterval())
        increaseSyncTimeAction->setEnabled(false);

    if (profile->syncTimeMultiplier() <= 1)
        decreaseSyncTimeAction->setEnabled(false);
}

/*
===================
ProfileMenu::switchSyncingMode
===================
*/
void ProfileMenu::switchSyncingMode(SyncProfile::SyncingMode mode)
{
    if (mode < SyncProfile::Manual || mode > SyncProfile::AutomaticFixed)
        mode = SyncProfile::AutomaticAdaptive;

    manualAction->setChecked(mode == SyncProfile::Manual);
    automaticAdaptiveAction->setChecked(mode == SyncProfile::AutomaticAdaptive);
    automaticFixedAction->setChecked(mode == SyncProfile::AutomaticFixed);
    increaseSyncTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    syncingTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    decreaseSyncTimeAction->setVisible(mode == SyncProfile::AutomaticAdaptive);
    fixedSyncingTimeAction->setVisible(mode == SyncProfile::AutomaticFixed);
    profile->setSyncingMode(mode);
}

/*
===================
ProfileMenu::setDeltaCopying
===================
*/
void ProfileMenu::setDeltaCopying(bool enable)
{
    profile->setDeltaCopying(enable);

    if (!syncApp->initiated())
        return;

    if (profile->deltaCopying())
    {
        QString title(syncApp->translate("Enable file delta copying?"));
        QString text(syncApp->translate("Are you sure? Beware: files will be overwritten, and there's no way to bring the previous versions back."));

        if (!syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
        {
            setDeltaCopying(false);
            deltaCopyingAction->setChecked(false);
        }
    }

    deltaCopyingMinSizeAction->setVisible(profile->deltaCopying());
}

/*
===================
ProfileMenu::increaseSyncTime
===================
*/
void ProfileMenu::increaseSyncTime()
{
    quint64 max = SyncManager::maxInterval();

    // If exceeds the maximum value of an qint64
    if (profile->syncEvery() >= max)
    {
        updateSyncTime();
        return;
    }

    profile->setSyncTimeMultiplier(profile->syncTimeMultiplier() + 1);
    decreaseSyncTimeAction->setEnabled(true);
}

/*
===================
ProfileMenu::decreaseSyncTime
===================
*/
void ProfileMenu::decreaseSyncTime()
{
    profile->setSyncTimeMultiplier(profile->syncTimeMultiplier() - 1);
    updateSyncTime();
}

/*
===================
ProfileMenu::setFixedInterval
===================
*/
void ProfileMenu::setFixedInterval()
{
    QString title(tr("Synchronize Every"));
    QString text(tr("Please enter the synchronization interval in seconds:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile->syncIntervalFixed() / 1000, 0))
        return;

    profile->setSyncIntervalFixed(size * 1000);
    updateSyncTime();
}

/*
===================
ProfileMenu::detectMovedFiles
===================
*/
void ProfileMenu::toggleDetectMoved()
{
    profile->setDetectMovedFiles(!profile->detectMovedFiles());
}

/*
===================
ProfileMenu::switchDeletionMode
===================
*/
void ProfileMenu::switchDeletionMode(SyncProfile::DeletionMode mode)
{
    if (mode < SyncProfile::MoveToTrash || mode > SyncProfile::DeletePermanently)
        mode = SyncProfile::MoveToTrash;

    if (syncApp->initiated() && mode == SyncProfile::DeletePermanently && mode != profile->deletionMode())
    {
        QString title(tr("Switch deletion mode to delete files permanently?"));
        QString text(tr("Are you sure? Beware: this could lead to data loss!"));

        if (!syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, this))
            mode = profile->deletionMode();
    }

    moveToTrashAction->setChecked(mode == SyncProfile::MoveToTrash);
    versioningAction->setChecked(mode == SyncProfile::Versioning);
    deletePermanentlyAction->setChecked(mode == SyncProfile::DeletePermanently);
    versioningFormatMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);
    versioningLocationMenu->menuAction()->setVisible(mode == SyncProfile::Versioning);
    profile->setDeletionMode(mode);
}

/*
===================
ProfileMenu::switchVersioningFormat
===================
*/
void ProfileMenu::switchVersioningFormat(SyncProfile::VersioningFormat format)
{
    if (format < SyncProfile::FileTimestampBefore || format > SyncProfile::LastVersion)
        format = SyncProfile::FileTimestampAfter;

    fileTimestampBeforeAction->setChecked(format == SyncProfile::FileTimestampBefore);
    fileTimestampAfterAction->setChecked(format == SyncProfile::FileTimestampAfter);
    folderTimestampAction->setChecked(format == SyncProfile::FolderTimestamp);
    lastVersionAction->setChecked(format == SyncProfile::LastVersion);
    profile->setVersioningFormat(format);
}

/*
===================
ProfileMenu::setVersioningPostfix
===================
*/
void ProfileMenu::setVersioningPostfix()
{
    QString postfix = profile->versioningFolder();
    QString title(tr("Versioning Folder Postfix"));
    QString text(tr("Please enter the versioning folder postfix:"));

    if (!syncApp->textInputDialog(this, title, text, postfix, postfix))
        return;

    profile->setVersioningFolder(postfix);
    versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(profile->versioningFolder()));
}

/*
===================
ProfileMenu::setVersioningPattern
===================
*/
void ProfileMenu::setVersioningPattern()
{
    QString pattern = profile->versioningPattern();
    QString title(tr("Versioning Pattern"));
    QString text(tr("Please enter the versioning pattern:"));
    text.append("\n\n");
    text.append(tr("Examples:"));
    text.append("\nyyyy_M_d_h_m_s_z - 2001_5_21_14_13_09_120");
    text.append("\nyyyy_MM_dd - 2001_05_21");
    text.append("\nyy_MMMM_d - 01_May_21");
    text.append("\nhh_mm_ss_zzz - 14_13_09_120");
    text.append("\nap_h_m_s - pm_2_13_9");

    if (!syncApp->textInputDialog(this, title, text, pattern, pattern))
        return;

    profile->setVersioningPattern(pattern);
    versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(profile->versioningPattern()));
}

/*
===================
ProfileMenu::switchVersioningLocation
===================
*/
void ProfileMenu::switchVersioningLocation(SyncProfile::VersioningLocation location)
{
    if (location < SyncProfile::LocallyNextToFolder || location > SyncProfile::CustomLocation)
        location = SyncProfile::LocallyNextToFolder;

    versioningPostfixAction->setVisible(location == SyncProfile::LocallyNextToFolder);
    locallyNextToFolderAction->setChecked(location == SyncProfile::LocallyNextToFolder);
    customLocationAction->setChecked(location == SyncProfile::CustomLocation);
    customLocationPathAction->setVisible(location == SyncProfile::CustomLocation);
    profile->setVersioningLocation(location);
}

/*
===================
ProfileMenu::setVersioningLocationPath
===================
*/
void ProfileMenu::setVersioningLocationPath()
{
    if (profile->versioningLocation() != SyncProfile::CustomLocation || !syncApp->initiated())
        return;

    QString title(tr("Browse for Versioning Folder"));
    QString dir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    QString path = QFileDialog::getExistingDirectory(this, title, dir, QFileDialog::ShowDirsOnly);

    if (path.isEmpty())
        return;

    profile->setVersioningPath(path);
    customLocationPathAction->setText(tr("Custom Location: ") + path);
}

/*
===================
ProfileMenu::switchDatabaseLocation
===================
*/
void ProfileMenu::switchDatabaseLocation(SyncProfile::DatabaseLocation location)
{
    if (location < SyncProfile::Locally || location > SyncProfile::Decentralized)
        location = SyncProfile::Decentralized;

    databaseLocallyAction->setChecked(location == SyncProfile::Locally);
    databaseDecentralizedAction->setChecked(location == SyncProfile::Decentralized);
    profile->setDatabaseLocation(location);
}

/*
===================
ProfileMenu::setFileMinSize
===================
*/
void ProfileMenu::setFileMinSize()
{
    QString title(tr("Minimum File Size"));
    QString text(tr("Please enter the minimum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile->fileMinSize(), 0))
        return;

    if (size && profile->fileMaxSize() && static_cast<quint64>(size) > profile->fileMaxSize())
        size = profile->fileMaxSize();

    profile->setFileMinSize(size);
    fileMinSizeAction->setText("&" + tr("Minimum File Size: %1").arg(formatSize(profile->fileMinSize())));
}

/*
===================
ProfileMenu::setFileMaxSize
===================
*/
void ProfileMenu::setFileMaxSize()
{
    QString title(tr("Maximum File Size"));
    QString text(tr("Please enter the maximum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile->fileMaxSize(), 0))
        return;

    if (size && static_cast<quint64>(size) < profile->fileMinSize())
        size = profile->fileMinSize();

    profile->setFileMaxSize(size);
    fileMaxSizeAction->setText("&" + tr("Maximum File Size: %1").arg(formatSize(profile->fileMaxSize())));
}

/*
===================
ProfileMenu::setMovedFileMinSize
===================
*/
void ProfileMenu::setMovedFileMinSize()
{
    QString title(tr("Minimum Size for Moved File"));
    QString text(tr("Please enter the minimum size for a moved file in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile->movedFileMinSize(), 0))
        return;

    profile->setMovedFileMinSize(size);
    movedFileMinSizeAction->setText("&" + tr("Minimum Size for a Moved File: %1").arg(formatSize(profile->movedFileMinSize())));
}

/*
===================
ProfileMenu::setDeltaCopyingMinSize
===================
*/
void ProfileMenu::setDeltaCopyingMinSize()
{
    QString title(tr("Minimum Size for Delta Copying"));
    QString text(tr("Please enter the minimum size for delta copying in bytes:"));
    int size;

    if (!syncApp->intInputDialog(this, title, text, size, profile->deltaCopyingMinSize(), 0))
        return;

    profile->setDeltaCopyingMinSize(size);
    deltaCopyingMinSizeAction->setText("&" + tr("Minimum Size for delta copying: %1").arg(formatSize(profile->deltaCopyingMinSize())));
}

/*
===================
ProfileMenu::setIncludeList
===================
*/
void ProfileMenu::setIncludeList()
{
    QString includeString = profile->includeList().join("; ");
    QString title(tr("Include List"));
    QString text(tr("Please enter include list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(this, title, text, includeString, includeString))
        return;

    QStringList includeList = includeString.split(";");

    for (auto &include : includeList)
        include = include.trimmed();

    includeString = includeList.join("; ");
    profile->setIncludeList(includeList);
    includeAction->setText("&" + tr("Include: %1").arg(includeString));
}

/*
===================
ProfileMenu::setExcludeList
===================
*/
void ProfileMenu::setExcludeList()
{
    QString excludeString = profile->excludeList().join("; ");
    QString title(tr("Exclude List"));
    QString text(tr("Please enter exclude list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(this, title, text, excludeString, excludeString))
        return;

    QStringList excludeList = excludeString.split(";");

    for (auto &exclude : excludeList)
        exclude = exclude.trimmed();

    excludeString = excludeList.join("; ");
    profile->setExcludeList(excludeList);
    excludeAction->setText("&" + tr("Exclude: %1").arg(excludeString));
}

/*
===================
ProfileMenu::toggleIgnoreHiddenFiles
===================
*/
void ProfileMenu::toggleIgnoreHiddenFiles()
{
    profile->setIgnoreHiddenFiles(!profile->ignoreHiddenFiles());
}
