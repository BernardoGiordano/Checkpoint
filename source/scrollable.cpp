/*  This file is part of Checkpoint
>	Copyright (C) 2017 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "scrollable.h"

static std::vector<Clickable> cells;

std::string getPathFromCell(size_t index)
{
	return cells.at(index).getMessage();
}

Scrollable::Scrollable(int _x, int _y, u32 _w, u32 _h, size_t _visibleEntries)
{
	x = _x;
	y = _y;
	w = _w;
	h = _h;
	visibleEntries = _visibleEntries;
	index = 0;
	page = 0;
}

void Scrollable::addCell(u32 color, u32 colorMessage, std::string message)
{
	static const float spacing = h / visibleEntries;
	Clickable cell(x, y + (getCount() % visibleEntries)*spacing, w, spacing, color, colorMessage, message, false);
	cells.push_back(cell);
}

void Scrollable::flush(void)
{
	cells.clear();
}

size_t Scrollable::getCount(void)
{
	return cells.size();
}

size_t Scrollable::getIndex(void)
{
	return index + page*visibleEntries;
}

void Scrollable::invertCellColors(size_t i)
{
	if (i < cells.size())
	{
		cells.at(i).invertColors();
	}
}

void Scrollable::resetIndex(void)
{
	index = 0;
	page = 0;
}

void Scrollable::updateSelection(void)
{
	touchPosition touch;
	hidTouchRead(&touch);

	const size_t maxentries = (cells.size() - page*visibleEntries) > visibleEntries ? visibleEntries : cells.size() - page*visibleEntries;
	const size_t maxpages = (cells.size() % visibleEntries == 0) ? cells.size() / visibleEntries : cells.size() / visibleEntries + 1;
	const u32 hu = maxentries * h / visibleEntries;
	
	if (hidKeysHeld() & KEY_TOUCH && touch.py > y && touch.py < y+hu && touch.px > x && touch.px < x+w)
	{
		index = (size_t)((touch.py - y)*visibleEntries/h);
	}

	calculateIndex(index, page, maxpages, maxentries, visibleEntries, 1);
}

void Scrollable::draw(void)
{
	const size_t baseIndex = visibleEntries*page;
	const size_t sz = cells.size() - baseIndex > visibleEntries ? visibleEntries : cells.size() - baseIndex;
	for (size_t i = baseIndex; i < baseIndex + sz; i++)
	{
		cells.at(i).drawStatic();
	}
}

size_t getCellsCount(void)
{
	return cells.size();
}