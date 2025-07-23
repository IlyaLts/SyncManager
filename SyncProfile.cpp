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

#include "Application.h"
#include "SyncManager.h"
#include "SyncProfile.h"
#include "SyncFolder.h"
#include "UnhidableMenu.h"
#include <QMutex>
#include <QStandardPaths>
#include <QModelIndex>
#include <QAction>
#include <QSettings>

/*
===================
SyncProfile::SyncProfile
===================
*/
SyncProfile::SyncProfile(const QString &name, const QModelIndex &index)
{
    this->index = index;
    this->name = name;
    syncTimer.setSingleShot(true);
    syncTimer.setTimerType(Qt::VeryCoarseTimer);

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString keyName(name + QLatin1String("_profile/"));

    paused = settings.value(keyName + QLatin1String("Paused"), false).toBool();
    m_detectMovedFiles = settings.value(keyName + "DetectMovedFiles", true).toBool();
    m_databaseLocation = static_cast<SyncProfile::DatabaseLocation>(settings.value(keyName + "DatabaseLocation", SyncProfile::Decentralized).toInt());
    m_syncTimeMultiplier = settings.value(keyName + "SyncTimeMultiplier", 1).toInt();
    if (m_syncTimeMultiplier <= 0) m_syncTimeMultiplier = 1;
    m_versioningFolder = settings.value(keyName + "VersionFolder", "[Deletions]").toString();
    m_versioningPattern = settings.value(keyName + "VersionPattern", "yyyy_M_d_h_m_s_z").toString();
    m_versioningPath = settings.value(keyName + "VersioningPath", "").toString();
    m_fileMinSize = settings.value(keyName + "FileMinSize", 0).toInt();
    m_fileMaxSize = settings.value(keyName + "FileMaxSize", 0).toInt();
    m_movedFileMinSize = settings.value(keyName + "MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt();
    m_includeList = settings.value(keyName + "IncludeList").toStringList();
    m_excludeList = settings.value(keyName + "ExcludeList").toStringList();
    m_ignoreHiddenFiles = settings.value(keyName + "IgnoreHiddenFiles", true).toBool();
}

/*
===================
SyncProfile::operator =
===================
*/
void SyncProfile::operator =(const SyncProfile &other)
{
    folders = other.folders;

    index = other.index;
    m_syncingMode = other.m_syncingMode;
    m_deletionMode = other.m_deletionMode;
    m_versioningLocation = other.m_versioningLocation;
    m_versioningFormat = other.m_versioningFormat;
    m_versioningFolder = other.m_versioningFolder;
    m_versioningPattern = other.m_versioningPattern;
    m_versioningPath = other.m_versioningPath;
    m_databaseLocation = other.m_databaseLocation;
    m_fileMinSize = other.m_fileMinSize;
    m_fileMaxSize = other.m_fileMaxSize;
    m_movedFileMinSize = other.m_movedFileMinSize;
    m_includeList = other.m_includeList;
    m_excludeList = other.m_excludeList;
    m_syncTimeMultiplier = other.m_syncTimeMultiplier;
    m_ignoreHiddenFiles = other.m_ignoreHiddenFiles;
    m_detectMovedFiles = other.m_detectMovedFiles;

    syncing = other.syncing;
    paused = other.paused;
    toBeRemoved = other.toBeRemoved;
    syncEvery = other.syncEvery;
    syncTime = other.syncTime;
    lastSyncDate = other.lastSyncDate;
    name = other.name;
    index = other.index;
}

/*
===================
SyncProfile::resetLocks
===================
*/
bool SyncProfile::resetLocks()
{
    QSet<hash64_t> fileHashes;
    QSet<hash64_t> folderHashes;

    for (auto &folder : folders)
    {
        for (QHash<Hash, FileToMoveInfo>::iterator it = folder.filesToMove.begin(); it != folder.filesToMove.end(); ++it)
        {
            fileHashes.insert(hash64(it.value().fromPath));
            fileHashes.insert(it.key().data);
        }

        for (QHash<Hash, FolderToRenameInfo>::iterator it = folder.foldersToRename.begin(); it != folder.foldersToRename.end(); ++it)
        {
            folderHashes.insert(it.key().data);
            folderHashes.insert(hash64(it.value().toPath));
        }
    }

    bool databaseChanged = false;

    for (auto &folder : folders)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folder.files.begin(); fileIt != folder.files.end(); ++fileIt)
        {
            if (fileIt->lockedFlag == SyncFile::Unlocked)
                continue;

            if (fileIt->type == SyncFile::File && fileHashes.contains(fileIt.key().data))
                continue;

            if (fileIt->type == SyncFile::Folder && folderHashes.contains(fileIt.key().data))
                continue;

            if (folder.files.contains(fileIt.key()))
            {
                databaseChanged = true;
                folder.files[fileIt.key()].lockedFlag = SyncFile::Unlocked;
            }
        }
    }

    return databaseChanged;
}

