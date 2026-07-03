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

#include "backupsize.hpp"
#include "directory.hpp"
#include "io.hpp"
#include <sys/stat.h>

void BackupSizeCache::ensureWorker(void)
{
    // Caller holds mMutex. Spawn the single worker lazily on the first request.
    if (!mWorker.joinable() && !mStop.load()) {
        mWorker = std::thread([this]() { this->workerLoop(); });
    }
}

void BackupSizeCache::request(u64 id, const std::string& rootPath)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mStop.load() || mCache.count(id) != 0 || mPending.count(id) != 0) {
        return;
    }
    mPending.insert(id);
    mQueue.emplace_back(id, rootPath);
    ensureWorker();
    mCond.notify_one();
}

std::optional<u64> BackupSizeCache::total(u64 id)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mCache.find(id);
    if (it == mCache.end()) {
        return std::nullopt;
    }
    return it->second.total;
}

std::optional<u64> BackupSizeCache::backupSize(u64 id, const std::string& fullPath)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mCache.find(id);
    if (it == mCache.end()) {
        return std::nullopt;
    }
    auto bit = it->second.perBackup.find(fullPath);
    if (bit == it->second.perBackup.end()) {
        return std::nullopt;
    }
    return bit->second;
}

void BackupSizeCache::invalidate(u64 id)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCache.erase(id);
    // If a compute for this id is in flight, tell it to discard its (now stale)
    // result so the next request recomputes against the current folders.
    if (mPending.count(id) != 0) {
        mDirty.insert(id);
    }
    mGeneration.fetch_add(1);
}

void BackupSizeCache::shutdown(void)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStop.load()) {
            return;
        }
        mStop.store(true);
    }
    mCond.notify_all();
    if (mWorker.joinable()) {
        mWorker.join();
    }
}

void BackupSizeCache::workerLoop(void)
{
    for (;;) {
        std::pair<u64, std::string> task;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCond.wait(lock, [this]() { return mStop.load() || !mQueue.empty(); });
            if (mStop.load()) {
                return;
            }
            task = std::move(mQueue.front());
            mQueue.pop_front();
        }
        compute(task.first, task.second);
    }
}

void BackupSizeCache::compute(u64 id, const std::string& rootPath)
{
    // Enumerate the immediate backup folders, summing each subtree (and keeping
    // the per-backup totals). io::directorySize does the recursive walk; the
    // mStop check between folders lets a shutdown abort a long scan promptly.
    Entry entry;
    std::string base = rootPath;
    if (!base.empty() && base.back() != '/') {
        base += "/";
    }

    Directory items(base);
    if (items.good()) {
        for (size_t i = 0, sz = items.size(); i < sz && !mStop.load(); i++) {
            const std::string full = base + items.entry(i);
            if (items.folder(i)) {
                const u64 s = io::directorySize(full + "/");
                entry.total += s;
                entry.perBackup[full] = s;
            }
            else {
                struct stat st;
                if (stat(full.c_str(), &st) == 0) {
                    entry.total += (u64)st.st_size;
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mPending.erase(id);
    // Discard a partial result produced during shutdown, or one invalidated
    // while the walk was running.
    if (mStop.load() || mDirty.erase(id) != 0) {
        return;
    }
    mCache[id] = std::move(entry);
    mGeneration.fetch_add(1);
}
