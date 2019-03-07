/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#include "ihid.hpp"

void IHidHorizontal::update(size_t count)
{
    mCurrentTime = tick();
    size_t rows = mMaxVisibleEntries / mColumns;
    mMaxPages = (count % mMaxVisibleEntries == 0) ? count / mMaxVisibleEntries : count / mMaxVisibleEntries + 1;
    u64 kHeld = held();
    u64 kDown = down();
    
    if (kDown & _KEY_ZL())
    {
        page_back();
        if (mIndex > maxEntries(count))
        {
            mIndex = maxEntries(count);
        }
    }
    else if (kDown & _KEY_ZR())
    {
        page_forward();
        if (mIndex > maxEntries(count))
        {
            mIndex = maxEntries(count);
        }
    }
    else if (mColumns > 1)
    {
        if (kDown & _KEY_LEFT())
        {
            if (mIndex % mColumns != 0) 
            {
                mIndex--;
            }
            else
            {
                page_back();
                mIndex += mColumns - 1;
            }
        }
        else if (kDown & _KEY_RIGHT())
        {
            if (mIndex % mColumns != mColumns - 1)
            {
                if (mIndex + mPage * mMaxVisibleEntries == count - 1)
                {
                    page_forward();
                    mIndex = (mIndex / mColumns) * mColumns;
                }
                else
                {
                    mIndex++;
                }
            }
            else
            {
                page_forward();
                mIndex -= mColumns - 1;
            }
        }
        else if (kDown & _KEY_UP())
        {
            if (mIndex < mColumns)
            {
                mIndex += mColumns * (rows - 1);
            }
            else
            {
                mIndex -= mColumns;
            }
        }
        else if (kDown & _KEY_DOWN())
        {
            if (mIndex >= mColumns * (rows - 1))
            {
                mIndex -= mColumns * (rows - 1);
            }
            else
            {
                mIndex += mColumns;
            }
        }
        else if (kHeld & _KEY_LEFT())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex % mColumns != 0) 
            {
                mIndex--;
            }
            else
            {
                page_back();
                mIndex += mColumns - 1;
            }
        }
        else if (kHeld & _KEY_RIGHT())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex % mColumns != mColumns - 1)
            {
                if (mIndex + mPage * mMaxVisibleEntries == count - 1)
                {
                    page_forward();
                    mIndex = (mIndex / mColumns) * mColumns;
                }
                else
                {
                    mIndex++;
                }
            }
            else
            {
                page_forward();
                mIndex -= mColumns - 1;
            }
        }
        else if (kHeld & _KEY_UP())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex < mColumns)
            {
                mIndex += mColumns * (rows - 1);
            }
            else
            {
                mIndex -= mColumns;
            }
        }
        else if (kHeld & _KEY_DOWN())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex >= mColumns * (rows - 1))
            {
                mIndex -= mColumns * (rows - 1);
            }
            else
            {
                mIndex += mColumns;
            }
        }
    }
    else
    {
        if (kDown & _KEY_UP())
        {
            if (mIndex > 0) 
            {
                mIndex--;
            }
            else if (mIndex == 0)
            {
                page_back();
                mIndex = maxEntries(count);
            }
        }
        else if (kDown & _KEY_DOWN())
        {
            if (mIndex < maxEntries(count))
            {
                mIndex++;
            }
            else if (mIndex == maxEntries(count))
            {
                page_forward();
                mIndex = 0;
            }
        }
        else if (kHeld & _KEY_UP())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex > 0) 
            {
                mIndex--;
            }
            else if (mIndex == 0)
            {
                page_back();
                mIndex = maxEntries(count);
            }
        }
        else if (kHeld & _KEY_DOWN())
        {
            if (mCurrentTime <= mLastTime + mDelayTicks)
            {
                return;
            }
            if (mIndex < maxEntries(count))
            {
                mIndex++;
            }
            else if (mIndex == maxEntries(count))
            {
                page_forward();
                mIndex = 0;
            }
        }
    }

    if (mIndex > maxEntries(count))
    {
        mIndex = maxEntries(count);
    }
    mLastTime = mCurrentTime;
}