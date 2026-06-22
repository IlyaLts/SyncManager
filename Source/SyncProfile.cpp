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
#include <QFileDialog>

/*
===================
SyncProfile::SyncProfile
===================
*/
SyncProfile::SyncProfile(QWidget *parent, const QString &name, const QModelIndex &index)
{
    this->m_index = index;
    this->m_name = name;
    m_versioningFolder = "[Deletions]";
    m_versioningPattern = "yyyy_M_d_h_m_s_z";

    m_syncTimer.setSingleShot(true);
    m_syncTimer.setTimerType(Qt::VeryCoarseTimer);

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
    QString keyName(m_name + QLatin1String("_profile/"));

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

    m_lastSyncDate = settings.value(keyName + "LastSyncDate").toDateTime();
    m_paused = settings.value(keyName + "Paused", false).toBool();
    m_syncTime = settings.value(keyName + "SyncTime", 0).toULongLong();

    ignoreHiddenFilesAction->setChecked(ignoreHiddenFiles());
    detectMovedFilesAction->setChecked(detectMovedFiles());

    updateNextSyncingTime();
    updateMenuSyncTime();
}

/*
===================
SyncProfile::saveSettings
===================
*/
void SyncProfile::saveSettings() const
{
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + SETTINGS_FILENAME, QSettings::IniFormat);
    QString profileKey(m_name + QLatin1String("_profile/"));

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

    settings.setValue(profileKey + QLatin1String("LastSyncDate"), m_lastSyncDate);
    settings.setValue(profileKey + QLatin1String("Paused"), m_paused);
    settings.setValue(profileKey + QLatin1String("SyncTime"), m_syncTime);

    for (const auto &folder : folders())
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
    settings.remove(m_name + QLatin1String("_profile/"));
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
    m_syncTimeMultiplier = qMax(1U, multiplier);
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

    for (auto &folder : folders())
        folder.setPaused(paused);

    if (paused)
        m_syncTimer.stop();
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

    for (auto &folder : folders())
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

    QDateTime dateToSync(m_lastSyncDate);

    if (syncingMode() == AutomaticAdaptive)
        dateToSync = dateToSync.addMSecs(m_syncEvery);
    else if (syncingMode() == AutomaticFixed)
        dateToSync = dateToSync.addMSecs(syncIntervalFixed());

    quint64 syncTime = 0;

    if (dateToSync >= QDateTime::currentDateTime())
        syncTime = QDateTime::currentDateTime().msecsTo(dateToSync);

    if (!isActive())
        syncTime = qMax(syncTime, SyncMinDelay);

    bool profileActive = m_syncTimer.isActive();
    bool startTimer = false;

    if (!syncApp->manager()->busy() && profileActive)
        startTimer = true;

    if (!profileActive || (duration<qint64, milli>(syncTime) < m_syncTimer.remainingTime()))
        startTimer = true;

    if (startTimer)
    {
        quint64 interval = qMin(syncTime, SyncManager::maxInterval());

        m_syncTimer.setInterval(duration_cast<duration<qint64, nano>>(duration<quint64, milli>(interval)));
        m_syncTimer.start();
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
        time = m_syncTime;

        // Multiplies sync time by 2
        for (quint32 i = 0; i < syncTimeMultiplier() - 1; i++)
        {
            quint64 maxInterval = SyncManager::maxInterval();
            time <<= 1;

            // If exceeds the maximum value of an qint64
            if (time > maxInterval)
            {
                time = maxInterval;
                break;
            }
        }
    }
    else if (syncingMode() == AutomaticFixed)
    {
        time = syncIntervalFixed();
    }

    m_syncEvery = qMax(time, SyncMinDelay);
}

