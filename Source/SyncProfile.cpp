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
    m_versioningFolder = "[Deletions]";
    m_versioningPattern = "yyyy_M_d_h_m_s_z";

    syncTimer.setSingleShot(true);
    syncTimer.setTimerType(Qt::VeryCoarseTimer);

    loadSettings();
}

/*
===================
SyncProfile::~SyncProfile
===================
*/
SyncProfile::~SyncProfile()
{
    if (!toBeRemoved)
        saveSettings();
}

/*
===================
SyncProfile::loadSettings
===================
*/
void SyncProfile::loadSettings()
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString keyName(name + QLatin1String("_profile/"));

    setSyncTimeMultiplier(settings.value(keyName + "SyncTimeMultiplier", 1).toInt());
    setSyncIntervalFixed(settings.value(keyName + "FixedSyncTime", 1).toInt());
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setVersioningPath(settings.value(keyName + "VersioningPath", "").toString());
    setDatabaseLocation(static_cast<SyncProfile::DatabaseLocation>(settings.value(keyName + "DatabaseLocation", SyncProfile::Decentralized).toInt()));
    setIgnoreHiddenFiles(settings.value(keyName + "IgnoreHiddenFiles", false).toBool());
    setFileMinSize(settings.value(keyName + "FileMinSize", 0).toInt());
    setFileMaxSize(settings.value(keyName + "FileMaxSize", 0).toInt());
    setMovedFileMinSize(settings.value(keyName + "MovedFileMinSize", MOVED_FILES_MIN_SIZE).toInt());
    setIncludeList(settings.value(keyName + "IncludeList").toStringList());
    setExcludeList(settings.value(keyName + "ExcludeList").toStringList());
    setVersioningFolder(settings.value(keyName + "VersionFolder", "[Deletions]").toString());
    setVersioningPattern(settings.value(keyName + "VersionPattern", "yyyy_M_d_h_m_s_z").toString());

    lastSyncDate = settings.value(keyName + "LastSyncDate").toDateTime();
    paused = settings.value(keyName + "Paused", false).toBool();
    syncTime = settings.value(keyName + "SyncTime", 0).toULongLong();
}

/*
===================
SyncProfile::saveSettings
===================
*/
void SyncProfile::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString profileKey(name + QLatin1String("_profile/"));

    settings.setValue(profileKey + "SyncingMode", syncingMode());
    settings.setValue(profileKey + "SyncTimeMultiplier", syncTimeMultiplier());
    settings.setValue(profileKey + "FixedSyncTime", syncIntervalFixed());
    settings.setValue(profileKey + "DetectMovedFiles", detectMovedFiles());
    settings.setValue(profileKey + "DeletionMode", deletionMode());
    settings.setValue(profileKey + "VersioningFormat", versioningFormat());
    settings.setValue(profileKey + "VersioningLocation", versioningLocation());
    settings.setValue(profileKey + "VersioningPath", versioningPath());
    settings.setValue(profileKey + "DatabaseLocation", databaseLocation());
    settings.setValue(profileKey + "IgnoreHiddenFiles", ignoreHiddenFiles());
    settings.setValue(profileKey + "FileMinSize", fileMinSize());
    settings.setValue(profileKey + "FileMaxSize", fileMaxSize());
    settings.setValue(profileKey + "MovedFileMinSize", movedFileMinSize());
    settings.setValue(profileKey + "IncludeList", includeList());
    settings.setValue(profileKey + "ExcludeList", excludeList());
    settings.setValue(profileKey + "VersionFolder", versioningFolder());
    settings.setValue(profileKey + "VersionPattern", versioningPattern());

    settings.setValue(profileKey + QLatin1String("LastSyncDate"), lastSyncDate);
    settings.setValue(profileKey + QLatin1String("Paused"), paused);
    settings.setValue(profileKey + QLatin1String("SyncTime"), syncTime);

    for (const auto &folder : folders)
        folder.saveSettings();
}

