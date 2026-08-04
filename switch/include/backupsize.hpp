/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#ifndef BACKUPSIZE_HPP
#define BACKUPSIZE_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <switch.h>
#include <thread>

// Computes and caches the on-SD size of a title's backup folder off the UI
// thread. The recursive directory walk is the expensive part (many small SD
// reads), so it runs on a background worker; the UI requests a size, shows a
// placeholder, and reads the cached value once it lands. Without this the walk
// ran inside draw() on every title change and stalled the whole UI.
//
// The walk visits every individual backup subfolder anyway, so it caches each
// one's size on the way (keyed by full path) for the per-row size labels.
class BackupSizeCache {
public:
    static BackupSizeCache& get(void)
    {
        static BackupSizeCache instance;
        return instance;
    }

    // Queues an off-thread compute of the total for `id` rooted at `rootPath`.
    // Cheap and idempotent: no-ops if already cached or in flight, so it is safe
    // to call every frame.
    void request(u64 id, const std::string& rootPath);

    // Cached total bytes for `id`, or nullopt while still computing.
    std::optional<u64> total(u64 id);

    // Cached size of a single backup folder (by full path) under `id`, or nullopt.
    std::optional<u64> backupSize(u64 id, const std::string& fullPath);

    // Drop the cached sizes for `id` so the next request recomputes — call after
    // a backup/restore/delete changes the title's folders.
    void invalidate(u64 id);

    // Monotonic counter bumped whenever cached sizes change (a compute lands or an
    // invalidation drops entries). The UI compares it against the value it
    // snapshotted to know when its size labels went stale.
    u32 generation(void) const { return mGeneration.load(); }

    // Suspend/resume the walk. The walk floods the FS service with small SD
    // requests, which delays latency-sensitive FS users in *other* processes —
    // observed as the swkbd applet taking the whole remaining walk (10s+) to
    // appear. Thread priority can't fix that (the contention is inside the FS
    // sysmodule, not on our cores), so callers about to launch an applet pause
    // the walk; it blocks between entries until resumed. Calls nest.
    void pause(void);
    void resume(void);

    // RAII pause for the duration of a scope (e.g. a swkbd session).
    struct PauseGuard {
        PauseGuard(void) { BackupSizeCache::get().pause(); }
        ~PauseGuard(void) { BackupSizeCache::get().resume(); }
        PauseGuard(const PauseGuard&)            = delete;
        PauseGuard& operator=(const PauseGuard&) = delete;
    };

    // Stop the worker and join it. Called at application shutdown; any in-flight
    // walk returns promptly (checked between top-level backup folders).
    void shutdown(void);

private:
    BackupSizeCache(void)                              = default;
    ~BackupSizeCache(void)                             = default;
    BackupSizeCache(const BackupSizeCache&)            = delete;
    BackupSizeCache& operator=(const BackupSizeCache&) = delete;

    struct Entry {
        u64 total = 0;
        std::map<std::string, u64> perBackup; // backup full path -> bytes
    };

    void ensureWorker(void);
    void workerLoop(void);
    void compute(u64 id, const std::string& rootPath);
    // Recursive subtree size like io::scanTree, but calls gate() between
    // entries so pause() takes effect mid-walk and mStop aborts promptly.
    u64 walkSize(const std::string& path);
    // Blocks while paused; returns immediately when running or stopping.
    void gate(void);

    std::mutex mMutex;
    std::condition_variable mCond;
    std::thread mWorker;
    std::deque<std::pair<u64, std::string>> mQueue; // (id, rootPath) awaiting compute
    std::map<u64, Entry> mCache;
    std::set<u64> mPending; // ids with a compute queued or in flight
    std::set<u64> mDirty;   // ids invalidated while a compute was in flight
    std::atomic<bool> mStop{false};
    std::atomic<int> mPauseCount{0};
    std::atomic<u32> mGeneration{1};
};

#endif // BACKUPSIZE_HPP
