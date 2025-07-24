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

#ifndef SYNCPROFILE_H
#define SYNCPROFILE_H

#include "SyncFolder.h"
#include "Common.h"
#include <QList>
#include <QChronoTimer>
#include <QMutex>
#include <QModelIndex>

class SyncFolder;
class UnhidableMenu;

/*
===========================================================

    SyncProfile

===========================================================
*/
class SyncProfile : public QObject
{
    Q_OBJECT

public:

    enum SyncingMode
    {
        Manual,
        AutomaticAdaptive,
        AutomaticFixed
    };

    enum DeletionMode
    {
        MoveToTrash,
        Versioning,
        DeletePermanently
    };

    enum DatabaseLocation
    {
        Locally,
        Decentralized
    };

    explicit SyncProfile(const QString &name, const QModelIndex &index);
    explicit SyncProfile(const SyncProfile &other) : SyncProfile(other.name, other.index) { *this = other; }
    explicit SyncProfile(SyncProfile &&other) : SyncProfile(other.name, other.index) { *this = other; }
    virtual ~SyncProfile(){}

    void operator =(const SyncProfile &other);
    inline bool operator ==(const SyncProfile &other) { return name == other.name; }

    inline void setSyncingMode(SyncingMode mode) { m_syncingMode = mode; }
    inline void setDeletionMode(DeletionMode mode) { m_deletionMode = mode; }
    inline void setDatabaseLocation(SyncProfile::DatabaseLocation location) { m_databaseLocation = location; }
    inline void setVersioningFormat(VersioningFormat format) { m_versioningFormat = format; }
    inline void setVersioningLocation(VersioningLocation location) { m_versioningLocation = location; }
    inline void setVersioningPath(QString path) { m_versioningPath = path; }
    inline void setVersioningFolder(const QString &name) { m_versioningFolder = name; }
    inline void setVersioningPattern(const QString &pattern) { m_versioningPattern = pattern; }
    inline void setFileMinSize(qint64 size) { m_fileMinSize = size; }
    inline void setFileMaxSize(qint64 size) { m_fileMaxSize = size; }
    inline void setMovedFileMinSize(qint64 size) { m_movedFileMinSize = size; }
    inline void setIncludeList(const QStringList &list) { m_includeList = list; }
    inline void setExcludeList(const QStringList &list) { m_excludeList = list; }
    inline void enableIgnoreHiddenFiles(bool enable) { m_ignoreHiddenFiles = enable; }
    inline void enableDetectMovedFiles(bool enable) { m_detectMovedFiles = enable; }

    inline SyncingMode syncingMode() const { return m_syncingMode; }
    inline int syncTimeMultiplier() const { return m_syncTimeMultiplier; }
    inline DeletionMode deletionMode() const { return m_deletionMode; }
    inline SyncProfile::DatabaseLocation databaseLocation() const { return m_databaseLocation; }
    inline VersioningFormat versioningFormat() const { return m_versioningFormat; }
    inline VersioningLocation versioningLocation() const { return m_versioningLocation; }
    inline QString versioningPath() const { return m_versioningPath; }
    inline const QString &versioningFolder() const { return m_versioningFolder; }
    inline const QString &versioningPattern() const { return m_versioningPattern; }
    inline qint64 fileMinSize() const { return m_fileMinSize; }
    inline qint64 fileMaxSize() const { return m_fileMaxSize; }
    inline qint64 movedFileMinSize() const { return m_movedFileMinSize; }
    inline const QStringList &includeList() const { return m_includeList; }
    inline const QStringList &excludeList() const { return m_excludeList; }
    inline bool ignoreHiddenFilesEnabled() const { return m_ignoreHiddenFiles; }
    inline bool detectMovedFilesEnabled() const { return m_detectMovedFiles; }

    bool resetLocks();
    void removeInvalidFileData();
    void saveDatabasesLocally() const;
    void saveDatabasesDecentralised() const;
    void loadDatabasesLocally();
    void loadDatebasesDecentralised();
    void addFilePath(hash64_t hash, const QByteArray &path);
    inline void clearFilePaths() { filePaths.clear(); }

    inline QByteArray filePath(Hash hash) const { return filePaths.value(hash); }
    inline bool hasFilePath(Hash hash) const { return filePaths.contains(hash); }
    bool isActive() const;
    bool isAutomatic() const;
    bool isTopFolderUpdated(const SyncFolder &folder, hash64_t hash) const;
    bool isAnyFolderCaseSensitive() const;
    bool hasExistingFolders() const;
    bool hasMissingFolders() const;
    SyncFolder *folderByIndex(QModelIndex index);
    SyncFolder *folderByPath(const QString &path);

    void setupMenus(QWidget *parent = nullptr);
    void destroyMenus();
    void loadSettings();
    void saveSettings() const;
    void updateStrings();

    std::list<SyncFolder> folders;

    QAction *manualAction;
    QAction *automaticAdaptiveAction;
    QAction *automaticFixedAction;
    QAction *detectMovedFilesAction;
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
    QAction *saveDatabaseLocallyAction;
    QAction *saveDatabaseDecentralizedAction;
    QAction *fileMinSizeAction;
    QAction *fileMaxSizeAction;
    QAction *movedFileMinSizeAction;
    QAction *includeAction;
    QAction *excludeAction;
    QAction *ignoreHiddenFilesAction;

    UnhidableMenu *syncingModeMenu;
    UnhidableMenu *deletionModeMenu;
    UnhidableMenu *versioningFormatMenu;
    UnhidableMenu *versioningLocationMenu;
    UnhidableMenu *databaseLocationMenu;
    UnhidableMenu *filteringMenu;

    QModelIndex index;
    SyncingMode m_syncingMode;
    DeletionMode m_deletionMode;
    VersioningLocation m_versioningLocation;
    VersioningFormat m_versioningFormat;
    QString m_versioningFolder;
    QString m_versioningPattern;
    QString m_versioningPath;
    DatabaseLocation m_databaseLocation = Decentralized;
    qint64 m_fileMinSize = 0;
    qint64 m_fileMaxSize = 0;
    qint64 m_movedFileMinSize = 0;
    QStringList m_includeList;
    QStringList m_excludeList;
    int m_syncTimeMultiplier = 1;
    bool m_detectMovedFiles = false;
    bool m_ignoreHiddenFiles = false;

    bool syncing = false;
    bool paused = false;
    bool toBeRemoved = false;
    bool syncHidden = false;
    quint64 syncEvery = 0;
    quint64 syncEveryFixed = 0;
    quint64 syncTime = 0;
    QChronoTimer syncTimer;
    QDateTime lastSyncDate;
    QString name;

private:

    QHash<Hash, QByteArray> filePaths;
    QMutex mutex;
};

#endif // SYNCPROFILE_H
