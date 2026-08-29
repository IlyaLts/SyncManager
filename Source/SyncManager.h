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

#ifndef SYNCMANAGER_H
#define SYNCMANAGER_H

#include "SyncProfile.h"
#include <QQueue>

#define DATA_FOLDER_PATH ".SyncManager"
#define DATABASE_FILENAME "db"
#define DATABASE_VERSION 5

static constexpr quint64 SyncMinDelay = 1000;

/*
===========================================================

    SyncManager

===========================================================
*/
class SyncManager : public QObject
{
    Q_OBJECT

public:

    SyncManager();
    ~SyncManager();

    void loadSettings();
    void saveSettings() const;

    void addToQueue(SyncProfile *profile);
    inline bool hasInQueue(const SyncProfile *profile) const { return m_queue.contains(profile); };
    inline qsizetype numberInQueue(const SyncProfile *profile) const { return m_queue.indexOf(profile); };
    inline qsizetype queueSize() const { return m_queue.size(); };

    void sync();

    void updateStatus();
    void purgeRemovedProfiles();

    inline std::list<SyncProfile> &profiles() { return m_profiles; }
    inline const std::list<SyncProfile> &profiles() const { return m_profiles; }

    inline void quit() { m_quit = true; }
    inline void setPaused(bool paused) { m_paused = paused; }

    inline int filesToSync() const { return m_filesToSync; }
    inline int existingProfiles() const { return m_existingProfiles; }
    inline bool quitting() const { return m_quit; }
    inline bool issue() const { return m_issue; }
    inline bool warning() const { return m_warning; }
    inline bool busy() const { return m_busy; }
    inline bool paused() const { return m_paused; }
    inline bool syncing() const { return m_syncing; }
    bool hasManualSyncProfile() const;
    bool inPausedState() const;

    static quint64 maxInterval();

Q_SIGNALS:

    void profileStatusChanged(SyncProfile *profile, bool syncing);
    void profileSynced(SyncProfile *profile);
    void profileRemoved(SyncProfile *profile);
    void finished();

private:

    bool syncProfile(SyncProfile &profile);
    bool scanFolders(SyncProfile &profile);
    void printDebugInfo(const SyncProfile &profile);

    QQueue<SyncProfile *> m_queue;
    std::list<SyncProfile> m_profiles;

    qsizetype m_filesToSync = 0;
    int m_existingProfiles = 0;
    bool m_quit = false;
    bool m_issue = true;
    bool m_warning = false;
    bool m_busy = false;
    bool m_paused = false;
    bool m_syncing = false;

    QSet<hash64_t> m_usedDevices;
};

#endif // SYNCMANAGER_H
