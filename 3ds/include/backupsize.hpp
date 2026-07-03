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

#include "archive.hpp" // BackupKind
#include <3ds/types.h>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>

// Computes and caches the on-SD size of a title's backup folder off the UI
// thread. The recursive directory walk is the expensive part (lots of small SD
// reads), so it runs on a worker; the UI requests a size, shows a placeholder,
// and reads the cached value once it lands.
//
// The recursive walk also visits every individual backup subfolder, so it caches
// each one's size on the way (keyed by full path) — free to keep, and ready for
// per-backup-row size labels later.
//
// A Meyer's singleton, matching TitleCatalog / TransferJob / Configuration.
class BackupSizeCache {
public:
    static BackupSizeCache& get(void)
    {
        static BackupSizeCache instance;
        return instance;
    }

    // Queues an off-thread compute of the total for (id, kind) rooted at
    // `rootPath`. Cheap and idempotent: no-ops if already cached or in flight, so
    // it is safe to call every frame.
    void request(u64 id, BackupKind kind, const std::u16string& rootPath);

    // Cached total bytes for (id, kind), or nullopt while still computing.
    std::optional<u64> total(u64 id, BackupKind kind);

    // Cached size of a single backup folder by full path, or nullopt.
    std::optional<u64> backupSize(const std::u16string& fullPath);

    // Drop the cached total (and per-backup entries under `rootPath`) so the next
    // request recomputes — call after a backup/restore/delete changes the folder.
    void invalidate(u64 id, BackupKind kind, const std::u16string& rootPath);

    // Drop everything (e.g. after a multi-title batch that touched many folders).
    void invalidateAll(void);

    // Monotonic counter bumped whenever cached sizes change (a compute lands or
    // an invalidation drops entries). The UI compares it against the value it
    // snapshotted to know when its size labels went stale.
    u32 generation(void) const { return mGeneration.load(); }

    // Cooperative abort for application shutdown: any in-flight walk returns
    // promptly so the worker pool can be joined without waiting on a long scan.
    void shutdown(void);
    static void shutdownStatic(void) { get().shutdown(); }

private:
    BackupSizeCache(void)                              = default;
    ~BackupSizeCache(void)                             = default;
    BackupSizeCache(const BackupSizeCache&)            = delete;
    BackupSizeCache& operator=(const BackupSizeCache&) = delete;

    static u64 key(u64 id, BackupKind kind) { return (id << 1) | (kind == BackupKind::Extdata ? 1u : 0u); }

    // Worker entry: enumerate the backup folders under `rootPath`, summing each
    // (and caching the per-backup totals), then store the grand total.
    void compute(u64 cacheKey, std::u16string rootPath);
    // Recursive, abort-aware size of a single directory subtree.
    u64 walk(const std::string& path);

    std::mutex mMutex;
    std::map<u64, u64> mTotals;                 // (id,kind) key -> total bytes
    std::map<std::u16string, u64> mBackupSizes; // backup full path -> bytes
    std::set<u64> mPending;                     // keys with a compute in flight
    std::atomic<bool> mAbort{false};
    std::atomic<u32> mGeneration{1};
};

#endif // BACKUPSIZE_HPP
