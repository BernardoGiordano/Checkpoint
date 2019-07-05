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

size_t IHid::index(void) const
{
    return mIndex;
}

void IHid::index(size_t v)
{
    mIndex = v;
}

size_t IHid::fullIndex(void) const
{
    return mIndex + mPage * mMaxVisibleEntries;
}

int IHid::page(void) const
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
    mPage  = 0;
}

size_t IHid::maxVisibleEntries(void) const
{
    return mMaxVisibleEntries;
}

size_t IHid::maxEntries(size_t count) const
{
    return (count - mPage * mMaxVisibleEntries) > mMaxVisibleEntries ? mMaxVisibleEntries - 1 : count - mPage * mMaxVisibleEntries - 1;
}

void IHid::page_back(void)
{
    if (mPage > 0) {
        mPage--;
    }
    else if (mPage == 0) {
        mPage = mMaxPages - 1;
    }
}

void IHid::page_forward(void)
{
    if (mPage < (int)mMaxPages - 1) {
        mPage++;
    }
    else if (mPage == (int)mMaxPages - 1) {
        mPage = 0;
    }
}

void IHid::pageBack(size_t count)
{
    page_back();
    if (mIndex > maxEntries(count)) {
        mIndex = maxEntries(count);
    }
}

void IHid::pageForward(size_t count)
{
    page_forward();
    if (mIndex > maxEntries(count)) {
        mIndex = maxEntries(count);
    }
}