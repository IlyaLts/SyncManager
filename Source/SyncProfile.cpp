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
SyncProfile::SyncProfile(QWidget *parent, const QString &name, const QModelIndex &index)
{
    this->m_index = index;
    this->name = name;
    m_versioningFolder = "[Deletions]";
    m_versioningPattern = "yyyy_M_d_h_m_s_z";

    syncTimer.setSingleShot(true);
    syncTimer.setTimerType(Qt::VeryCoarseTimer);

    setupMenus(parent);
    loadSettings();
}

/*
===================
SyncProfile::~SyncProfile
===================
*/
SyncProfile::~SyncProfile()
{
    if (!m_toBeRemoved)
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

    setSyncTimeMultiplier(settings.value(keyName + "SyncTimeMultiplier", 1).toUInt());
    setSyncIntervalFixed(settings.value(keyName + "FixedSyncTime", defaultFixedInterval).toULongLong());
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setDetectMovedFiles(settings.value(keyName + "DetectMovedFiles", true).toBool());
    setDeltaCopying(settings.value(keyName + "DeltaCopying", false).toBool());
    setVersioningPath(settings.value(keyName + "VersioningPath", "").toString());
    setDatabaseLocation(static_cast<SyncProfile::DatabaseLocation>(settings.value(keyName + "DatabaseLocation", SyncProfile::Decentralized).toInt()));
    setIgnoreHiddenFiles(settings.value(keyName + "IgnoreHiddenFiles", false).toBool());
    setFileMinSize(settings.value(keyName + "FileMinSize", 0).toULongLong());
    setFileMaxSize(settings.value(keyName + "FileMaxSize", 0).toULongLong());
    setMovedFileMinSize(settings.value(keyName + "MovedFileMinSize", MovedFilesMinSize).toULongLong());
    setDeltaCopyingMinSize(settings.value(keyName + "DeltaCopyingMinSize", DeltaCopyingMinSize).toULongLong());
    setIncludeList(settings.value(keyName + "IncludeList").toStringList());
    setExcludeList(settings.value(keyName + "ExcludeList").toStringList());
    setVersioningFolder(settings.value(keyName + "VersionFolder", "[Deletions]").toString());
    setVersioningPattern(settings.value(keyName + "VersionPattern", "yyyy_M_d_h_m_s_z").toString());

    lastSyncDate = settings.value(keyName + "LastSyncDate").toDateTime();
    m_paused = settings.value(keyName + "Paused", false).toBool();
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
    settings.setValue(profileKey + "DeltaCopying", deltaCopying());
    settings.setValue(profileKey + "DeletionMode", deletionMode());
    settings.setValue(profileKey + "VersioningFormat", versioningFormat());
    settings.setValue(profileKey + "VersioningLocation", versioningLocation());
    settings.setValue(profileKey + "VersioningPath", versioningPath());
    settings.setValue(profileKey + "DatabaseLocation", databaseLocation());
    settings.setValue(profileKey + "IgnoreHiddenFiles", ignoreHiddenFiles());
    settings.setValue(profileKey + "FileMinSize", fileMinSize());
    settings.setValue(profileKey + "FileMaxSize", fileMaxSize());
    settings.setValue(profileKey + "MovedFileMinSize", movedFileMinSize());
    settings.setValue(profileKey + "DeltaCopyingMinSize", deltaCopyingMinSize());
    settings.setValue(profileKey + "IncludeList", includeList());
    settings.setValue(profileKey + "ExcludeList", excludeList());
    settings.setValue(profileKey + "VersionFolder", versioningFolder());
    settings.setValue(profileKey + "VersionPattern", versioningPattern());

    settings.setValue(profileKey + QLatin1String("LastSyncDate"), lastSyncDate);
    settings.setValue(profileKey + QLatin1String("Paused"), m_paused);
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
SyncProfile::setSyncTimeMultiplier
===================
*/
void SyncProfile::setSyncTimeMultiplier(quint32 multiplier)
{
    if (multiplier <= 0)
        multiplier = 1;

    m_syncTimeMultiplier = multiplier;
    updateNextSyncingTime();
}

/*
===================
SyncProfile::setDeltaCopying
===================
*/
void SyncProfile::setDeltaCopying(bool enable)
{
    m_deltaCopying = enable;

    if (!syncApp->initiated())
        return;

    if (deltaCopying())
    {
        QString title(syncApp->translate("Enable file delta copying?"));
        QString text(syncApp->translate("Are you sure? Beware: files will be overwritten, and there's no way to bring the previous versions back."));

        if (!syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, nullptr))
        {
            setDeltaCopying(false);
            deltaCopyingAction->setChecked(false);
        }
    }

    deltaCopyingMinSizeAction->setVisible(deltaCopying());
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
SyncProfile::setPaused
===================
*/
void SyncProfile::setPaused(bool paused)
{
    m_paused = paused;

    for (auto &folder : folders)
        folder.setPaused(paused);

    if (paused)
        syncTimer.stop();
    else
        updateTimer();
}

/*
===================
SyncProfile::remove
===================
*/
void SyncProfile::remove()
{
    setPaused(true);

    m_toBeRemoved = true;
    removeSettings();

    for (auto &folder : folders)
        folder.remove();
}

/*
===================
SyncProfile::updateTimer
===================
*/
void SyncProfile::updateTimer()
{
    using namespace std;
    using namespace std::chrono;

    if (!isAutomatic())
        return;

    QDateTime dateToSync(lastSyncDate);

    if (syncingMode() == AutomaticAdaptive)
        dateToSync = dateToSync.addMSecs(syncEvery);
    else if (syncingMode() == AutomaticFixed)
        dateToSync = dateToSync.addMSecs(syncIntervalFixed());

    qint64 syncTime = 0;

    if (dateToSync >= QDateTime::currentDateTime())
        syncTime = QDateTime::currentDateTime().msecsTo(dateToSync);

    if (!isActive())
        if (syncTime < SYNC_MIN_DELAY)
            syncTime = SYNC_MIN_DELAY;

    bool profileActive = syncTimer.isActive();
    bool startTimer = false;

    if (!syncApp->manager()->busy() && profileActive)
        startTimer = true;

    if (!profileActive || (duration<qint64, milli>(syncTime) < syncTimer.remainingTime()))
        startTimer = true;

    if (startTimer)
    {
        quint64 interval = syncTime;
        quint64 max = SyncManager::maxInterval();

        // If exceeds the maximum value of an qint64
        if (interval > max)
            interval = max;

        syncTimer.setInterval(duration_cast<duration<qint64, nano>>(duration<quint64, milli>(interval)));
        syncTimer.start();
    }
}

/*
===================
SyncProfile::updateNextSyncingTime
===================
*/
void SyncProfile::updateNextSyncingTime()
{
    quint64 time = 0;

    if (syncingMode() == AutomaticAdaptive)
    {
        time = syncTime;

        // Multiplies sync time by 2
        for (quint32 i = 0; i < syncTimeMultiplier() - 1; i++)
        {
            time <<= 1;
            quint64 max = SyncManager::maxInterval();

            // If exceeds the maximum value of an qint64
            if (time > max)
            {
                time = max;
                break;
            }
        }
    }
    else if (syncingMode() == AutomaticFixed)
    {
        time = syncIntervalFixed();
    }

    if (time < SYNC_MIN_DELAY)
        time = SYNC_MIN_DELAY;

    syncEvery = time;
}

/*
===================
SyncProfile::updatePausedState
===================
*/
void SyncProfile::updatePausedState()
{
    int unpausedFolders = 0;

    for (auto &folder : folders)
        if (!folder.paused())
            unpausedFolders++;

    m_paused = unpausedFolders < 2;

    if (m_paused)
        syncTimer.stop();
    else
        updateTimer();
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
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path())) + ".db");
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
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QDir().mkdir(folder.path() + DATA_FOLDER_PATH);

        if (!QDir(folder.path() + DATA_FOLDER_PATH).exists())
            continue;

        folder.saveToDatabase(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);

