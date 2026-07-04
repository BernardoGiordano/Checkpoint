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

#ifndef TITLECATALOG_HPP
#define TITLECATALOG_HPP

#include "iconstore.hpp"
#include "savekind.hpp"
#include "title.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

// The single owner of the in-process title lists (one vector per AccountUid, with
// the BCAT/Device/System singletons appended to every user), the icon textures
// they reference, and the current sort mode.
//
// Queries take the AccountUid (and, where relevant, the save-type filter)
// explicitly and hand back a *copy* of the Title: the catalog never leaks a
// reference into a vector a reload could clear. TitleProbe is the producer it
// drives per entry during loadTitles.
class TitleCatalog {
public:
    static TitleCatalog& get(void)
    {
        static TitleCatalog instance;
        return instance;
    }

    // Enumerate every installed save (User + System space) and rebuild the lists.
    void loadTitles(void);

    // Raw (unfiltered) queries over one user's list.
    void getTitle(Title& dst, AccountUid uid, size_t i);
    size_t getTitleCount(AccountUid uid);

    // Filtered queries: the UI shows one save-type at a time.
    size_t getFilteredTitleCount(AccountUid uid, saveTypeFilter_t filter);
    void getFilteredTitle(Title& dst, AccountUid uid, saveTypeFilter_t filter, size_t i);
    size_t filteredToRawIndex(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx);
    bool filteredFavorite(AccountUid uid, saveTypeFilter_t filter, int i);
    SDL_Texture* filteredSmallIcon(AccountUid uid, saveTypeFilter_t filter, size_t i);

    // Big icon of the title with this id (the selected title in the side panel).
    SDL_Texture* iconFor(u64 id);

    // Sort the lists in place; rotate to the next sort mode and re-sort. Both
    // persist the new mode through Configuration::setSortMode so the grid's
    // X-button cycle and Settings' "Default sort" spinner — the same setting
    // — survive a relaunch.
    void sortTitles(void);
    void rotateSortMode(void);
    void setSortMode(sort_t mode);
    sort_t sortMode(void) const { return mSortMode; }

    // Re-scan the backup folders of the title with this id (after a backup).
    void refreshDirectories(u64 id);

    // Re-applies Configuration's hidden-id set to every user's filter
    // buckets. Called by Settings > Library after hiding/unhiding a title —
    // hiding is a global (by id) setting, not scoped to one user, so every
    // uid's buckets need rebuilding, not just the currently active one.
    void refreshHiddenFilter(void);

    // Bumped whenever a load/sort/directory-refresh changes what a raw or
    // filtered index refers to, or what backups exist for it. Cache holders
    // (BackupList) compare this against a stored value to know a rebuild is
    // needed instead of unconditionally rebuilding every frame.
    u32 generation(void) const { return mGeneration; }

    // Destroy every owned icon texture (called on exit).
    void freeIcons(void);

    // id -> name map of every known title (used to seed the filter configuration).
    std::unordered_map<std::string, std::string> getCompleteTitleList(void);

    // (id, name) of every title owning a save of this FsSaveDataType, unique by
    // id across users and sorted by name. Feeds the Settings > Save folders
    // title pickers, so they only offer titles the folder can apply to (and
    // system/BCAT titles stay out of the user/device lists).
    std::vector<std::pair<u64, std::string>> titleListForSaveType(u8 saveDataType);

private:
    // Seeds mSortMode from Configuration's persisted setting.
    TitleCatalog(void);
    ~TitleCatalog(void)                          = default;
    TitleCatalog(const TitleCatalog&)            = delete;
    TitleCatalog& operator=(const TitleCatalog&) = delete;

    bool favorite(AccountUid uid, int i);

    // Rebuilds mFilterIndex[uid] from mTitles[uid]: one raw-index vector per
    // saveTypeFilter_t row, so a filtered query stops being an O(n) scan over
    // the raw list. Called once per user whenever a load or a sort changes
    // what a raw index refers to (membership or order); refreshDirectories
    // touches neither, so it does not need to rebuild. Titles hidden through
    // Configuration::filter() are left out of every bucket — hiding/unhiding
    // from Settings > Library calls this again (via a generation bump) to
    // apply the change.
    void rebuildFilterIndex(AccountUid uid);

    static constexpr size_t FILTER_COUNT = 4; // one row per SaveKind::all() entry

    std::unordered_map<AccountUid, std::vector<Title>> mTitles;
    std::unordered_map<AccountUid, std::array<std::vector<size_t>, FILTER_COUNT>> mFilterIndex;
    TextureIconStore mIcons;
    sort_t mSortMode;
    u32 mGeneration = 0;
};

#endif // TITLECATALOG_HPP
