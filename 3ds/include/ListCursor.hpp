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

#ifndef LISTCURSOR_HPP
#define LISTCURSOR_HPP

#include <3ds.h>
#include <cstddef>

// The selection + scroll-window arithmetic shared by vertical lists. Every list
// otherwise re-derives the same three things: the D-Pad auto-repeat timing, the
// wrap-around at both ends, and the clamping that keeps the selected row inside
// the viewport without leaving a trailing gap. This owns that logic once so the
// list components (e.g. BackupList) keep only drawing; it also becomes the single
// home of the auto-repeat delay constant that had drifted into two places.
class ListCursor {
public:
    // D-Pad auto-repeat delay, in system ticks.
    static constexpr u64 REPEAT_DELAY_TICKS = 50000000;

    explicit ListCursor(size_t visibleRows) : mVisibleRows(visibleRows) {}

    // Updates the item count, clamping the index and window when the list shrinks;
    // an empty list resets index and offset to 0.
    void setCount(size_t count)
    {
        mCount = count;
        clamp();
    }

    // Index and offset back to the top.
    void reset(void)
    {
        mIndex  = 0;
        mOffset = 0;
    }

    // Sets the selection, then re-clamps the window around it.
    void setIndex(size_t i)
    {
        mIndex = i;
        clamp();
    }

    size_t index(void) const { return mIndex; }
    size_t offset(void) const { return mOffset; }

    // KEY_UP/KEY_DOWN navigation. Edge presses move immediately and stamp the tick;
    // a held direction repeats once now passes lastTick + REPEAT_DELAY_TICKS.
    // Movement wraps at both ends. The window is re-clamped after any move.
    void update(u32 kDown, u32 kHeld, u64 nowTick)
    {
        if (mCount == 0) {
            return;
        }

        auto moveDown = [&]() { mIndex = (mIndex + 1 < mCount) ? mIndex + 1 : 0; };
        auto moveUp   = [&]() { mIndex = (mIndex > 0) ? mIndex - 1 : mCount - 1; };

        if (kDown & KEY_DOWN) {
            moveDown();
            mLastTick = nowTick;
        }
        else if (kDown & KEY_UP) {
            moveUp();
            mLastTick = nowTick;
        }
        else if ((kHeld & KEY_DOWN) && nowTick > mLastTick + REPEAT_DELAY_TICKS) {
            moveDown();
            mLastTick = nowTick;
        }
        else if ((kHeld & KEY_UP) && nowTick > mLastTick + REPEAT_DELAY_TICKS) {
            moveUp();
            mLastTick = nowTick;
        }

        clamp();
    }

private:
    // Keep the selection inside the viewport, without leaving a trailing gap when
    // the list could fill it.
    void clamp(void)
    {
        if (mCount == 0) {
            mIndex  = 0;
            mOffset = 0;
            return;
        }
        if (mIndex >= mCount) {
            mIndex = mCount - 1;
        }
        if (mIndex < mOffset) {
            mOffset = mIndex;
        }
        else if (mIndex >= mOffset + mVisibleRows) {
            mOffset = mIndex - mVisibleRows + 1;
        }
        const size_t maxOffset = mCount > mVisibleRows ? mCount - mVisibleRows : 0;
        if (mOffset > maxOffset) {
            mOffset = maxOffset;
        }
    }

    size_t mVisibleRows;
    size_t mCount  = 0;
    size_t mIndex  = 0;
    size_t mOffset = 0; // index of the first visible row
    u64 mLastTick  = 0;
};

#endif // LISTCURSOR_HPP