#ifdef Q_OS_WIN
        setHiddenFileAttribute(QString(folder.path() + DATA_FOLDER_PATH), true);
        setHiddenFileAttribute(QString(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME), true);
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
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        QByteArray filename(QByteArray::number(hash64(folder.path())) + ".db");
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
        if (!folder.isActive() || folder.toBeRemoved())
            continue;

        folder.loadFromDatabase(folder.path() + DATA_FOLDER_PATH + "/" + DATABASE_FILENAME);
    }
}

/*
===================
SyncProfile::addFilePath
===================
*/
void SyncProfile::addFilePath(hash64_t hash, const QByteArray &path)
{
    m_mutex.lock();

    if (!m_filePaths.contains(Hash(hash)))
    {
        auto it = m_filePaths.insert(Hash(hash), path);
        it->squeeze();
    }

    m_mutex.unlock();
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

    m_mutex.lock();
    m_filePaths.remove(hash);

    // I don't know exactly what squeeze() does internally,
    // but calling it repeatedly significantly impacts performance.
    // So, since QHash capacity is always doubled, we should just check
    // if we really need to squeeze it. That, itself, brings performance back.
    // Also, the minimum capacity for QHash is 64 bytes.
    if (m_filePaths.size() < m_filePaths.capacity() / 2 && m_filePaths.size() > 64)
        m_filePaths.squeeze();

    m_mutex.unlock();
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

    return !m_paused && !m_toBeRemoved && activeFolders >= 2;
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
        if (folder.caseSensitive())
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
        if (folder.exists())
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
        if (!folder.exists())
            return true;

    return false;
}

