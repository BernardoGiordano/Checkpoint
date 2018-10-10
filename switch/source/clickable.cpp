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

bool Clickable::held()
{
    touchPosition touch;
    hidTouchRead(&touch, 0);
    return ((hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH) && (int)touch.px > mx && (int)touch.px < mx+mw && (int)touch.py > my && (int)touch.py < my+mh);
}

bool Clickable::released(void)
{
    touchPosition touch;
    hidTouchRead(&touch, 0);	
    const bool on = (int)touch.px > mx && (int)touch.px < mx+mw && (int)touch.py > my && (int)touch.py < my+mh;
    
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

void Clickable::draw(void)
{
    u32 textw, texth;
    SDLH_GetTextDimensions(36, mText.c_str(), &textw, &texth);
    const u32 messageWidth = mCentered ? textw : mw - 8;
    bool hld = held();
    const SDL_Color bgColor = hld ? mColorText : mColorBg;
    const SDL_Color msgColor = hld ? mColorBg : mColorText;
    
    SDLH_DrawRect(mx, my, mw, mh, bgColor);
    SDLH_DrawText(36, mx + (mw - messageWidth)/2, my + (mh - texth)/2, msgColor, mText.c_str());	
}

void Clickable::draw(float font)
{
    u32 textw, texth;
    SDLH_GetTextDimensions(font, mText.c_str(), &textw, &texth);
    const u32 messageWidth = mCentered ? textw : mw - 8;
    
    SDLH_DrawRect(mx, my, mw, mh, mColorBg);
    SDLH_DrawTextBox(font, mx + (mw - messageWidth)/2, my + (mh - texth)/2, mColorText, mw - 4*2, mText.c_str());
}