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

#include <citro2d.h>
#include <string>
#include <vector>

// A vertically scrolling list of backups for the v4 title-detail screen. Unlike
// the generic Scrollable (which paged horizontally and so never advanced past the
// first page on a vertical D-Pad), this scrolls smoothly: the selection moves
// through every row and the viewport follows it, with a scrollbar that makes the
// presence of more rows obvious.
//
// Each row carries a name and a right-aligned meta string (a backup's size, or a
// caption for the leading "New backup" row). Rebuild the rows every frame with
// clear()/push_back(); the component keeps its own selection and scroll offset.
class BackupList {
public:
    BackupList(int x, int y, int w, int h, size_t visibleRows);

    struct Row {
        std::string name;
        std::string meta;
        bool isNew = false;
    };

    void clear(void) { mRows.clear(); }
    void push_back(const std::string& name, const std::string& meta, bool isNew) { mRows.push_back({name, meta, isNew}); }

    size_t size(void) const { return mRows.size(); }
    size_t index(void) const { return mIndex; }
    std::string name(size_t i) const { return i < mRows.size() ? mRows[i].name : std::string(); }

    void setIndex(size_t i);
    void resetIndex(void);

    // Moves the selection from D-Pad / touch and keeps it in view. Call only while
    // the list owns input (the detail screen is focused).
    void update(void);

    // `focused` brightens the selection styling when the list owns input.
    void draw(bool focused) const;

private:
    void clampOffset(void);

    int mx, my, mw, mh;
    size_t mVisibleRows;
    int mRowH;
    size_t mIndex  = 0;
    size_t mOffset = 0; // index of the first visible row
    u64 mLastTick  = 0;
    std::vector<Row> mRows;
};

#endif // BACKUPLIST_HPP
