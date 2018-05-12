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

#ifndef HBKBD_HPP
#define HBKBD_HPP

#include <locale>
#include <string>
#include <switch.h>
#include <vector>
#include "clickable.hpp"
#include "draw.hpp"
#include "util.hpp"

#define CUSTOM_PATH_LEN 49

extern u8* framebuf;
extern u32 framebuf_width;

class HbkbdButton : public Clickable 
{
public:
    HbkbdButton(u32 x, u32 y, u16 w, u16 h, color_t colorBg, color_t colorText, const std::string& message, bool centered)
    : Clickable(x, y, w, h, colorBg, colorText, message, centered)
    {
        mSelected = false;
    }

    void selected(bool v)
    {
        mSelected = v;
    }

    void draw(void)
    {
        Clickable::draw();
        // outline
        if (mSelected)
        {
            color_t color = MakeColor(138, 138, 138, 255);
            static const size_t size = 2;
            rectangled(mx - size, my - size, mw + 2*size, size, color); // top
            rectangled(mx - size, my, size, mh, color); // left
            rectangled(mx + mw, my, size, mh, color); // right
            rectangled(mx - size, my + mh, mw + 2*size, size, color); // bottom
        }
    }

protected:
    bool mSelected;
};

namespace hbkbd 
{
    void        init(void);
    void        exit(void);
    size_t      count(void);
    std::string keyboard(const std::string& suggestion);
}

#endif