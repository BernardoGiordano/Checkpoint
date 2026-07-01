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
#include "thread.hpp"
#include "util.hpp"
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>
#include <vector>

void BackupSizeCache::request(u64 id, BackupKind kind, const std::u16string& rootPath)
{
    const u64 k = key(id, kind);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAbort.load() || mTotals.count(k) != 0 || mPending.count(k) != 0) {
            return;
        }
        mPending.insert(k);
    }
    // Copy the path into the task; the worker owns it for the duration of the walk.
    Threads::executeTask([this, k, path = rootPath]() { this->compute(k, path); });
}

std::optional<u64> BackupSizeCache::total(u64 id, BackupKind kind)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mTotals.find(key(id, kind));
    if (it == mTotals.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<u64> BackupSizeCache::backupSize(const std::u16string& fullPath)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mBackupSizes.find(fullPath);
    if (it == mBackupSizes.end()) {
        return std::nullopt;
    }
    return it->second;
}

void BackupSizeCache::invalidate(u64 id, BackupKind kind, const std::u16string& rootPath)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mTotals.erase(key(id, kind));
    // Drop every per-backup entry sitting under this root.
    for (auto it = mBackupSizes.begin(); it != mBackupSizes.end();) {
        if (it->first.compare(0, rootPath.size(), rootPath) == 0) {
            it = mBackupSizes.erase(it);
        }
        else {
            ++it;
        }
    }
}

void BackupSizeCache::invalidateAll(void)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mTotals.clear();
    mBackupSizes.clear();
}

void BackupSizeCache::shutdown(void)
{
    mAbort.store(true);
}

u64 BackupSizeCache::walk(const std::string& path)
{
    if (mAbort.load()) {
        return 0;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return 0;
    }
    u64 total = 0;
    struct dirent* ent;
    while (!mAbort.load() && (ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        std::string full = path + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += walk(full);
            }
            else {
                total += (u64)st.st_size;
            }
        }
    }
    closedir(dir);
    return total;
}

void BackupSizeCache::compute(u64 cacheKey, std::u16string rootPath)
{
    const std::string root = StringUtils::UTF16toUTF8(rootPath);
    u64 total              = 0;
    std::vector<std::pair<std::u16string, u64>> perBackup;

    DIR* dir = opendir(root.c_str());
    if (dir) {
        struct dirent* ent;
        while (!mAbort.load() && (ent = readdir(dir)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            std::string full = root + "/" + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                u64 sz = walk(full);
                total += sz;
                perBackup.emplace_back(rootPath + u"/" + StringUtils::UTF8toUTF16(ent->d_name), sz);
            }
            else {
                total += (u64)st.st_size;
            }
        }
        closedir(dir);
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mPending.erase(cacheKey);
    // Discard a partial result produced while shutting down.
    if (mAbort.load()) {
        return;
    }
    mTotals[cacheKey] = total;
    for (auto& pb : perBackup) {
        mBackupSizes[pb.first] = pb.second;
    }
}
