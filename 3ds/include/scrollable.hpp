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

#ifndef SCROLLABLE_HPP
#define SCROLLABLE_HPP

#include <citro2d.h>
#include <vector>
#include "clickable.hpp"
#include "colors.hpp"
#include "hid.hpp"

class Scrollable
{
public:
    Scrollable(int x, int y, u32 w, u32 h, size_t visibleEntries);
    ~Scrollable(void);

    std::string cellName(size_t i);
    void        draw(void);
    void        flush(void);
    size_t      index(void);
    void        index(size_t i);
    void        invertCellColors(size_t index);
    void        push_back(u32 color, u32 colorMessage, const std::string& message);
    void        resetIndex(void);
    size_t      size(void);
    void        updateSelection(void);

private:
    int          mx;
    int          my;
    u32          mw;
    u32          mh;
    size_t       mVisibleEntries;
    size_t       mIndex;
    int          mPage;
    std::vector
    <Clickable*> mCells;
};

#endif