/*
===================
SyncProfile::updatePausedState
===================
*/
void SyncProfile::updatePausedState()
{
    int unpausedFolders = 0;

    for (auto &folder : folders())
        if (!folder.paused())
            unpausedFolders++;

    m_paused = unpausedFolders < 2;

    if (m_paused)
        m_syncTimer.stop();
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

    for (auto &folder : folders())
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

    for (auto &folder : folders())
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
    for (auto &folder : folders())
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
    for (const auto &folder : folders())
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
    for (auto &folder : folders())
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
    for (auto &folder : folders())
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
    for (auto &folder : folders())
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
    for (auto &folder : folders())
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

    for (const auto &folder : folders())
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
    for (const auto &folder : folders())
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

    for (const auto &folder : folders())
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
    for (const auto &folder : folders())
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
    for (const auto &folder : folders())
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
    for (auto &folder : folders())
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
    for (auto &folder : folders())
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
    syncingTimeAction = new QAction(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(m_syncEvery)), parent);
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
    connect(manualAction, &QAction::triggered, this, [this](){ switchSyncingMode(Manual); });
    connect(automaticAdaptiveAction, &QAction::triggered, this, [this](){ switchSyncingMode(AutomaticAdaptive); });
    connect(automaticFixedAction, &QAction::triggered, this, [this](){ switchSyncingMode(AutomaticFixed); });
    connect(increaseSyncTimeAction, &QAction::triggered, this, [this](){ increaseSyncTime(); });
    connect(decreaseSyncTimeAction, &QAction::triggered, this, [this](){ decreaseSyncTime(); });
    connect(fixedSyncingTimeAction, &QAction::triggered, this, [this](){ setFixedInterval(); });
    connect(detectMovedFilesAction, &QAction::triggered, this, [this](){ toggleDetectMoved(); });
    connect(moveToTrashAction, &QAction::triggered, this, [this](){ switchDeletionMode(MoveToTrash); });
    connect(versioningAction, &QAction::triggered, this, [this](){ switchDeletionMode(Versioning); });
    connect(deletePermanentlyAction, &QAction::triggered, this, [this](){ switchDeletionMode(DeletePermanently); });
    connect(fileTimestampBeforeAction, &QAction::triggered, this, [this](){ switchVersioningFormat(FileTimestampBefore); });
    connect(fileTimestampAfterAction, &QAction::triggered, this, [this](){ switchVersioningFormat(FileTimestampAfter); });
    connect(folderTimestampAction, &QAction::triggered, this, [this](){ switchVersioningFormat(FolderTimestamp); });
    connect(lastVersionAction, &QAction::triggered, this, [this](){ switchVersioningFormat(LastVersion); });
    connect(versioningPostfixAction, &QAction::triggered, this, [this](){ setVersioningPostfix(); });
    connect(versioningPatternAction, &QAction::triggered, this, [this](){ setVersioningPattern(); });
    connect(locallyNextToFolderAction, &QAction::triggered, this, [this](){ switchVersioningLocation(LocallyNextToFolder); });
    connect(customLocationAction, &QAction::triggered, this, [this](){ switchVersioningLocation(CustomLocation); });
    connect(customLocationPathAction, &QAction::triggered, this, [this](){ setVersioningLocationPath(); });
    connect(databaseLocallyAction, &QAction::triggered, this, [this](){ switchDatabaseLocation(Locally); });
    connect(databaseDecentralizedAction, &QAction::triggered, this, [this](){ switchDatabaseLocation(Decentralized); });
    connect(fileMinSizeAction, &QAction::triggered, this, [this](){ setFileMinSize(); });
    connect(fileMaxSizeAction, &QAction::triggered, this, [this](){ setFileMaxSize(); });
    connect(movedFileMinSizeAction, &QAction::triggered, this, [this](){ setMovedFileMinSize(); });
    connect(deltaCopyingMinSizeAction, &QAction::triggered, this, [this](){ setDeltaCopyingMinSize(); });
    connect(includeAction, &QAction::triggered, this, [this](){ setIncludeList(); });
    connect(excludeAction, &QAction::triggered, this, [this](){ setExcludeList(); });
    connect(ignoreHiddenFilesAction, &QAction::triggered, this, [this](){ toggleIgnoreHiddenFiles(); });
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
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(m_syncEvery)));
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
    syncingTimeAction->setText(syncApp->translate("Synchronize Every") + QString(": %1").arg(formatTime(m_syncEvery)));
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

