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

#include "messagebox.hpp"

MessageBox::MessageBox(u32 colorBg, u32 colorText, gfxScreen_t screen)
{
    mColorBg = colorBg;
    mColorText = colorText;
    mScreen = screen;
    mList.clear();
    mTextBuf = C2D_TextBufNew(256);
}

MessageBox::~MessageBox()
{
    C2D_TextBufDelete(mTextBuf);
}

void MessageBox::push_message(const std::string& newmessage)
{
    C2D_Text text;
    C2D_TextParse(&text, mTextBuf, newmessage.c_str());
    C2D_TextOptimize(&text);
    mList.push_back(text);
}

void MessageBox::clear(void)
{
    mList.clear();
    C2D_TextBufClear(mTextBuf);
}

void MessageBox::draw(void)
{
    static const float size = 0.6f;
    static const float spacingFromEdges = 10;
    static const u32 mScreenw = mScreen == GFX_TOP ? 400 : 320;
    static const float mh = ceilf(size*fontGetInfo()->lineFeed);
    const float h = mh*mList.size() + 2*spacingFromEdges;
    
    float w = 0;
    for (size_t i = 0, sz = mList.size(); i < sz; i++)
    {
        float nw = ceilf(mList.at(i).width*size);
        if (nw > w)
        {
            w = nw;
        }
    }
    w += 2*spacingFromEdges; 
    
    const int x = (mScreenw-w)/2;
    const int y = (240-h)/2;

    C2D_DrawRectSolid(x - 2, y - 2, 0.7f, w + 4, h + 4, COLOR_BLACK);
    C2D_DrawRectSolid(x, y, 0.7f, w, h, mColorBg);
    
    for (size_t i = 0, sz = mList.size(); i < sz; i++)
    {
        C2D_DrawText(&mList.at(i), C2D_WithColor, (mScreenw - ceilf(mList.at(i).width * size)) / 2, y + spacingFromEdges + mh*i, 0.7f, size, size, mColorText);
    }
}