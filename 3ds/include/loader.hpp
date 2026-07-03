/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef LOADER_HPP
#define LOADER_HPP

#include "iconstore.hpp"
#include "title.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Value snapshot of the catalog's loading state. Owns the (done * 100) / total
// math the UI used to compute inline against three raw globals.
struct LoadProgress {
    bool active;
    int done;
    int total;
    int percent(void) const { return total == 0 ? 0 : (done * 100) / total; }
};

// The single owner of the in-process title lists (saves + extdata), their
// guarding mutex, the on-SD cache, the cartridge-scan loop, and the loading
// state. Meyer's singleton, matching Configuration / CheatManager.
//
// Reads hand back a *copy* of a Title on purpose: the background reload
// clear()s and refills the vectors, so a caller must hold a snapshot, not a
// dangling reference into a vector that is about to be cleared.
class TitleCatalog {
public:
    static TitleCatalog& get(void)
    {
        static TitleCatalog instance;
        return instance;
    }

    // Queries (each takes the mutex, copies out).
    void getTitle(Title& dst, int i, BackupKind kind);
    bool getTitleById(Title& dst, u64 id);
    // Copies out just the short description for a title id, avoiding a full Title
    // copy. Returns false if the id isn't in the catalog.
    bool nameById(std::string& dst, u64 id);
    bool getTitleByName(Title& dst, const std::string& name);
    int getTitleCount(BackupKind kind);
    C2D_Image icon(int i, BackupKind kind);
    bool favorite(int i, BackupKind kind);

    // Backup-directory refresh of titles already in the catalog.
    void refreshDirectories(u64 id);
    void refreshAllDirectories(void);

    // Loading-state snapshot for the UI.
    LoadProgress progress(void);

    // Monotonic counter bumped whenever the catalog's contents change (full
    // load, directory refresh, cart insert/remove). The UI compares it against
    // the value it snapshotted to know when cached title data went stale.
    u32 generation(void) const { return mGeneration.load(); }

    // Function-pointer entry points for Threads:: / ATEXIT. Static so they stay
    // plain void(*)() pointers; each forwards to get().
    static void loadTitlesThread(void);
    static void cartScan(void);
    static void cartScanFlagTestAndSet(void);
    static void clearCartScanFlag(void);

private:
    TitleCatalog(void)                           = default;
    ~TitleCatalog(void)                          = default;
    TitleCatalog(const TitleCatalog&)            = delete;
    TitleCatalog& operator=(const TitleCatalog&) = delete;

    bool validId(u64 id);
    bool scanCard(void);

    // loadTitles orchestrates these steps; each owns one phase. Every phase
    // works on caller-owned vectors so the whole load runs lock-free on
    // locals; loadTitles publishes the result with a swap under mMutex.
    void loadTitles(bool forceRefreshParam);
    bool isCacheFresh(void); // hash check; (re)writes the hash file as a side effect
    void loadFromCache(std::vector<Title>& saves, std::vector<Title>& extdatas, IconStore& icons);       // fast path: cache + refresh dirs
    void scanInstalledTitles(std::vector<Title>& saves, std::vector<Title>& extdatas, IconStore& icons); // slow path: NAND / SD / PKSM
    void appendCartTitle(std::vector<Title>& saves, std::vector<Title>& extdatas, IconStore& icons);     // prepend inserted game-card title
    void sortLists(std::vector<Title>& saves, std::vector<Title>& extdatas);                             // favorites-first, then by name
    void exportCaches(std::vector<Title>& saves, std::vector<Title>& extdatas);                          // serialize both lists to SD

    void exportTitleListCache(std::vector<Title>& list, const std::u16string& path);
    void importTitleListCache(std::vector<Title>& saves, std::vector<Title>& extdatas, IconStore& icons);

    std::vector<Title> mSaves;
    std::vector<Title> mExtdatas;
    // Owns every icon texture for the titles currently in mSaves/mExtdatas.
    // Guarded by mMutex like the lists: swapped in loadTitles, read by icon().
    IconStore mIcons;
    std::mutex mMutex;

    bool mForceRefresh           = false;
    std::atomic_flag mDoCartScan = ATOMIC_FLAG_INIT;

    std::atomic<bool> mLoading{false};
    std::atomic<int> mCounter{0};
    std::atomic<int> mLimit{0};
    std::atomic<u32> mGeneration{1};
};

#endif // LOADER_HPP