/*
===================
SyncProfile::switchSyncingMode
===================
*/
void SyncProfile::switchSyncingMode(SyncingMode mode)
{
    if (mode < Manual || mode > AutomaticFixed)
        mode = AutomaticAdaptive;

    setSyncingMode(mode);
    manualAction->setChecked(mode == Manual);
    automaticAdaptiveAction->setChecked(mode == AutomaticAdaptive);
    automaticFixedAction->setChecked(mode == AutomaticFixed);
    increaseSyncTimeAction->setVisible(mode == AutomaticAdaptive);
    syncingTimeAction->setVisible(mode == AutomaticAdaptive);
    decreaseSyncTimeAction->setVisible(mode == AutomaticAdaptive);
    fixedSyncingTimeAction->setVisible(mode == AutomaticFixed);

    if (mode == Manual)
    {
        m_syncTimer.stop();
    }
    // Otherwise, automatic
    else
    {
        updateNextSyncingTime();
        updateTimer();
    }

    emit syncingModeChanged();
    emit syncingTimeChanged();

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::increaseSyncTime
===================
*/
void SyncProfile::increaseSyncTime()
{
    quint64 max = SyncManager::maxInterval();

    // If exceeds the maximum value of an qint64
    if (m_syncEvery >= max)
    {
        updateMenuSyncTime();
        return;
    }

    setSyncTimeMultiplier(syncTimeMultiplier() + 1);
    decreaseSyncTimeAction->setEnabled(true);
    updateMenuSyncTime();
    updateTimer();
    emit syncingTimeChanged();

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::decreaseSyncTime
===================
*/
void SyncProfile::decreaseSyncTime()
{
    setSyncTimeMultiplier(syncTimeMultiplier() - 1);
    updateMenuSyncTime();
    updateTimer();
    emit syncingTimeChanged();

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setFixedInterval
===================
*/
void SyncProfile::setFixedInterval()
{
    QString title(tr("Synchronize Every"));
    QString text(tr("Please enter the synchronization interval in seconds:"));
    int size;

    if (!syncApp->intInputDialog(nullptr, title, text, size, syncIntervalFixed() / 1000, 0))
        return;

    setSyncIntervalFixed(size * 1000);
    updateMenuSyncTime();

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::detectMovedFiles
===================
*/
void SyncProfile::toggleDetectMoved()
{
    setDetectMovedFiles(!detectMovedFiles());

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::switchDeletionMode
===================
*/
void SyncProfile::switchDeletionMode(DeletionMode mode)
{
    if (mode < MoveToTrash || mode > DeletePermanently)
        mode = MoveToTrash;

    if (syncApp->initiated() && mode == DeletePermanently && mode != deletionMode())
    {
        QString title(tr("Switch deletion mode to delete files permanently?"));
        QString text(tr("Are you sure? Beware: this could lead to data loss!"));

        if (!syncApp->questionBox(QMessageBox::Warning, title, text, QMessageBox::No, nullptr))
            mode = deletionMode();
    }

    setDeletionMode(mode);
    moveToTrashAction->setChecked(mode == MoveToTrash);
    versioningAction->setChecked(mode == Versioning);
    deletePermanentlyAction->setChecked(mode == DeletePermanently);
    versioningFormatMenu->menuAction()->setVisible(mode == Versioning);
    versioningLocationMenu->menuAction()->setVisible(mode == Versioning);

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::switchVersioningFormat
===================
*/
void SyncProfile::switchVersioningFormat(VersioningFormat format)
{
    if (format < FileTimestampBefore || format > LastVersion)
        format = FileTimestampAfter;

    fileTimestampBeforeAction->setChecked(format == FileTimestampBefore);
    fileTimestampAfterAction->setChecked(format == FileTimestampAfter);
    folderTimestampAction->setChecked(format == FolderTimestamp);
    lastVersionAction->setChecked(format == LastVersion);
    setVersioningFormat(format);

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningPostfix
===================
*/
void SyncProfile::setVersioningPostfix()
{
    QString postfix = versioningFolder();
    QString title(tr("Versioning Folder Postfix"));
    QString text(tr("Please enter the versioning folder postfix:"));

    if (!syncApp->textInputDialog(nullptr, title, text, postfix, postfix))
        return;

    setVersioningFolder(postfix);
    versioningPostfixAction->setText(QString("&" + tr("Folder Postfix: %1")).arg(versioningFolder()));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningPattern
===================
*/
void SyncProfile::setVersioningPattern()
{
    QString pattern = versioningPattern();
    QString title(tr("Versioning Pattern"));
    QString text(tr("Please enter the versioning pattern:"));
    text.append("\n\n");
    text.append(tr("Examples:"));
    text.append("\nyyyy_M_d_h_m_s_z - 2001_5_21_14_13_09_120");
    text.append("\nyyyy_MM_dd - 2001_05_21");
    text.append("\nyy_MMMM_d - 01_May_21");
    text.append("\nhh_mm_ss_zzz - 14_13_09_120");
    text.append("\nap_h_m_s - pm_2_13_9");

    if (!syncApp->textInputDialog(nullptr, title, text, pattern, pattern))
        return;

    setVersioningPattern(pattern);
    versioningPatternAction->setText(QString("&" + tr("Pattern: %1")).arg(versioningPattern()));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::switchVersioningLocation
===================
*/
void SyncProfile::switchVersioningLocation(VersioningLocation location)
{
    if (location < LocallyNextToFolder || location > CustomLocation)
        location = LocallyNextToFolder;

    versioningPostfixAction->setVisible(location == LocallyNextToFolder);
    locallyNextToFolderAction->setChecked(location == LocallyNextToFolder);
    customLocationAction->setChecked(location == CustomLocation);
    customLocationPathAction->setVisible(location == CustomLocation);
    setVersioningLocation(location);

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setVersioningLocationPath
===================
*/
void SyncProfile::setVersioningLocationPath()
{
    if (versioningLocation() == CustomLocation && syncApp->initiated())
    {
        QString title(tr("Browse for Versioning Folder"));
        QString path = QFileDialog::getExistingDirectory(nullptr, title, QStandardPaths::writableLocation(QStandardPaths::HomeLocation), QFileDialog::ShowDirsOnly);

        if (!path.isEmpty())
        {
            setVersioningPath(path);
            customLocationPathAction->setText(tr("Custom Location: ") + path);
        }
    }

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::switchDatabaseLocation
===================
*/
void SyncProfile::switchDatabaseLocation(DatabaseLocation location)
{
    if (location < Locally || location > Decentralized)
        location = Decentralized;

    databaseLocallyAction->setChecked(location == false);
    databaseDecentralizedAction->setChecked(location == true);
    setDatabaseLocation(location);

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setFileMinSize
===================
*/
void SyncProfile::setFileMinSize()
{
    QString title(tr("Minimum File Size"));
    QString text(tr("Please enter the minimum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(nullptr, title, text, size, fileMinSize(), 0))
        return;

    if (size && fileMaxSize() && static_cast<quint64>(size) > fileMaxSize())
        size = fileMaxSize();

    setFileMinSize(size);
    fileMinSizeAction->setText("&" + tr("Minimum File Size: %1").arg(formatSize(fileMinSize())));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setFileMaxSize
===================
*/
void SyncProfile::setFileMaxSize()
{
    QString title(tr("Maximum File Size"));
    QString text(tr("Please enter the maximum size in bytes:"));
    int size;

    if (!syncApp->intInputDialog(nullptr, title, text, size, fileMaxSize(), 0))
        return;

    if (size && static_cast<quint64>(size) < fileMinSize())
        size = fileMinSize();

    setFileMaxSize(size);
    fileMaxSizeAction->setText("&" + tr("Maximum File Size: %1").arg(formatSize(fileMaxSize())));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setMovedFileMinSize
===================
*/
void SyncProfile::setMovedFileMinSize()
{
    QString title(tr("Minimum Size for Moved File"));
    QString text(tr("Please enter the minimum size for a moved file in bytes:"));
    int size;

    if (!syncApp->intInputDialog(nullptr, title, text, size, movedFileMinSize(), 0))
        return;

    setMovedFileMinSize(size);
    movedFileMinSizeAction->setText("&" + tr("Minimum Size for a Moved File: %1").arg(formatSize(movedFileMinSize())));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setDeltaCopyingMinSize
===================
*/
void SyncProfile::setDeltaCopyingMinSize()
{
    QString title(tr("Minimum Size for Delta Copying"));
    QString text(tr("Please enter the minimum size for delta copying in bytes:"));
    int size;

    if (!syncApp->intInputDialog(nullptr, title, text, size, deltaCopyingMinSize(), 0))
        return;

    setDeltaCopyingMinSize(size);
    deltaCopyingMinSizeAction->setText("&" + tr("Minimum Size for delta copying: %1").arg(formatSize(deltaCopyingMinSize())));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setIncludeList
===================
*/
void SyncProfile::setIncludeList()
{
    QString includeString = includeList().join("; ");
    QString title(tr("Include List"));
    QString text(tr("Please enter include list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(nullptr, title, text, includeString, includeString))
        return;

    QStringList includeList = includeString.split(";");

    for (auto &include : includeList)
        include = include.trimmed();

    includeString = includeList.join("; ");
    setIncludeList(includeList);
    includeAction->setText("&" + tr("Include: %1").arg(includeString));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::setExcludeList
===================
*/
void SyncProfile::setExcludeList()
{
    QString excludeString = excludeList().join("; ");
    QString title(tr("Exclude List"));
    QString text(tr("Please enter exclude list, separated by semicolons. Wildcards (e.g., *.txt) are supported."));

    if (!syncApp->textInputDialog(nullptr, title, text, excludeString, excludeString))
        return;

    QStringList excludeList = excludeString.split(";");

    for (auto &exclude : excludeList)
        exclude = exclude.trimmed();

    excludeString = excludeList.join("; ");
    setExcludeList(excludeList);
    excludeAction->setText("&" + tr("Exclude: %1").arg(excludeString));

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::toggleIgnoreHiddenFiles
===================
*/
void SyncProfile::toggleIgnoreHiddenFiles()
{
    setIgnoreHiddenFiles(!ignoreHiddenFiles());

    if (syncApp->initiated())
        saveSettings();
}

/*
===================
SyncProfile::updateMenuSyncTime
===================
*/
void SyncProfile::updateMenuSyncTime()
{
    quint64 time = 0;
    QAction *action = nullptr;

    if (syncingMode() == AutomaticAdaptive)
    {
        time = m_syncEvery;
        action = syncingTimeAction;
    }
    else if (syncingMode() == AutomaticFixed)
    {
        time = syncIntervalFixed();
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

    if (syncTimeMultiplier() <= 1)
        decreaseSyncTimeAction->setEnabled(false);
}

/*
===================
SyncProfile::addActionsToMenu
===================
*/
void SyncProfile::addActionsToMenu(QMenu *menu)
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
SyncProfile::enableContextMenus
===================
*/
void SyncProfile::enableContextMenus(bool enable)
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