/*
===================
SyncProfile::removeInvalidFileData

If a file doesn't have a path after getListOfFiles(), then that means that the file doesn't exist at all.
So, it is better to remove it from the database to prevent further synchronization issues.
===================
*/
void SyncProfile::removeInvalidFileData()
{
    for (auto &folder : folders)
    {
        for (QHash<Hash, SyncFile>::iterator fileIt = folder.files.begin(); fileIt != folder.files.end();)
        {
            if (!hasFilePath(fileIt.key()))
                fileIt = folder.files.erase(static_cast<QHash<Hash, SyncFile>::const_iterator>(fileIt));
            else
                ++fileIt;
        }
    }
}

/*
===================
SyncProfile::saveDatabasesLocally
===================
*/
void SyncProfile::saveDatabasesLocally() const
{
    for (auto &folder : folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path)) + ".db");
        folder.saveToDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncProfile::saveDatabasesDecentralised
===================
*/
void SyncProfile::saveDatabasesDecentralised() const
{
    for (auto &folder : folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QDir().mkdir(folder.path + DATA_FOLDER_PATH);

        if (!QDir(folder.path + DATA_FOLDER_PATH).exists())
            continue;

        folder.saveToDatabase(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);

#ifdef Q_OS_WIN
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH), true);
        setHiddenFileAttribute(QString(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
#endif
    }
}

/*
===================
SyncProfile::loadDatabasesLocally
===================
*/
void SyncProfile::loadDatabasesLocally()
{
    for (auto &folder : folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path)) + ".db");
        folder.loadFromDatabase(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + filename);
    }
}

