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

#ifndef HID_HPP
#define HID_HPP

#include <3ds.h>
#include <cmath>
#include "common.hpp"

class Hid 
{
public:
    Hid(size_t entries, size_t columns)
    : mMaxVisibleEntries(entries), mColumns(columns)
    {
        reset();
        mMaxPages = 0;
    }

    ~Hid(void) { }

    size_t fullIndex(void);
    size_t index(void);
    void index(size_t v);
    size_t maxEntries(size_t max);
    size_t maxVisibleEntries(void);
    int page(void);
    void page(int v);
    void reset(void);
    void update(size_t count);

private:
    void page_back(void);
    void page_forward(void);

    size_t mIndex;
    int mPage;
    size_t mMaxPages;
    size_t mMaxVisibleEntries;
    size_t mColumns;
};

#endif
