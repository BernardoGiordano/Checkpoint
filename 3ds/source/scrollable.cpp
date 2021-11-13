/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2021 Bernardo Giordano, FlagBrew
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

void Scrollable::c2dText(size_t i, const std::string& v)
{
    ((Clickable*)mCells.at(i))->c2dText(v);
}

void Scrollable::setIndex(size_t i)
{
    IScrollable::index(i);
    mHid.index(mIndex);
    mHid.page(mPage);
}

void Scrollable::resetIndex(void)
{
    setIndex(0);
}

void Scrollable::push_back(u32 color, u32 colorMessage, const std::string& message, bool selected)
{
    const float spacing = mh / mVisibleEntries;
    Clickable* cell     = new Clickable(mx, my + (size() % mVisibleEntries) * spacing, mw, spacing, color, colorMessage, message, false);
    cell->selected(selected);
    mCells.push_back(cell);
}

void Scrollable::updateSelection(void)
{
    touchPosition touch;
    hidTouchRead(&touch);

    const u32 hu = (mHid.maxEntries(size()) + 1) * mh / mVisibleEntries;

    if (hidKeysHeld() & KEY_TOUCH && touch.py > (float)my && touch.py < (float)(my + hu) && touch.px > (float)mx && touch.px < (float)(mx + mw)) {
        mHid.index((size_t)((touch.py - my) * mVisibleEntries / mh));
    }

    mHid.update(size());
    mIndex = mHid.index();
    mPage  = mHid.page();
}

void Scrollable::draw(bool condition)
{
    const size_t baseIndex = mVisibleEntries * mPage;
    const size_t sz        = size() - baseIndex > mVisibleEntries ? mVisibleEntries : size() - baseIndex;
    for (size_t i = baseIndex; i < baseIndex + sz; i++) {
        mCells.at(i)->draw(0.5f, 0);
    }

    size_t blankRows = mVisibleEntries - sz;
    size_t rowHeight = mh / mVisibleEntries;
    C2D_DrawRectSolid(mx, my + sz * rowHeight, 0.5f, mw, rowHeight * blankRows, COLOR_GREY_DARKER);

    // draw selector
    for (size_t i = baseIndex; i < baseIndex + sz; i++) {
        if (mCells.at(i)->selected()) {
            mCells.at(i)->drawOutline(condition ? COLOR_BLUE : COLOR_GREY_LIGHT);
            break;
        }
    }
}