/*
===================
SyncProfile::loadDatebasesDecentralised
===================
*/
void SyncProfile::loadDatebasesDecentralised()
{
    for (auto &folder : folders)
    {
        if (!folder.isActive() || folder.toBeRemoved)
            continue;

        folder.loadFromDatabase(folder.path + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
    }
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    mutex.lock();

    if (!filePaths.contains(Hash(hash)))
    {
        auto it = filePaths.insert(Hash(hash), path);
        it->squeeze();
    }

    mutex.unlock();
}

/*
===================
SyncProfile::isActive
===================
*/
bool SyncProfile::isActive() const
{
    int activeFolders = 0;

    for (auto &folder : folders)
        if (folder.isActive())
            activeFolders++;

    return !paused && !toBeRemoved && activeFolders >= 2;
}

/*
===================
SyncProfile::isTopFolderUpdated
===================
*/
bool SyncProfile::isTopFolderUpdated(const SyncFolder &folder, hash64_t hash) const
{
    QByteArray path = filePath(hash);
    return folder.files.value(hash64(QByteArray(path).remove(path.indexOf('/'), path.size()))).updated();
}

/*
===================
SyncProfile::isAnyFolderCaseSensitive
===================
*/
bool SyncProfile::isAnyFolderCaseSensitive() const
{
    for (auto &folder : folders)
        if (folder.caseSensitive)
            return true;

    return false;
}

/*
===================
SyncProfile::hasExistingFolders
===================
*/
bool SyncProfile::hasExistingFolders() const
{
    for (const auto &folder : folders)
        if (folder.exists)
            return true;

    return false;
}

/*
===================
SyncProfile::hasMissingFolders
===================
*/
bool SyncProfile::hasMissingFolders() const
{
    for (const auto &folder : folders)
        if (!folder.exists)
            return true;

    return false;
}

/*
===================
SyncProfile::folderByIndex
===================
*/
SyncFolder *SyncProfile::folderByIndex(QModelIndex index)
{
    for (auto &folder : folders)
        if (folder.path == index.data(Qt::DisplayRole).toString())
            return &folder;

    return nullptr;
}

/*
===================
SyncProfile::folderByPath
===================
*/
SyncFolder *SyncProfile::folderByPath(const QString &path)
{
    for (auto &folder : folders)
        if (folder.path == path)
            return &folder;

    return nullptr;
}

/*
===================
SyncProfile::setupMenus
===================
*/
void SyncProfile::setupMenus(QWidget *parent)
{
    automaticAction = new QAction("&" + tr("Automatic"), parent);
    manualAction = new QAction("&" + tr("Manual"), parent);
    detectMovedFilesAction = new QAction("&" + tr("Detect Renamed and Moved Files"), parent);
    increaseSyncTimeAction = new QAction("&" + tr("Increase"), parent);
    syncingTimeAction = new QAction(tr("Synchronize Every: "), parent);
    decreaseSyncTimeAction = new QAction("&" + tr("Decrease"), parent);
    moveToTrashAction = new QAction("&" + tr("Move Files to Trash"), parent);
    versioningAction = new QAction("&" + tr("Versioning"), parent);
    deletePermanentlyAction = new QAction("&" + tr("Delete Files Permanently"), parent);
    fileTimestampBeforeAction = new QAction("&" + tr("File Timestamp (Before Extension)"), parent);
    fileTimestampAfterAction = new QAction("&" + tr("File Timestamp (After Extension)"), parent);
    folderTimestampAction = new QAction("&" + tr("Folder Timestamp"), parent);
    lastVersionAction = new QAction("&" + tr("Last Version"), parent);
    versioningPostfixAction = new QAction(QString("&" + tr("Folder Postfix: %1")).arg(m_versioningFolder), parent);
    versioningPatternAction = new QAction(QString("&" + tr("Pattern: %1")).arg(m_versioningPattern), parent);
    locallyNextToFolderAction = new QAction("&" + tr("Locally Next to Folder"), parent);
    customLocationAction = new QAction("&" + tr("Custom Location"), parent);
    customLocationPathAction = new QAction(tr("Custom Location: ") + m_versioningPath, parent);
    saveDatabaseLocallyAction = new QAction("&" + tr("Locally (On the local machine)"), parent);
    saveDatabaseDecentralizedAction = new QAction("&" + tr("Decentralized (Inside synchronization folders)"), parent);
    fileMinSizeAction = new QAction(QString("&" + tr("Minimum File Size: %1 bytes")).arg(m_fileMinSize), parent);
    fileMaxSizeAction = new QAction(QString("&" + tr("Maximum File Size: %1 bytes")).arg(m_fileMaxSize), parent);
    movedFileMinSizeAction = new QAction(QString("&" + tr("Minimum Size for a Moved File: %1 bytes")).arg(m_movedFileMinSize), parent);
    includeAction = new QAction(QString("&" + tr("Include: %1")).arg(m_includeList.join("; ")), parent);
    excludeAction = new QAction(QString("&" + tr("Exclude: %1")).arg(m_excludeList.join("; ")), parent);
    ignoreHiddenFilesAction = new QAction("&" + tr("Ignore Hidden Files"), parent);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(m_syncTimeMultiplier <= 1);

    increaseSyncTimeAction->setEnabled(true);
    decreaseSyncTimeAction->setEnabled(m_syncTimeMultiplier > 1);

    automaticAction->setCheckable(true);
    manualAction->setCheckable(true);
    detectMovedFilesAction->setCheckable(true);
    deletePermanentlyAction->setCheckable(true);
    moveToTrashAction->setCheckable(true);
    versioningAction->setCheckable(true);
    fileTimestampBeforeAction->setCheckable(true);
    fileTimestampAfterAction->setCheckable(true);
    folderTimestampAction->setCheckable(true);
    lastVersionAction->setCheckable(true);
    locallyNextToFolderAction->setCheckable(true);
    customLocationAction->setCheckable(true);
    customLocationPathAction->setDisabled(true);
    saveDatabaseLocallyAction->setCheckable(true);
    saveDatabaseDecentralizedAction->setCheckable(true);
    ignoreHiddenFilesAction->setCheckable(true);

    syncingModeMenu = new UnhidableMenu("&" + tr("Syncing Mode"), parent);
    syncingModeMenu->addAction(automaticAction);
    syncingModeMenu->addAction(manualAction);
    syncingModeMenu->addSeparator();
    syncingModeMenu->addAction(detectMovedFilesAction);

    syncingTimeMenu = new UnhidableMenu("&" + tr("Syncing Time"), parent);
    syncingTimeMenu->addAction(increaseSyncTimeAction);
    syncingTimeMenu->addAction(syncingTimeAction);
    syncingTimeMenu->addAction(decreaseSyncTimeAction);

    deletionModeMenu = new UnhidableMenu("&" + tr("Deletion Mode"), parent);
    deletionModeMenu->addAction(moveToTrashAction);
    deletionModeMenu->addAction(versioningAction);
    deletionModeMenu->addAction(deletePermanentlyAction);

    versioningFormatMenu = new UnhidableMenu("&" + tr("Versioning Format"), parent);
    versioningFormatMenu->addAction(fileTimestampBeforeAction);
    versioningFormatMenu->addAction(fileTimestampAfterAction);
    versioningFormatMenu->addAction(folderTimestampAction);
    versioningFormatMenu->addAction(lastVersionAction);
    versioningFormatMenu->addSeparator();
    versioningFormatMenu->addAction(versioningPostfixAction);
    versioningFormatMenu->addAction(versioningPatternAction);

    versioningLocationMenu = new UnhidableMenu("&" + tr("Versioning Location"), parent);
    versioningLocationMenu->addAction(locallyNextToFolderAction);
    versioningLocationMenu->addAction(customLocationAction);
    versioningLocationMenu->addSeparator();
    versioningLocationMenu->addAction(customLocationPathAction);

    databaseLocationMenu = new UnhidableMenu("&" + tr("Database Location"), parent);
    databaseLocationMenu->addAction(saveDatabaseLocallyAction);
    databaseLocationMenu->addAction(saveDatabaseDecentralizedAction);

    filteringMenu = new UnhidableMenu("&" + tr("Filtering"), parent);
    filteringMenu->addAction(fileMinSizeAction);
    filteringMenu->addAction(fileMaxSizeAction);
    filteringMenu->addAction(movedFileMinSizeAction);
    filteringMenu->addAction(includeAction);
    filteringMenu->addAction(excludeAction);
    filteringMenu->addSeparator();
    filteringMenu->addAction(ignoreHiddenFilesAction);

    detectMovedFilesAction->setChecked(detectMovedFilesEnabled());
    databaseLocationMenu->setEnabled(databaseLocation());
    ignoreHiddenFilesAction->setChecked(ignoreHiddenFilesEnabled());
}

/*
===================
SyncProfile::saveSettings
===================
*/
void SyncProfile::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString profileKeyname(name + QLatin1String("_profile/"));

    settings.setValue(profileKeyname + "SyncingMode", syncingMode());
    settings.setValue(profileKeyname + "DeletionMode", deletionMode());
    settings.setValue(profileKeyname + "VersioningFormat", versioningFormat());
    settings.setValue(profileKeyname + "VersioningLocation", versioningLocation());
    settings.setValue(profileKeyname + "VersioningPath", versioningPath());
    settings.setValue(profileKeyname + "DatabaseLocation", databaseLocation());
    settings.setValue(profileKeyname + "IgnoreHiddenFiles", ignoreHiddenFilesEnabled());
    settings.setValue(profileKeyname + "DetectMovedFiles", detectMovedFilesEnabled());
    settings.setValue(profileKeyname + "SyncTimeMultiplier", syncTimeMultiplier());
    settings.setValue(profileKeyname + "FileMinSize", fileMinSize());
    settings.setValue(profileKeyname + "FileMaxSize", fileMaxSize());
    settings.setValue(profileKeyname + "MovedFileMinSize", movedFileMinSize());
    settings.setValue(profileKeyname + "IncludeList", includeList());
    settings.setValue(profileKeyname + "ExcludeList", excludeList());
    settings.setValue(profileKeyname + "VersionFolder", versioningFolder());
    settings.setValue(profileKeyname + "VersionPattern", versioningPattern());
}

