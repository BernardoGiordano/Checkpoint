/*  This file is part of Checkpoint
>	Copyright (C) 2017/2018 Bernardo Giordano
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

#ifndef SCROLLABLE_H
#define SCROLLABLE_H

#include "common.h"

size_t getCellsCount(void);
std::string getPathFromCell(size_t index);

class Scrollable
{
public:
	Scrollable(int x, int y, u32 w, u32 h, size_t visibleEntries);
	void addCell(u32 color, u32 colorMessage, std::string message);
	void flush(void);
	size_t getCount(void);
	size_t getIndex(void);
	void invertCellColors(size_t index);
	void resetIndex(void);
	void updateSelection(void);
	void setIndex(size_t i);

	void draw(void);

private:
	int x;
	int y;
	u32 w;
	u32 h;
	size_t visibleEntries;
	size_t index;
	int page;
};

#endif