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

#include "scrollable.hpp"

std::string Scrollable::cellName(size_t index)
{
    return mCells.at(index)->text();
}

Scrollable::Scrollable(u32 _x, u32 _y, u32 _w, u32 _h, size_t _visibleEntries)
{
    mx = _x;
    my = _y;
    mw = _w;
    mh = _h;
    mVisibleEntries = _visibleEntries;
    mIndex = 0;
    mPage = 0;
}

Scrollable::~Scrollable(void)
{
    flush();
}

void Scrollable::push_back(color_t color, color_t colorMessage, const std::string& message)
{
    static const float spacing = mh / mVisibleEntries;
    Clickable* cell = new Clickable(mx, my + (size() % mVisibleEntries)*spacing, mw, spacing, color, colorMessage, message, false);
    mCells.push_back(cell);
}

void Scrollable::flush(void)
{
    for (size_t i = 0, sz = size(); i < sz; i++)
    {
        delete mCells[i];
    }
    mCells.clear();
}

size_t Scrollable::size(void)
{
    return mCells.size();
}

int Scrollable::page(void)
{
    return mPage;
}

size_t Scrollable::maxVisibleEntries(void)
{
    return (size() - mPage*mVisibleEntries) > mVisibleEntries ? mVisibleEntries : size() - mPage*mVisibleEntries;
}

size_t Scrollable::index(void)
{
    return mIndex + mPage*mVisibleEntries;
}

void Scrollable::invertCellColors(size_t i)
{
    if (i < size())
    {
        mCells.at(i)->invertColors();
    }
}

void Scrollable::resetIndex(void)
{
    mIndex = 0;
    mPage = 0;
}

void Scrollable::index(size_t i)
{
    mPage = i / mVisibleEntries;
    mIndex = i - mPage * mVisibleEntries;
}

void Scrollable::updateSelection(void)
{
    touchPosition touch;
    hidTouchRead(&touch, 0);

    const size_t maxentries = maxVisibleEntries();
    const size_t maxpages = (size() % mVisibleEntries == 0) ? size() / mVisibleEntries : size() / mVisibleEntries + 1;
    const u32 hu = maxentries * mh / mVisibleEntries;
    
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH && touch.py > my && touch.py < my+hu && touch.px > mx && touch.px < mx+mw)
    {
        mIndex = (size_t)((touch.py - my)*mVisibleEntries/mh);
    }
    
    hid::index(mIndex, mPage, maxpages, maxentries, mVisibleEntries, 1);
}

void Scrollable::draw(void)
{
    const size_t baseIndex = mVisibleEntries*mPage;
    const size_t sz = size() - baseIndex > mVisibleEntries ? mVisibleEntries : size() - baseIndex;
    for (size_t i = baseIndex; i < baseIndex + sz; i++)
    {
        mCells.at(i)->draw(font14);
    }
}