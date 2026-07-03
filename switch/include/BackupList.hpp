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

#ifndef BACKUPLIST_HPP
#define BACKUPLIST_HPP

#include "account.hpp"
#include "scrollable.hpp"
#include "title.hpp"
#include <optional>
#include <string>

// Owns the detail-panel backup list for the currently selected title: the
// Scrollable widget backing it and a cached copy of the resolved Title.
class BackupList {
public:
    BackupList(int x, int y, int w, int h, size_t visibleRows);

    // Re-resolves the title at the filtered index `filteredIdx` (for `uid`
    // under `filter`) and rebuilds the row list, but only if uid, filter,
    // filteredIdx, or catalogGen differ from the last call. Always safe to
    // call every frame.
    void refreshSelected(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx, u32 catalogGen);

    // Title's accessors are not const-qualified (they lazily format display
    // strings), so this returns a mutable reference to the cached copy.
    Title& title(void) { return mTitle; }

    size_t size(void) const { return mScrollable.size(); }
    size_t index(void) { return mScrollable.index(); }
    std::string cellName(size_t i) { return i < size() ? mScrollable.cellName(i) : std::string(); }
    void setIndex(size_t i) { mScrollable.setIndex(i); }
    void resetIndex(void) { mScrollable.resetIndex(); }
    void updateSelection(void) { mScrollable.updateSelection(); }

    // Syncs the selection highlight to the current scroll cursor (cheap,
    // touches existing rows only) and draws.
    void draw(bool focused);

    // Picks the destination folder for a backup. cellIndex 0 = a new folder
    // (named by the keyboard, or the date-time stamp during a
    // multi-selection); cellIndex > 0 = overwrite the chosen existing
    // backup. Returns nullopt if the keyboard prompt was cancelled. Sets
    // usedKeyboardFallback when the system keyboard was unavailable and the
    // suggested name was used instead.
    static std::optional<std::string> chooseDst(Title& title, size_t cellIndex, bool& usedKeyboardFallback);

private:
    void rebuild(void);

    Scrollable mScrollable;
    Title mTitle;
    bool mValid              = false;
    AccountUid mUid          = {};
    saveTypeFilter_t mFilter = FILTER_SAVES;
    size_t mFilteredIdx      = 0;
    u32 mCatalogGen          = 0;
};

#endif
