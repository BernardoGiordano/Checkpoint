/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

size_t IHid::index(void)
{
    return mIndex;
}

void IHid::index(size_t v)
{
    mIndex = v;
}

size_t IHid::fullIndex(void)
{
    return mIndex + mPage * mMaxVisibleEntries;
}

int IHid::page(void)
{
    return mPage;
}

void IHid::page(int v)
{
    mPage = v;
}

void IHid::reset(void)
{
    mIndex = 0;
    mPage = 0;
}

size_t IHid::maxVisibleEntries(void)
{
    return mMaxVisibleEntries;
}

size_t IHid::maxEntries(size_t count)
{
    return (count - mPage*mMaxVisibleEntries) > mMaxVisibleEntries ? mMaxVisibleEntries - 1 : count - mPage*mMaxVisibleEntries - 1;
}

void IHid::page_back(void)
{
    if (mPage > 0) 
    {
        mPage--;
    }
    else if (mPage == 0)
    {
        mPage = mMaxPages - 1;
    }
}

void IHid::page_forward(void)
{
    if (mPage < (int)mMaxPages - 1)
    {
        mPage++;
    }
    else if (mPage == (int)mMaxPages - 1)
    {		
        mPage = 0;
    }
}

void IHid::update(size_t count) {
    mMaxPages = (count % mMaxVisibleEntries == 0) ? count / mMaxVisibleEntries : count / mMaxVisibleEntries + 1;

    u64 kHeld = held();
    u64 kDown = down();
    mSleep = false;
    
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
        if (kHeld & _KEY_LEFT())
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
            mSleep = true;
        }
        else if (kHeld & _KEY_RIGHT())
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
            mSleep = true;
        }
        else if (kHeld & _KEY_UP())
        {
            if (mIndex < mColumns)
            {
                // store the previous page
                int page = mPage;
                // update it
                page_back();
                // refresh the maximum amount of entries
                size_t maxentries = maxEntries(count);
                // if we went to the last page, make sure we go to the last valid row instead of the last absolute one
                mIndex = page == 0 ? (size_t)((floor(maxentries / mColumns) + 1)) * mColumns + mIndex - mColumns : mMaxVisibleEntries + mIndex - mColumns;
                // handle possible overflows
                if (mIndex > maxentries)
                {
                    mIndex = maxentries;
                }
            }
            else if (mIndex >= mColumns)
            {
                mIndex -= mColumns;
            }
            mSleep = true;
        }
        else if (kHeld & _KEY_DOWN())
        {
            size_t maxentries = maxEntries(count);
            if ((int)(maxentries - mColumns) >= 0)
            {
                if (mIndex <= maxentries - mColumns)
                {
                    mIndex += mColumns;
                }
                else if (mIndex >= maxentries - mColumns + 1)
                {
                    // store the old maxentries value to use later
                    maxentries = maxEntries(count);
                    // change page
                    page_forward();
                    // we need to setup the index to be on a new row: quantize the row and evaluate it
                    mIndex = (mIndex + mColumns) % (size_t)(floor(maxentries / mColumns) * mColumns + mColumns);
                    // in case rows are not full of icons, handle possible overflows
                    if (mIndex > maxEntries(count))
                    {
                        mIndex = maxEntries(count);
                    }
                }
                mSleep = true;
            }
        }
    }
    else
    {
        if (kHeld & _KEY_UP())
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
            mSleep = true;
        }
        else if (kHeld & _KEY_DOWN())
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
            mSleep = true;
        }
    }
}