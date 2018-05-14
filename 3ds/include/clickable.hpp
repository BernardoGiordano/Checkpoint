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

#include <citro2d.h>
#include <string>
#include "colors.hpp"

class Clickable
{
public:
    Clickable(int x, int y, u16 w, u16 h, u32 colorBg, u32 colorText, std::string message, bool centered);
    ~Clickable(void);

    void        draw(void);
    void        draw(float size);
    bool        held(void);
    void        invertColors(void);
    bool        released(void);
    void        setColors(u32 bg, u32 text);
    std::string text();

private:
    int         mx;
    int         my;
    u16         mw;
    u16         mh;
    u32         mColorBg;
    u32         mColorText;
    std::string mText;
    bool        mCentered;
    bool        mOldPressed;
    C2D_Text    mC2dText;
    C2D_TextBuf mTextBuf;
};

#endif