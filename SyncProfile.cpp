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
SyncProfile::setSyncingMode
===================
*/
void SyncProfile::setSyncingMode(SyncingMode mode)
{
    if (mode < Manual || mode > AutomaticFixed)
        mode = AutomaticAdaptive;

    m_syncingMode = mode;
}

/*
===================
SyncProfile::setDeletionMode
===================
*/
void SyncProfile::setDeletionMode(DeletionMode mode)
{
    if (mode < MoveToTrash || mode > DeletePermanently)
        mode = MoveToTrash;

    m_deletionMode = mode;
}

/*
===================
SyncProfile::setDatabaseLocation
===================
*/
void SyncProfile::setDatabaseLocation(SyncProfile::DatabaseLocation location)
{
    if (location < Locally || location > Decentralized)
        location = Decentralized;

    m_databaseLocation = location;
}

/*
===================
SyncProfile::setVersioningFormat
===================
*/
void SyncProfile::setVersioningFormat(VersioningFormat format)
{
    if (format < FileTimestampBefore || format > LastVersion)
        format = FileTimestampAfter;

    m_versioningFormat = format;
}

/*
===================
SyncProfile::setVersioningLocation
===================
*/
void SyncProfile::setVersioningLocation(VersioningLocation location)
{
    if (location < LocallyNextToFolder || location > CustomLocation)
        location = LocallyNextToFolder;

    m_versioningLocation = location;
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
SyncProfile::isAutomatic
===================
*/
bool SyncProfile::isAutomatic() const
{
    return m_syncingMode == AutomaticAdaptive || m_syncingMode == AutomaticFixed;
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
    manualAction = new QAction("&" + qApp->translate("MainWindow", "Manual"), parent);
    automaticAdaptiveAction = new QAction("&" + qApp->translate("MainWindow", "Automatic (Adaptive)"), parent);
    automaticFixedAction = new QAction("&" + qApp->translate("MainWindow", "Automatic (Fixed)"), parent);
    detectMovedFilesAction = new QAction("&" + qApp->translate("MainWindow", "Detect Renamed and Moved Files"), parent);
    increaseSyncTimeAction = new QAction("&" + qApp->translate("MainWindow", "Increase"), parent);
    syncingTimeAction = new QAction(qApp->translate("MainWindow", "Synchronize Every: "), parent);
    decreaseSyncTimeAction = new QAction("&" + qApp->translate("MainWindow", "Decrease"), parent);
    fixedSyncingTimeAction = new QAction("&" + qApp->translate("MainWindow", "Synchronize Every: "), parent);
    moveToTrashAction = new QAction("&" + qApp->translate("MainWindow", "Move Files to Trash"), parent);
    versioningAction = new QAction("&" + qApp->translate("MainWindow", "Versioning"), parent);
    deletePermanentlyAction = new QAction("&" + qApp->translate("MainWindow", "Delete Files Permanently"), parent);
    fileTimestampBeforeAction = new QAction("&" + qApp->translate("MainWindow", "File Timestamp (Before Extension)"), parent);
    fileTimestampAfterAction = new QAction("&" + qApp->translate("MainWindow", "File Timestamp (After Extension)"), parent);
    folderTimestampAction = new QAction("&" + qApp->translate("MainWindow", "Folder Timestamp"), parent);
    lastVersionAction = new QAction("&" + qApp->translate("MainWindow", "Last Version"), parent);
    versioningPostfixAction = new QAction(QString("&" + qApp->translate("MainWindow", "Folder Postfix: %1")).arg(m_versioningFolder), parent);
    versioningPatternAction = new QAction(QString("&" + qApp->translate("MainWindow", "Pattern: %1")).arg(m_versioningPattern), parent);
    locallyNextToFolderAction = new QAction("&" + qApp->translate("MainWindow", "Locally Next to Folder"), parent);
    customLocationAction = new QAction("&" + qApp->translate("MainWindow", "Custom Location"), parent);
    customLocationPathAction = new QAction(qApp->translate("MainWindow", "Custom Location: ") + m_versioningPath, parent);
    saveDatabaseLocallyAction = new QAction("&" + qApp->translate("MainWindow", "Locally (On the local machine)"), parent);
    saveDatabaseDecentralizedAction = new QAction("&" + qApp->translate("MainWindow", "Decentralized (Inside synchronization folders)"), parent);
    fileMinSizeAction = new QAction(QString("&" + qApp->translate("MainWindow", "Minimum File Size: %1 bytes")).arg(m_fileMinSize), parent);
    fileMaxSizeAction = new QAction(QString("&" + qApp->translate("MainWindow", "Maximum File Size: %1 bytes")).arg(m_fileMaxSize), parent);
    movedFileMinSizeAction = new QAction(QString("&" + qApp->translate("MainWindow", "Minimum Size for a Moved File: %1 bytes")).arg(m_movedFileMinSize), parent);
    includeAction = new QAction(QString("&" + qApp->translate("MainWindow", "Include: %1")).arg(m_includeList.join("; ")), parent);
    excludeAction = new QAction(QString("&" + qApp->translate("MainWindow", "Exclude: %1")).arg(m_excludeList.join("; ")), parent);
    ignoreHiddenFilesAction = new QAction("&" + qApp->translate("MainWindow", "Ignore Hidden Files"), parent);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(m_syncTimeMultiplier <= 1);

    increaseSyncTimeAction->setEnabled(true);
    decreaseSyncTimeAction->setEnabled(m_syncTimeMultiplier > 1);

    manualAction->setCheckable(true);
    automaticAdaptiveAction->setCheckable(true);
    automaticFixedAction->setCheckable(true);
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

    syncingModeMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Syncing Mode"), parent);
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

    deletionModeMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Deletion Mode"), parent);
    deletionModeMenu->addAction(moveToTrashAction);
    deletionModeMenu->addAction(versioningAction);
    deletionModeMenu->addAction(deletePermanentlyAction);

    versioningFormatMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Versioning Format"), parent);
    versioningFormatMenu->addAction(fileTimestampBeforeAction);
    versioningFormatMenu->addAction(fileTimestampAfterAction);
    versioningFormatMenu->addAction(folderTimestampAction);
    versioningFormatMenu->addAction(lastVersionAction);
    versioningFormatMenu->addSeparator();
    versioningFormatMenu->addAction(versioningPostfixAction);
    versioningFormatMenu->addAction(versioningPatternAction);

    versioningLocationMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Versioning Location"), parent);
    versioningLocationMenu->addAction(locallyNextToFolderAction);
    versioningLocationMenu->addAction(customLocationAction);
    versioningLocationMenu->addSeparator();
    versioningLocationMenu->addAction(customLocationPathAction);

    databaseLocationMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Database Location"), parent);
    databaseLocationMenu->addAction(saveDatabaseLocallyAction);
    databaseLocationMenu->addAction(saveDatabaseDecentralizedAction);

    filteringMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Filtering"), parent);
    filteringMenu->addAction(fileMinSizeAction);
    filteringMenu->addAction(fileMaxSizeAction);
    filteringMenu->addAction(movedFileMinSizeAction);
    filteringMenu->addAction(includeAction);
    filteringMenu->addAction(excludeAction);
    filteringMenu->addSeparator();
    filteringMenu->addAction(ignoreHiddenFilesAction);

    detectMovedFilesAction->setChecked(detectMovedFiles());
    databaseLocationMenu->setEnabled(databaseLocation());
    ignoreHiddenFilesAction->setChecked(ignoreHiddenFiles());
}

/*
===================
SyncProfile::destroyMenus
===================
*/
void SyncProfile::destroyMenus()
{
    manualAction->deleteLater();
    automaticAdaptiveAction->deleteLater();
    automaticFixedAction->deleteLater();
    detectMovedFilesAction->deleteLater();
    increaseSyncTimeAction->deleteLater();
    syncingTimeAction->deleteLater();
    decreaseSyncTimeAction->deleteLater();
    fixedSyncingTimeAction->deleteLater();
    moveToTrashAction->deleteLater();
    versioningAction->deleteLater();
    deletePermanentlyAction->deleteLater();
    fileTimestampBeforeAction->deleteLater();
    fileTimestampAfterAction->deleteLater();
    folderTimestampAction->deleteLater();
    lastVersionAction->deleteLater();
    versioningPostfixAction->deleteLater();
    versioningPatternAction->deleteLater();
    locallyNextToFolderAction->deleteLater();
    customLocationAction->deleteLater();
    customLocationPathAction->deleteLater();
    saveDatabaseLocallyAction->deleteLater();
    saveDatabaseDecentralizedAction->deleteLater();
    fileMinSizeAction->deleteLater();
    fileMaxSizeAction->deleteLater();
    movedFileMinSizeAction->deleteLater();
    includeAction->deleteLater();
    excludeAction->deleteLater();
    ignoreHiddenFilesAction->deleteLater();

    syncingModeMenu->deleteLater();
    deletionModeMenu->deleteLater();
    versioningFormatMenu->deleteLater();
    versioningLocationMenu->deleteLater();
    databaseLocationMenu->deleteLater();
    filteringMenu->deleteLater();
}

/*
===================
SyncProfile::loadSettings
===================
*/
void SyncProfile::loadSettings()
{
    if (toBeRemoved)
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString keyName(name + QLatin1String("_profile/"));

    paused = settings.value(keyName + QLatin1String("Paused"), false).toBool();
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setDatabaseLocation(static_cast<SyncProfile::DatabaseLocation>(settings.value(keyName + "DatabaseLocation", SyncProfile::Decentralized).toInt()));
    setSyncTimeMultiplier(settings.value(keyName + "SyncTimeMultiplier", 1).toInt());
    setSyncEveryFixed(settings.value(keyName + "FixedSyncTime", 1).toInt());
    setVersioningFolder(settings.value(keyName + "VersionFolder", "[Deletions]").toString());
    setVersioningPattern(settings.value(keyName + "VersionPattern", "yyyy_M_d_h_m_s_z").toString());
    setVersioningPath(settings.value(keyName + "VersioningPath", "").toString());
    setFileMinSize(settings.value(keyName + "FileMinSize", 0).toInt());
    setFileMaxSize(settings.value(keyName + "FileMaxSize", 0).toInt());
    setMovedFileMinSize(settings.value(keyName + "MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt());
    setIncludeList(settings.value(keyName + "IncludeList").toStringList());
    setExcludeList(settings.value(keyName + "ExcludeList").toStringList());
    setIgnoreHiddenFiles(settings.value(keyName + "IgnoreHiddenFiles", true).toBool());
}

/*
===================
SyncProfile::saveSettings
===================
*/
void SyncProfile::saveSettings() const
{
    if (toBeRemoved)
        return;

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString keyName(name + QLatin1String("_profile/"));

    settings.setValue(keyName + "SyncingMode", syncingMode());
    settings.setValue(keyName + "SyncTimeMultiplier", syncTimeMultiplier());
    settings.setValue(keyName + "FixedSyncTime", syncEveryFixed());
    settings.setValue(keyName + "DetectMovedFiles", detectMovedFiles());
    settings.setValue(keyName + "DeletionMode", deletionMode());
    settings.setValue(keyName + "VersioningFormat", versioningFormat());
    settings.setValue(keyName + "VersioningLocation", versioningLocation());
    settings.setValue(keyName + "VersioningPath", versioningPath());
    settings.setValue(keyName + "DatabaseLocation", databaseLocation());
    settings.setValue(keyName + "IgnoreHiddenFiles", ignoreHiddenFiles());
    settings.setValue(keyName + "FileMinSize", fileMinSize());
    settings.setValue(keyName + "FileMaxSize", fileMaxSize());
    settings.setValue(keyName + "MovedFileMinSize", movedFileMinSize());
    settings.setValue(keyName + "IncludeList", includeList());
    settings.setValue(keyName + "ExcludeList", excludeList());
    settings.setValue(keyName + "VersionFolder", versioningFolder());
    settings.setValue(keyName + "VersionPattern", versioningPattern());

    settings.setValue(keyName + QLatin1String("LastSyncDate"), lastSyncDate);
    settings.setValue(keyName + QLatin1String("Paused"), paused);
    settings.setValue(keyName + QLatin1String("SyncTime"), syncTime);

    for (auto &folder : folders)
    {
        if (folder.toBeRemoved)
            continue;

        settings.setValue(keyName + folder.path + QLatin1String("_LastSyncDate"), folder.lastSyncDate);
        settings.setValue(keyName + folder.path + QLatin1String("_Paused"), folder.paused);
        settings.setValue(keyName + folder.path + QLatin1String("_SyncType"), folder.syncType);
    }
}

/*
===================
SyncProfile::updateStrings
===================
*/
void SyncProfile::updateStrings()
{
    manualAction->setText("&" + qApp->translate("MainWindow", "Manual"));
    automaticAdaptiveAction->setText("&" + qApp->translate("MainWindow", "Automatic (Adaptive)"));
    automaticFixedAction->setText("&" + qApp->translate("MainWindow", "Automatic (Fixed)"));
    detectMovedFilesAction->setText("&" + qApp->translate("MainWindow", "Detect Renamed and Moved Files"));
    increaseSyncTimeAction->setText("&" + qApp->translate("MainWindow", "Increase"));
    syncingTimeAction->setText(qApp->translate("MainWindow", "Synchronize Every:"));
    decreaseSyncTimeAction->setText("&" + qApp->translate("MainWindow", "Decrease"));
    fixedSyncingTimeAction->setText(QString("&" + qApp->translate("MainWindow", "Synchronize every %1")).arg(0));
    moveToTrashAction->setText("&" + qApp->translate("MainWindow", "Move Files to Trash"));
    versioningAction->setText("&" + qApp->translate("MainWindow", "Versioning"));
    deletePermanentlyAction->setText("&" + qApp->translate("MainWindow", "Delete Files Permanently"));
    fileTimestampBeforeAction->setText("&" + qApp->translate("MainWindow", "File Timestamp (Before Extension)"));
    fileTimestampAfterAction->setText("&" + qApp->translate("MainWindow", "File Timestamp (After Extension)"));
    folderTimestampAction->setText("&" + qApp->translate("MainWindow", "Folder Timestamp"));
    lastVersionAction->setText("&" + qApp->translate("MainWindow", "Last Version"));
    versioningPostfixAction->setText(QString("&" + qApp->translate("MainWindow", "Folder Postfix: %1")).arg(versioningFolder()));
    versioningPatternAction->setText(QString("&" + qApp->translate("MainWindow", "Pattern: %1")).arg(versioningPattern()));
    locallyNextToFolderAction->setText("&" + qApp->translate("MainWindow", "Locally Next to Folder"));
    customLocationAction->setText("&" + qApp->translate("MainWindow", "Custom Location"));
    customLocationPathAction->setText(qApp->translate("MainWindow", "Custom Location: ") + versioningPath());
    saveDatabaseLocallyAction->setText("&" + qApp->translate("MainWindow", "Locally (On the local machine)"));
    saveDatabaseDecentralizedAction->setText("&" + qApp->translate("MainWindow", "Decentralized (Inside synchronization folders)"));
    fileMinSizeAction->setText(QString("&" + qApp->translate("MainWindow", "Minimum File Size: %1 bytes")).arg(fileMinSize()));
    fileMaxSizeAction->setText(QString("&" + qApp->translate("MainWindow", "Maximum File Size: %1 bytes")).arg(fileMaxSize()));
    movedFileMinSizeAction->setText(QString("&" + qApp->translate("MainWindow", "Minimum Size for a Moved File: %1 bytes")).arg(movedFileMinSize()));
    includeAction->setText(QString("&" + qApp->translate("MainWindow", "Include: %1")).arg(includeList().join("; ")));
    excludeAction->setText(QString("&" + qApp->translate("MainWindow", "Exclude: %1")).arg(excludeList().join("; ")));
    ignoreHiddenFilesAction->setText("&" + qApp->translate("MainWindow", "Ignore Hidden Files"));
    syncingModeMenu->setTitle("&" + qApp->translate("MainWindow", "Syncing Mode"));
    deletionModeMenu->setTitle("&" + qApp->translate("MainWindow", "Deletion Mode"));
    versioningFormatMenu->setTitle("&" + qApp->translate("MainWindow", "Versioning Format"));
    versioningLocationMenu->setTitle("&" + qApp->translate("MainWindow", "Versioning Location"));
    databaseLocationMenu->setTitle("&" + qApp->translate("MainWindow", "Database Location"));
    filteringMenu->setTitle("&" + qApp->translate("MainWindow", "Filtering"));
}
