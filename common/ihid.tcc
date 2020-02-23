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

#ifndef IHID_HPP
#error "This file should not be directly included!"
#endif

template <HidDirection ListDirection, HidDirection PageDirection, u64 Delay>
void IHid<ListDirection, PageDirection, Delay>::update(size_t count)
{
    u64 currentTime = tick();

    mMaxPages = (count % mMaxVisibleEntries == 0) ? count / mMaxVisibleEntries : count / mMaxVisibleEntries + 1;

    if (leftTriggerDown())
    {
        pageBack();
    }
    else if (rightTriggerDown())
    {
        pageForward();
    }
    else if (leftTriggerHeld())
    {
        if (currentTime <= mLastTime + Delay)
        {
            return;
        }

        pageBack();
    }
    else if (rightTriggerHeld())
    {
        if (currentTime <= mLastTime + Delay)
        {
            return;
        }

        pageForward();
    }

    if constexpr (ListDirection == HidDirection::HORIZONTAL)
    {
        if (upDown())
        {
            if (mIndex < mColumns)
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageBack();
                }
                mIndex += mColumns * (mRows - 1);
            }
            else
            {
                mIndex -= mColumns;
            }
        }
        else if (downDown())
        {
            mIndex += mColumns;
            if (mIndex > maxEntries(count))
            {
                mIndex %= mColumns;
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageForward();
                }
            }
        }
        else if (upHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if (mIndex < mColumns)
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageBack();
                }
                mIndex += mColumns * (mRows - 1);
            }
            else
            {
                mIndex -= mColumns;
            }
        }
        else if (downHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            mIndex += mColumns;
            if (mIndex > maxEntries(count))
            {
                mIndex %= mColumns;
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageForward();
                }
            }
        }

        if (leftDown())
        {
            if (mIndex % mColumns != 0)
            {
                mIndex--;
            }
            else
            {
                mIndex += mColumns - 1;
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageBack();
                }
            }
        }
        else if (rightDown())
        {
            if (mIndex % mColumns != mColumns - 1)
            {
                mIndex++;
                if (mIndex > maxEntries(count))
                {
                    if constexpr (PageDirection == HidDirection::HORIZONTAL)
                    {
                        pageForward();
                    }
                    mIndex = mIndex - (mIndex % mColumns);
                }
            }
            else
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageForward();
                }
                mIndex -= mColumns - 1;
            }
        }
        else if (leftHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if (mIndex % mColumns != 0)
            {
                mIndex--;
            }
            else
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageBack();
                }
                mIndex += mColumns - 1;
            }
        }
        else if (rightHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if (mIndex % mColumns != mColumns - 1)
            {
                mIndex++;
                if (mIndex > maxEntries(count))
                {
                    if constexpr (PageDirection == HidDirection::HORIZONTAL)
                    {
                        pageForward();
                    }
                    mIndex = mIndex - (mIndex % mColumns);
                }
            }
            else
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageForward();
                }
                mIndex -= mColumns - 1;
            }
        }
    }
    else
    {
        if (leftDown())
        {
            if (mIndex / mRows != 0)
            {
                mIndex -= mRows;
            }
            else
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageBack();
                }
                mIndex += mRows * (mColumns - 1);
            }
        }
        else if (rightDown())
        {
            mIndex += mRows;
            if (mIndex > maxEntries(count))
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageForward();
                }
                mIndex %= mRows;
            }
        }
        else if (leftHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if (mIndex / mRows != 0)
            {
                mIndex -= mRows;
            }
            else
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageBack();
                }
                mIndex += mRows * (mColumns - 1);
            }
        }
        else if (rightHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            mIndex += mRows;
            if (mIndex > maxEntries(count))
            {
                if constexpr (PageDirection == HidDirection::HORIZONTAL)
                {
                    pageForward();
                }
                mIndex %= mRows;
            }
        }

        if (upDown())
        {
            if (mIndex % mRows > 0)
            {
                mIndex -= 1;
            }
            else
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageBack();
                }
                mIndex = mIndex + mRows - 1;
            }
        }
        else if (downDown())
        {
            if ((mIndex % mRows) < mRows - 1)
            {
                if (mIndex + mPage * mMaxVisibleEntries == count - 1)
                {
                    if constexpr (PageDirection == HidDirection::VERTICAL)
                    {
                        pageForward();
                    }
                    mIndex = mIndex - (mIndex % mRows);
                }
                else
                {
                    mIndex += 1;
                }
            }
            else
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageForward();
                }
                mIndex = mIndex + 1 - mRows;
            }
        }
        else if (upHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if (mIndex % mRows > 0)
            {
                mIndex -= 1;
            }
            else
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageBack();
                }
                mIndex = mIndex + mRows - 1;
            }
        }
        else if (downHeld())
        {
            if (currentTime <= mLastTime + Delay)
            {
                return;
            }
            if ((mIndex % mRows) < mRows - 1)
            {
                if (mIndex + mPage * mMaxVisibleEntries == count - 1)
                {
                    if constexpr (PageDirection == HidDirection::VERTICAL)
                    {
                        pageForward();
                    }
                    mIndex = mIndex - (mIndex % mRows);
                }
                else
                {
                    mIndex += 1;
                }
            }
            else
            {
                if constexpr (PageDirection == HidDirection::VERTICAL)
                {
                    pageForward();
                }
                mIndex = mIndex + 1 - mRows;
            }
        }
    }

    correctIndex(count);

    mLastTime = currentTime;
}
