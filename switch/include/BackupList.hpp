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
#include <vector>

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
    // Real backups only, i.e. excluding the synthetic "New..." entry the model
    // keeps at index 0 (used for the "BACKUPS · N" header count).
    size_t backupCount(void) const { return mScrollable.size() > 0 ? mScrollable.size() - 1 : 0; }
    size_t index(void) { return mScrollable.index(); }
    std::string cellName(size_t i) { return i < size() ? mScrollable.cellName(i) : std::string(); }
    // Human-readable on-disk size of every backup for this title combined
    // (empty when there are no backups). Shown in the backups header.
    const std::string& totalSizeString(void) const { return mTotalSize; }
    void setIndex(size_t i) { mScrollable.setIndex(i); }
    void resetIndex(void) { mScrollable.resetIndex(); }
    void updateSelection(void) { mScrollable.updateSelection(); }

    // Draws the backup rows directly (rounded 48px rows, mono names, a
    // per-backup user chip); mScrollable is kept only for the
    // scroll/selection/touch state. `focused` means the list (not the grid)
    // currently owns the cursor: the selected row then gets the accent-tint
    // background.
    void draw(bool focused);

    // Picks the destination folder for a backup. cellIndex 0 = a new folder
    // (named by the keyboard, or the date-time stamp during a
    // multi-selection); cellIndex > 0 = overwrite the chosen existing
    // backup. Returns nullopt if the keyboard prompt was cancelled. Sets
    // usedKeyboardFallback when the system keyboard was unavailable and the
    // suggested name was used instead.
    static std::optional<std::string> chooseDst(Title& title, size_t cellIndex, bool& usedKeyboardFallback);

    // Row metrics: 48px rows, 6px gap → 54px pitch. The Scrollable is
    // built with a matching height (visibleRows * pitch) so its touch math and
    // the rows drawn here line up.
    static constexpr int ROW_H = 48, ROW_GAP = 6, ROW_PITCH = ROW_H + ROW_GAP;

private:
    // Rebuilds the row list from the resolved title and pulls cached backup
    // sizes. `sizesOnly` skips re-resolving the title (used when only the
    // BackupSizeCache generation changed, i.e. an async walk landed).
    void rebuild(bool sizesOnly);

    int mListX, mListY, mListW;
    Scrollable mScrollable;
    Title mTitle;
    // Per-row on-disk size strings, aligned with the Scrollable rows (index 0 is
    // the synthetic "New..." row and is left empty). Filled by rebuild().
    std::vector<std::string> mSizes;
    std::string mTotalSize;
    bool mValid              = false;
    AccountUid mUid          = {};
    saveTypeFilter_t mFilter = FILTER_SAVES;
    size_t mFilteredIdx      = 0;
    u32 mCatalogGen          = 0;
    u32 mSizeGen             = 0;
};

#endif