/*
===================
SyncProfile::removeSettings
===================
*/
void SyncProfile::removeSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    settings.remove(name + QLatin1String("_profile/"));
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
        for (FileMoveList::iterator it = folder.filesToMove.begin(); it != folder.filesToMove.end(); ++it)
        {
            fileHashes.insert(hash64(it.value().fromPath));
            fileHashes.insert(it.key().data);
        }

        for (FolderRenameList::iterator it = folder.foldersToRename.begin(); it != folder.foldersToRename.end(); ++it)
        {
            folderHashes.insert(it.key().data);
            folderHashes.insert(hash64(it.value().toPath));
        }
    }

    bool databaseChanged = false;

    for (auto &folder : folders)
    {
        for (Files::iterator fileIt = folder.files.begin(); fileIt != folder.files.end(); ++fileIt)
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
SyncProfile::removeNonexistentFileData
===================
*/
void SyncProfile::removeNonexistentFileData()
{
    for (auto &folder : folders)
    {
        for (Files::iterator fileIt = folder.files.begin(); fileIt != folder.files.end();)
        {
            if (!fileIt->exists() || fileIt->type == SyncFile::Unknown)
                fileIt = folder.files.erase(static_cast<Files::const_iterator>(fileIt));
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
    for (const auto &folder : folders)
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
SyncProfile::removeUnneededFilePath
===================
*/
void SyncProfile::removeUnneededFilePath(hash64_t hash)
{
    for (auto &folder : folders)
    {
        const SyncFile &file = folder.files.value(hash);

        if (file.type == SyncFile::Unknown)
            return;

        if (!file.scanned() || !file.exists())
            return;

        if (file.updated() || file.attributesUpdated())
            return;

        if (file.newlyAdded())
            return;
    }

    mutex.lock();
    filePaths.remove(hash);

    // I don't know exactly what squeeze() does internally,
    // but calling it repeatedly significantly impacts performance.
    // So, since QHash capacity is always doubled, we should just check
    // if we really need to squeeze it. That, itself, brings performance back.
    // Also, the minimum capacity for QHash is 64 bytes.
    if (filePaths.size() < filePaths.capacity() / 2 && filePaths.size() > 64)
        filePaths.squeeze();

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

    for (const auto &folder : folders)
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
    for (const auto &folder : folders)
        if (folder.caseSensitive)
            return true;

    return false;
}

/*
===================
SyncProfile::countExistingFolders
===================
*/
int SyncProfile::countExistingFolders() const
{
    int n = 0;

    for (const auto &folder : folders)
        if (folder.exists)
            n++;

    return n;
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
SyncProfile::partiallySynchronized
===================
*/
bool SyncProfile::partiallySynchronized() const
{
    for (const auto &folder : folders)
        if (folder.partiallySynchronized())
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
        if (folder.path.compare(path.toUtf8(), folder.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
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
    syncingTimeAction = new QAction(qApp->translate("MainWindow", "Synchronize Every") + QString(": %1").arg(syncEvery), parent);
    decreaseSyncTimeAction = new QAction("&" + qApp->translate("MainWindow", "Decrease"), parent);
    fixedSyncingTimeAction = new QAction("&" + qApp->translate("MainWindow", "Synchronize Every") + QString(": %1").arg(syncIntervalFixed()), parent);
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
    databaseLocallyAction = new QAction("&" + qApp->translate("MainWindow", "Locally (On the local machine)"), parent);
    databaseDecentralizedAction = new QAction("&" + qApp->translate("MainWindow", "Decentralized (Inside synchronization folders)"), parent);
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
    databaseLocallyAction->setCheckable(true);
    databaseDecentralizedAction->setCheckable(true);
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
    databaseLocationMenu->addAction(databaseLocallyAction);
    databaseLocationMenu->addAction(databaseDecentralizedAction);

    filteringMenu = new UnhidableMenu("&" + qApp->translate("MainWindow", "Filtering"), parent);
    filteringMenu->addAction(fileMinSizeAction);
    filteringMenu->addAction(fileMaxSizeAction);
    filteringMenu->addAction(movedFileMinSizeAction);
    filteringMenu->addAction(includeAction);
    filteringMenu->addAction(excludeAction);
    filteringMenu->addSeparator();
    filteringMenu->addAction(ignoreHiddenFilesAction);

    updateMenuStates();
}

/*
===================
SyncProfile::updateMenuStates
===================
*/
void SyncProfile::updateMenuStates()
{
    manualAction->setChecked(syncingMode() == Manual);
    automaticAdaptiveAction->setChecked(syncingMode() == AutomaticAdaptive);
    automaticFixedAction->setChecked(syncingMode() == AutomaticFixed);
    detectMovedFilesAction->setChecked(detectMovedFiles());
    syncingTimeAction->setVisible(syncingMode() == AutomaticAdaptive);
    syncingTimeAction->setText(tr("Synchronize Every"));
    fixedSyncingTimeAction->setVisible(syncingMode() == AutomaticFixed);
    fixedSyncingTimeAction->setText(tr("Synchronize Every"));
    moveToTrashAction->setChecked(deletionMode() == MoveToTrash);
    versioningAction->setChecked(deletionMode() == Versioning);
    deletePermanentlyAction->setChecked(deletionMode() == DeletePermanently);
    fileTimestampBeforeAction->setChecked(versioningFormat() == FileTimestampBefore);
    fileTimestampAfterAction->setChecked(versioningFormat() == FileTimestampAfter);
    folderTimestampAction->setChecked(versioningFormat() == FolderTimestamp);
    lastVersionAction->setChecked(versioningFormat() == LastVersion);
    versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(versioningFolder()));
    versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(versioningPattern()));
    locallyNextToFolderAction->setChecked(VersioningLocation() == LocallyNextToFolder);
    customLocationAction->setChecked(VersioningLocation() == CustomLocation);
    customLocationPathAction->setText(tr("Custom Location: ") + versioningPath());
    databaseLocallyAction->setChecked(databaseLocation() == Locally);
    databaseDecentralizedAction->setChecked(databaseLocation() == Decentralized);
    fileMinSizeAction->setText("&" + tr("Minimum File Size: %1 bytes").arg(fileMinSize()));
    fileMaxSizeAction->setText("&" + tr("Maximum File Size: %1 bytes").arg(fileMaxSize()));
    movedFileMinSizeAction->setText("&" + tr("Minimum Size for a Moved File: %1 bytes").arg(movedFileMinSize()));
    includeAction->setText("&" + tr("Include: %1").arg(includeList().join("; ")));
    excludeAction->setText("&" + tr("Exclude: %1").arg(excludeList().join("; ")));
    ignoreHiddenFilesAction->setChecked(ignoreHiddenFiles());

    versioningFormatMenu->setVisible(deletionMode() == Versioning);
    versioningLocationMenu->setVisible(deletionMode() == Versioning);
    filteringMenu->setVisible(deletionMode() == Versioning);
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
    databaseLocallyAction->deleteLater();
    databaseDecentralizedAction->deleteLater();
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
SyncProfile::retranslate
===================
*/
void SyncProfile::retranslate()
{
    manualAction->setText("&" + qApp->translate("MainWindow", "Manual"));
    automaticAdaptiveAction->setText("&" + qApp->translate("MainWindow", "Automatic (Adaptive)"));
    automaticFixedAction->setText("&" + qApp->translate("MainWindow", "Automatic (Fixed)"));
    detectMovedFilesAction->setText("&" + qApp->translate("MainWindow", "Detect Renamed and Moved Files"));
    increaseSyncTimeAction->setText("&" + qApp->translate("MainWindow", "Increase"));
    syncingTimeAction->setText(qApp->translate("MainWindow", "Synchronize Every") + QString(": %1").arg(syncEvery));
    decreaseSyncTimeAction->setText("&" + qApp->translate("MainWindow", "Decrease"));
    fixedSyncingTimeAction->setText("&" + qApp->translate("MainWindow", "Synchronize Every") + QString(": %1").arg(syncIntervalFixed()));
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
    databaseLocallyAction->setText("&" + qApp->translate("MainWindow", "Locally (On the local machine)"));
    databaseDecentralizedAction->setText("&" + qApp->translate("MainWindow", "Decentralized (Inside synchronization folders)"));
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
