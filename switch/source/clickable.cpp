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

#include "clickable.hpp"

Clickable::Clickable(u32 x, u32 y, u16 w, u16 h, color_t colorBg, color_t colorText, const std::string& text, bool centered)
{
    mx = x;
    my = y;
    mw = w;
    mh = h;
    mColorBg = colorBg;
    mColorText = colorText;
    mText = text;
    mCentered = centered;
    mOldPressed = false;
}

std::string Clickable::text(void)
{
    return mText;
}

void Clickable::text(const std::string& v)
{
    mText = v;
}

bool Clickable::held()
{
    touchPosition touch;
    hidTouchRead(&touch, 0);
    return ((hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH) && touch.px > mx && touch.px < mx+mw && touch.py > my && touch.py < my+mh);
}

bool Clickable::released(void)
{
    touchPosition touch;
    hidTouchRead(&touch, 0);	
    const bool on = touch.px > mx && touch.px < mx+mw && touch.py > my && touch.py < my+mh;
    
    if (on)
    {
        mOldPressed = true;
    }
    else
    {
        if (mOldPressed && !(touch.px > 0 || touch.py > 0))
        {
            mOldPressed = false;			
            return true;
        }
        mOldPressed = false;
    }
    
    return false;
}

void Clickable::invertColors(void)
{
    color_t tmp = mColorBg;
    mColorBg = mColorText;
    mColorText = tmp;
}

void Clickable::setColors(color_t bg, color_t text)
{
    mColorBg = bg;
    mColorText = text;
}

void Clickable::draw(void)
{
    static const u32 messageHeight = 44;
    u32 textw;
    GetTextDimensions(font24, mText.c_str(), &textw, NULL);
    const u32 messageWidth = mCentered ? textw : mw - 8;
    bool hld = held();
    const color_t bgColor = hld ? mColorText : mColorBg;
    const color_t msgColor = hld ? mColorBg : mColorText;
    
    rectangle(mx, my, mw, mh, bgColor);
    DrawText(font24, mx + (mw - messageWidth)/2, my + (mh - messageHeight)/2, msgColor, mText.c_str());	
}

void Clickable::draw(const ffnt_header_t* font, u32 messageHeight)
{
    u32 textw;
    GetTextDimensions(font, mText.c_str(), &textw, NULL);
    const u32 messageWidth = mCentered ? textw : mw - 8;
    
    rectangle(mx, my, mw, mh, mColorBg);
    DrawTextTruncate(font, mx + (mw - messageWidth)/2, my + (mh - messageHeight)/2, mColorText, mText.c_str(), mw - 4*2 - 40);
}