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

#include "BackupList.hpp"
#include "colors.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>

BackupList::BackupList(int x, int y, int w, int h, size_t visibleRows) : mx(x), my(y), mw(w), mh(h), mVisibleRows(visibleRows), mCursor(visibleRows)
{
    mRowH = mh / (int)mVisibleRows;
}

void BackupList::setIndex(size_t i)
{
    mCursor.setCount(mRows.size());
    mCursor.setIndex(i);
}

void BackupList::resetIndex(void)
{
    mCursor.reset();
}

void BackupList::update(void)
{
    if (mRows.empty()) {
        return;
    }

    // Offset used to resolve a touch is the one currently on screen, before the
    // D-Pad move reclamps it — matching the original single-clamp-at-end order.
    const size_t touchOffset = mCursor.offset();

    // Rows are rebuilt every frame, so hand the current count to the cursor before
    // it navigates; this is where the selection is clamped, as clampOffset used to.
    mCursor.setCount(mRows.size());
    mCursor.update(hidKeysDown(), hidKeysHeld(), svcGetSystemTick());

    // Touch picks a visible row directly (BackupList owns the row geometry).
    if (hidKeysHeld() & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        if ((int)touch.px >= mx && (int)touch.px < mx + mw && (int)touch.py >= my && (int)touch.py < my + mh) {
            const size_t row = touchOffset + (size_t)((touch.py - my) / mRowH);
            if (row < mRows.size()) {
                mCursor.setIndex(row);
            }
        }
    }
}

void BackupList::draw(bool focused) const
{
    TextPool& text      = TextPool::get();
    const size_t offset = mCursor.offset();
    const size_t index  = mCursor.index();
    const size_t last   = std::min(offset + mVisibleRows, mRows.size());
    for (size_t i = offset; i < last; i++) {
        const Row& r   = mRows[i];
        const int rowY = my + (int)(i - offset) * mRowH;
        const bool sel = i == index;

        if (sel) {
            C2D_DrawRectSolid(mx, rowY, 0.5f, mw, mRowH, focused ? C2D_Color32(122, 66, 196, 70) : C2D_Color32(122, 66, 196, 40));
            C2D_DrawRectSolid(mx, rowY + 3, 0.5f, 2, mRowH - 6, COLOR_V4_RING); // left accent bar
        }

        const int textY = rowY + (mRowH - 13) / 2;

        // Leading marker: a teal "+" tile for "New backup", a small dot otherwise.
        if (r.isNew) {
            C2D_DrawRectSolid(mx + 6, rowY + (mRowH - 14) / 2, 0.5f, 14, 14, C2D_Color32(143, 227, 218, 40));
            text.draw("+", mx + 10, textY - 1, 0.5f, COLOR_V4_TEAL);
        }
        else {
            C2D_DrawRectSolid(mx + 9, rowY + mRowH / 2 - 2, 0.5f, 4, 4, sel ? COLOR_V4_RING : COLOR_V4_FAINT);
        }

        // Name (left) — teal for the New row, brighter when selected.
        const u32 nameColor = r.isNew ? COLOR_V4_TEAL : (sel ? COLOR_V4_TEXT : COLOR_V4_MUTED);
        text.draw(r.name, mx + 26, textY, 0.45f, nameColor);

        // Meta (right) — backup size, or the New-row caption.
        if (!r.meta.empty()) {
            const float metaW = text.width(r.meta, 0.4f);
            text.draw(r.meta, mx + mw - 10 - metaW, textY + 1, 0.4f, COLOR_V4_FAINT);
        }
    }

    // Scrollbar: only when there is more than one viewport of rows.
    if (mRows.size() > mVisibleRows) {
        const int trackX = mx + mw - 3;
        C2D_DrawRectSolid(trackX, my, 0.5f, 3, mh, COLOR_V4_LINE);
        const float frac = (float)mVisibleRows / (float)mRows.size();
        int thumbH       = (int)(mh * frac);
        if (thumbH < 12) {
            thumbH = 12;
        }
        const float posFrac = (float)offset / (float)(mRows.size() - mVisibleRows);
        const int thumbY    = my + (int)((mh - thumbH) * posFrac);
        C2D_DrawRectSolid(trackX, thumbY, 0.5f, 3, thumbH, COLOR_V4_ACCENT);
    }
}