/*
===================
SyncProfile::m_partiallySynchronized
===================
*/
bool SyncProfile::partiallySynchronized() const
{
    for (const auto &folder : folders)
        if (folder.hasUnsyncedFiles())
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
        if (folder.path() == index.data(Qt::DisplayRole).toString())
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
        if (folder.path().compare(path.toUtf8(), folder.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
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
    manualAction = new QAction("&" + syncApp->translate("Manual"), parent);
    automaticAdaptiveAction = new QAction("&" + syncApp->translate("Automatic (Adaptive)"), parent);
    automaticFixedAction = new QAction("&" + syncApp->translate("Automatic (Fixed)"), parent);
    detectMovedFilesAction = new QAction("&" + syncApp->translate("Detect Renamed and Moved Files"), parent);
    deltaCopyingAction = new QAction("&" + syncApp->translate("File Delta Copying") + " (Beta)", parent);
    increaseSyncTimeAction = new QAction("&" + syncApp->translate("Increase"), parent);
    syncingTimeAction = new QAction(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncEvery)), parent);
    decreaseSyncTimeAction = new QAction("&" + syncApp->translate("Decrease"), parent);
    fixedSyncingTimeAction = new QAction("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncIntervalFixed())), parent);
    moveToTrashAction = new QAction("&" + syncApp->translate("Move Files to Trash"), parent);
    versioningAction = new QAction("&" + syncApp->translate("Versioning"), parent);
    deletePermanentlyAction = new QAction("&" + syncApp->translate("Delete Files Permanently"), parent);
    fileTimestampBeforeAction = new QAction("&" + syncApp->translate("File Timestamp (Before Extension)"), parent);
    fileTimestampAfterAction = new QAction("&" + syncApp->translate("File Timestamp (After Extension)"), parent);
    folderTimestampAction = new QAction("&" + syncApp->translate("Folder Timestamp"), parent);
    lastVersionAction = new QAction("&" + syncApp->translate("Last Version"), parent);
    versioningPostfixAction = new QAction(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(m_versioningFolder), parent);
    versioningPatternAction = new QAction(QString("&" + syncApp->translate("Pattern: %1")).arg(m_versioningPattern), parent);
    locallyNextToFolderAction = new QAction("&" + syncApp->translate("Locally Next to Folder"), parent);
    customLocationAction = new QAction("&" + syncApp->translate("Custom Location"), parent);
    customLocationPathAction = new QAction(syncApp->translate("Custom Location: ") + m_versioningPath, parent);
    databaseLocallyAction = new QAction("&" + syncApp->translate("Locally (On the local machine)"), parent);
    databaseDecentralizedAction = new QAction("&" + syncApp->translate("Decentralized (Inside synchronization folders)"), parent);
    fileMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum File Size: %1")).arg(formatSize((m_fileMinSize))), parent);
    fileMaxSizeAction = new QAction(QString("&" + syncApp->translate("Maximum File Size: %1")).arg(formatSize((m_fileMaxSize))), parent);
    movedFileMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum Size for a Moved File: %1")).arg(formatSize((m_movedFileMinSize))), parent);
    deltaCopyingMinSizeAction = new QAction(QString("&" + syncApp->translate("Minimum Size for Delta Copying: %1")).arg(formatSize((m_deltaCopyingMinSize))), parent);
    includeAction = new QAction(QString("&" + syncApp->translate("Include: %1")).arg(m_includeList.join("; ")), parent);
    excludeAction = new QAction(QString("&" + syncApp->translate("Exclude: %1")).arg(m_excludeList.join("; ")), parent);
    ignoreHiddenFilesAction = new QAction("&" + syncApp->translate("Ignore Hidden Files"), parent);

    syncingTimeAction->setDisabled(true);
    decreaseSyncTimeAction->setDisabled(m_syncTimeMultiplier <= 1);

    increaseSyncTimeAction->setEnabled(true);
    decreaseSyncTimeAction->setEnabled(m_syncTimeMultiplier > 1);

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

    updateMenuStates();

    connect(deltaCopyingAction, &QAction::triggered, this, &SyncProfile::setDeltaCopying);
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
    deltaCopyingAction->setChecked(m_deltaCopying);
    syncingTimeAction->setVisible(syncingMode() == AutomaticAdaptive);
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncEvery)));
    fixedSyncingTimeAction->setVisible(syncingMode() == AutomaticFixed);
    fixedSyncingTimeAction->setText("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncIntervalFixed())));
    moveToTrashAction->setChecked(deletionMode() == MoveToTrash);
    versioningAction->setChecked(deletionMode() == Versioning);
    deletePermanentlyAction->setChecked(deletionMode() == DeletePermanently);
    fileTimestampBeforeAction->setChecked(versioningFormat() == FileTimestampBefore);
    fileTimestampAfterAction->setChecked(versioningFormat() == FileTimestampAfter);
    folderTimestampAction->setChecked(versioningFormat() == FolderTimestamp);
    lastVersionAction->setChecked(versioningFormat() == LastVersion);
    versioningPostfixAction->setText(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(versioningFolder()));
    versioningPatternAction->setText(QString("&" + syncApp->translate("Pattern: %1")).arg(versioningPattern()));
    locallyNextToFolderAction->setChecked(VersioningLocation() == LocallyNextToFolder);
    customLocationAction->setChecked(VersioningLocation() == CustomLocation);
    customLocationPathAction->setText(syncApp->translate("Custom Location: ") + versioningPath());
    databaseLocallyAction->setChecked(databaseLocation() == Locally);
    databaseDecentralizedAction->setChecked(databaseLocation() == Decentralized);
    fileMinSizeAction->setText("&" + syncApp->translate("Minimum File Size: %1").arg(formatSize(fileMinSize())));
    fileMaxSizeAction->setText("&" + syncApp->translate("Maximum File Size: %1").arg(formatSize(fileMaxSize())));
    movedFileMinSizeAction->setText("&" + syncApp->translate("Minimum Size for a Moved File: %1").arg(formatSize(movedFileMinSize())));
    deltaCopyingMinSizeAction->setVisible(deltaCopying());
    deltaCopyingMinSizeAction->setText("&" + syncApp->translate("Minimum Size for Delta Copying: %1").arg(formatSize(deltaCopyingMinSize())));
    includeAction->setText("&" + syncApp->translate("Include: %1").arg(includeList().join("; ")));
    excludeAction->setText("&" + syncApp->translate("Exclude: %1").arg(excludeList().join("; ")));
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
    deltaCopyingAction->deleteLater();
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
    deltaCopyingMinSizeAction->deleteLater();
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
    manualAction->setText("&" + syncApp->translate("Manual"));
    automaticAdaptiveAction->setText("&" + syncApp->translate("Automatic (Adaptive)"));
    automaticFixedAction->setText("&" + syncApp->translate("Automatic (Fixed)"));
    detectMovedFilesAction->setText("&" + syncApp->translate("Detect Renamed and Moved Files"));
    deltaCopyingAction->setText("&" + syncApp->translate("File Delta Copying") + " (Beta)");
    increaseSyncTimeAction->setText("&" + syncApp->translate("Increase"));
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncEvery)));
    decreaseSyncTimeAction->setText("&" + syncApp->translate("Decrease"));
    fixedSyncingTimeAction->setText("&" + syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(syncIntervalFixed())));
    moveToTrashAction->setText("&" + syncApp->translate("Move Files to Trash"));
    versioningAction->setText("&" + syncApp->translate("Versioning"));
    deletePermanentlyAction->setText("&" + syncApp->translate("Delete Files Permanently"));
    fileTimestampBeforeAction->setText("&" + syncApp->translate("File Timestamp (Before Extension)"));
    fileTimestampAfterAction->setText("&" + syncApp->translate("File Timestamp (After Extension)"));
    folderTimestampAction->setText("&" + syncApp->translate("Folder Timestamp"));
    lastVersionAction->setText("&" + syncApp->translate("Last Version"));
    versioningPostfixAction->setText(QString("&" + syncApp->translate("Folder Postfix: %1")).arg(versioningFolder()));
    versioningPatternAction->setText(QString("&" + syncApp->translate("Pattern: %1")).arg(versioningPattern()));
    locallyNextToFolderAction->setText("&" + syncApp->translate("Locally Next to Folder"));
    customLocationAction->setText("&" + syncApp->translate("Custom Location"));
    customLocationPathAction->setText(syncApp->translate("Custom Location: ") + versioningPath());
    databaseLocallyAction->setText("&" + syncApp->translate("Locally (On the local machine)"));
    databaseDecentralizedAction->setText("&" + syncApp->translate("Decentralized (Inside synchronization folders)"));
    fileMinSizeAction->setText(QString("&" + syncApp->translate("Minimum File Size: %1")).arg(formatSize(fileMinSize())));
    fileMaxSizeAction->setText(QString("&" + syncApp->translate("Maximum File Size: %1")).arg(formatSize(fileMaxSize())));
    movedFileMinSizeAction->setText(QString("&" + syncApp->translate("Minimum Size for a Moved File: %1")).arg(formatSize(movedFileMinSize())));
    deltaCopyingMinSizeAction->setText(QString("&" + syncApp->translate("Minimum Size for Delta Copying: %1")).arg(formatSize(deltaCopyingMinSize())));
    includeAction->setText(QString("&" + syncApp->translate("Include: %1")).arg(includeList().join("; ")));
    excludeAction->setText(QString("&" + syncApp->translate("Exclude: %1")).arg(excludeList().join("; ")));
    ignoreHiddenFilesAction->setText("&" + syncApp->translate("Ignore Hidden Files"));
    syncingModeMenu->setTitle("&" + syncApp->translate("Syncing Mode"));
    deletionModeMenu->setTitle("&" + syncApp->translate("Deletion Mode"));
    versioningFormatMenu->setTitle("&" + syncApp->translate("Versioning Format"));
    versioningLocationMenu->setTitle("&" + syncApp->translate("Versioning Location"));
    databaseLocationMenu->setTitle("&" + syncApp->translate("Database Location"));
    filteringMenu->setTitle("&" + syncApp->translate("Filtering"));
}
