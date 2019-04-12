/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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
    hidTouchRead(&touch);
    return ((hidKeysHeld() & KEY_TOUCH) && touch.px > mx && touch.px < mx+mw && touch.py > my && touch.py < my+mh);
}

bool Clickable::released(void)
{
    touchPosition touch;
    hidTouchRead(&touch);	
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

void Clickable::draw(float size, u32 overlay)
{
    static const float messageHeight = ceilf(size*fontGetInfo()->lineFeed);
    const float messageWidth = mCentered ? mC2dText.width * size : mw - 8;
    u32 bgColor = held() ? mColorText : mColorBg;
    u32 msgColor = bgColor == mColorBg ? mColorText : mColorBg;

    if (mCanInvertColor)
    {
        bool hld = held();
        bgColor = hld ? mColorText : mColorBg;
        msgColor = hld ? mColorBg : mColorText;
    }
    
    C2D_DrawRectSolid(mx, my, 0.5f, mw, mh, bgColor);
    if (mSelected)
    {

    }
    C2D_DrawText(&mC2dText, C2D_WithColor, mx + (mw - messageWidth)/2, my + (mh - messageHeight)/2, 0.5f, size, size, msgColor);	
}