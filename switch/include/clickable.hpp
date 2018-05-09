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

#ifndef CLICKABLE_HPP
#define CLICKABLE_HPP

#include <string>
#include <switch.h>
#include "draw.hpp"

class Clickable
{
public:
    Clickable(u32 x, u32 y, u16 w, u16 h, color_t colorBg, color_t colorText, const std::string& message, bool centered);
    ~Clickable(void) { };

    void        draw(void);
    void        draw(const ffnt_header_t* font, u32 messageHeight);
    bool        held(void);
    void        invertColors(void);
    bool        released(void);
    void        setColors(color_t bg, color_t text);
    std::string text(void);
    void        text(const std::string& v);

private:
    u32         mx;
    u32         my;
    u16         mw;
    u16         mh;
    color_t     mColorBg;
    color_t     mColorText;
    std::string mText;
    bool        mCentered;
    bool        mOldPressed;
};

#endif