/*
===================
SyncProfile::updateStrings
===================
*/
void SyncProfile::updateStrings()
{
    automaticAction->setText("&" + tr("Automatic"));
    manualAction->setText("&" + tr("Manual"));
    detectMovedFilesAction->setText("&" + tr("Detect Renamed and Moved Files"));
    increaseSyncTimeAction->setText("&" + tr("Increase"));
    syncingTimeAction->setText(tr("Synchronize Every:"));
    decreaseSyncTimeAction->setText("&" + tr("Decrease"));
    moveToTrashAction->setText("&" + tr("Move Files to Trash"));
    versioningAction->setText("&" + tr("Versioning"));
    deletePermanentlyAction->setText("&" + tr("Delete Files Permanently"));
    fileTimestampBeforeAction->setText("&" + tr("File Timestamp (Before Extension)"));
    fileTimestampAfterAction->setText("&" + tr("File Timestamp (After Extension)"));
    folderTimestampAction->setText("&" + tr("Folder Timestamp"));
    lastVersionAction->setText("&" + tr("Last Version"));
    versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(versioningFolder()));
    versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(versioningPattern()));
    locallyNextToFolderAction->setText("&" + tr("Locally Next to Folder"));
    customLocationAction->setText("&" + tr("Custom Location"));
    customLocationPathAction->setText(tr("Custom Location: ") + versioningPath());
    saveDatabaseLocallyAction->setText("&" + tr("Locally (On the local machine)"));
    saveDatabaseDecentralizedAction->setText("&" + tr("Decentralized (Inside synchronization folders)"));
    fileMinSizeAction->setText(QString("&" + tr("Minimum File Size: %1 bytes")).arg(fileMinSize()));
    fileMaxSizeAction->setText(QString("&" + tr("Maximum File Size: %1 bytes")).arg(fileMaxSize()));
    movedFileMinSizeAction->setText(QString("&" + tr("Minimum Size for a Moved File: %1 bytes")).arg(movedFileMinSize()));
    includeAction->setText(QString("&" + tr("Include: %1")).arg(includeList().join("; ")));
    excludeAction->setText(QString("&" + tr("Exclude: %1")).arg(excludeList().join("; ")));
    ignoreHiddenFilesAction->setText("&" + tr("Ignore Hidden Files"));
    syncingModeMenu->setTitle("&" + tr("Syncing Mode"));
    syncingTimeMenu->setTitle("&" + tr("Syncing Time"));
    deletionModeMenu->setTitle("&" + tr("Deletion Mode"));
    versioningFormatMenu->setTitle("&" + tr("Versioning Format"));
    versioningLocationMenu->setTitle("&" + tr("Versioning Location"));
    databaseLocationMenu->setTitle("&" + tr("Database Location"));
    filteringMenu->setTitle("&" + tr("Filtering"));
}
