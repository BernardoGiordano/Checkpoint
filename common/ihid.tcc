/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2020 Bernardo Giordano, Admiral Fish, piepie62
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

#ifndef HID_HPP
#error "This file should not be directly included!"
#endif

template <HidDirection GrowDirection, HidDirection PageDirection>
void Hid<GrowDirection, PageDirection>::update(const InputDataHolder& input, size_t count)
{
    mMaxPages = ((count % mMaxVisibleEntries) == 0) ? count / mMaxVisibleEntries : count / mMaxVisibleEntries + 1;

    if (leftTriggerDownRepeat(input)) {
        pageBack();
    }
    else if (rightTriggerDownRepeat(input)) {
        pageForward();
    }

    const auto decrementInGrowDir = [&]() {
        if constexpr (PageDirection == GrowDirection) {
            if (mIndexInVisible % mVisiblePerChunk == 0) {
                pageBack();
            }
        }

        if (mIndexInVisible % mVisiblePerChunk == 0) {
            mIndexInVisible += mVisiblePerChunk;
        }

        mIndexInVisible -= 1;
    };
    const auto incrementInGrowDir = [&]() {
        mIndexInVisible += 1;

        if constexpr (PageDirection == GrowDirection) {
            if (mIndexInVisible % mVisiblePerChunk == 0) {
                pageForward();
            }
        }

        if (mIndexInVisible % mVisiblePerChunk == 0) {
            mIndexInVisible -= mVisiblePerChunk;
        }
    };

    const auto decrementInOtherDir = [&]() {
        if (mIndexInVisible < mVisiblePerChunk) {
            if constexpr (PageDirection != GrowDirection) {
                pageBack();
            }
            mIndexInVisible += mMaxVisibleEntries; // past the end
            // go back in bounds
        }

        mIndexInVisible -= mVisiblePerChunk;
    };
    const auto incrementInOtherDir = [&]() {
        mIndexInVisible += mVisiblePerChunk;
        if (mIndexInVisible >= mMaxVisibleEntries) {
            if constexpr (PageDirection != GrowDirection) {
                pageForward();
            }
            mIndexInVisible -= mMaxVisibleEntries;
        }
    };

    if constexpr (GrowDirection == HidDirection::HORIZONTAL) {// grow right
        if (upDownRepeat(input)) decrementInOtherDir();
        else if (downDownRepeat(input)) incrementInOtherDir();

        if (leftDownRepeat(input)) decrementInGrowDir();
        else if (rightDownRepeat(input)) incrementInGrowDir();

        /*
        if (upDownRepeat(input)) {
            if (mIndexInVisible < mVisiblePerChunk) {
                if constexpr (PageDirection == HidDirection::VERTICAL) {
                    pageBack();
                }
                mIndexInVisible += mMaxVisibleEntries; // past the end
                // go back in bounds
            }

            mIndexInVisible -= mVisiblePerChunk;
        }
        else if (downDownRepeat(input)) {
            mIndexInVisible += mVisiblePerChunk;
            if (mIndexInVisible >= mMaxVisibleEntries) {
                if constexpr (PageDirection == HidDirection::VERTICAL) {
                    pageForward();
                }
                mIndexInVisible -= mMaxVisibleEntries;
            }
        }

        if (leftDownRepeat(input)) {
            if constexpr (PageDirection == HidDirection::HORIZONTAL) {
                if (mIndexInVisible % mVisiblePerChunk == 0) {
                    pageBack();
                }
            }

            if (mIndexInVisible % mVisiblePerChunk == 0) {
                mIndexInVisible += mVisiblePerChunk;
            }

            mIndexInVisible -= 1;
        }
        else if (rightDownRepeat(input)) {
            mIndexInVisible += 1;

            if constexpr (PageDirection == HidDirection::HORIZONTAL) {
                if (mIndexInVisible % mVisiblePerChunk == 0) {
                    pageForward();
                }
            }

            if (mIndexInVisible % mVisiblePerChunk == 0) {
                mIndexInVisible -= mVisiblePerChunk;
            }
        }
        */
    }
    else { // grow down
        if (upDownRepeat(input)) decrementInGrowDir();
        else if (downDownRepeat(input)) incrementInGrowDir();

        if (leftDownRepeat(input)) decrementInOtherDir();
        else if (rightDownRepeat(input)) incrementInOtherDir();

        /*
        if (upDownRepeat(input)) {
            if constexpr (PageDirection == HidDirection::VERTICAL) {
                if (mIndexInVisible % mVisiblePerChunk == 0) {
                    pageBack();
                }
            }

            if (mIndexInVisible % mVisiblePerChunk == 0) {
                mIndexInVisible += mVisiblePerChunk;
            }

            mIndexInVisible -= 1;
        }
        else if (downDownRepeat(input)) {
            mIndexInVisible += 1;

            if constexpr (PageDirection == HidDirection::VERTICAL) {
                if (mIndexInVisible % mVisiblePerChunk == 0) {
                    pageForward();
                }
            }

            if (mIndexInVisible % mVisiblePerChunk == 0) {
                mIndexInVisible -= mVisiblePerChunk;
            }
        }
        
        if (leftDownRepeat(input)) {
            if (mIndexInVisible < mVisiblePerChunk) {
                if constexpr (PageDirection == HidDirection::HORIZONTAL) {
                    pageBack();
                }
                mIndexInVisible += mMaxVisibleEntries; // past the end
                // go back in bounds
            }

            mIndexInVisible -= mVisiblePerChunk;
        }
        else if (rightDownRepeat(input)) {
            mIndexInVisible += mVisiblePerChunk;
            if (mIndexInVisible >= mMaxVisibleEntries) {
                if constexpr (PageDirection == HidDirection::HORIZONTAL) {
                    pageForward();
                }
                mIndexInVisible -= mMaxVisibleEntries;
            }
        }
        */
    }

    correctIndex(count);
}
