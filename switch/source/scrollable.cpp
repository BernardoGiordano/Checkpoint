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

#include "scrollable.hpp"

void Scrollable::setIndex(size_t i)
{
    IScrollable::index(i);
    mHid->index(mIndex);
    mHid->page(mPage);
}

void Scrollable::resetIndex(void)
{
    mHid->index(0);
    mHid->page(0);
}

void Scrollable::push_back(SDL_Color color, SDL_Color colorMessage, const std::string& message, bool selected)
{
    static const float spacing = mh / mVisibleEntries;
    Clickable* cell = new Clickable(mx, my + (size() % mVisibleEntries)*spacing, mw, spacing, color, colorMessage, message, false);
    cell->selected(selected);
    mCells.push_back(cell);
}

void Scrollable::updateSelection(void)
{
    touchPosition touch;
    hidTouchRead(&touch, 0);

    const int hu = (mHid->maxEntries(size()) + 1) * mh / mVisibleEntries;
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH && touch.py > (float)my && touch.py < (float)(my+hu) && touch.px > (float)mx && touch.px < (float)(mx+mw))
    {
        mHid->index(ceilf((touch.py - my)*mVisibleEntries/mh));
    }
    
    mHid->update(size());
    mIndex = mHid->index();
    mPage = mHid->page();
}

void Scrollable::draw(void)
{
    const size_t baseIndex = mVisibleEntries*mPage;
    const size_t sz = size() - baseIndex > mVisibleEntries ? mVisibleEntries : size() - baseIndex;
    for (size_t i = baseIndex; i < baseIndex + sz; i++)
    {
        mCells.at(i)->draw(20, g_backupScrollEnabled ? COLOR_BLUE : theme().c0);
    }

    size_t blankRows = mVisibleEntries - sz;
    size_t rowHeight = mh / mVisibleEntries;
    SDLH_DrawRect(mx, my + sz * rowHeight, mw, rowHeight * blankRows, theme().c2);
    
    // draw selector
    for (size_t i = baseIndex; i < baseIndex + sz; i++)
    {
        if (mCells.at(i)->selected())
        {
            mCells.at(i)->drawOutline(g_backupScrollEnabled ? COLOR_BLUE : theme().c5);
            break;
        }
    }
}