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

#include "info.hpp"

Info::Info(void)
{
    mTextBuf = C2D_TextBufNew(128);
}

Info::~Info(void)
{
    C2D_TextBufDelete(mTextBuf);
}

void Info::init(const std::string& title, const std::string& message, int ttl, Info_t type)
{
    mw = 240;
    mh = 70;
    mx = (400 - mw) / 2;
    my = 240 - mh - 9;
    mTTL = ttl;
    mType = type;
    mRes = 0;

    C2D_TextBufClear(mTextBuf);
    C2D_TextParse(&mTitle, mTextBuf, title.c_str());
    C2D_TextParse(&mMessage, mTextBuf, message.c_str());
    C2D_TextOptimize(&mTitle);
    C2D_TextOptimize(&mMessage);
}

void Info::init(Result res, const std::string& message, int ttl, Info_t type)
{
    mw = 240;
    mh = 70;
    mx = (400 - mw) / 2;
    my = 240 - mh - 9;
    mTTL = ttl;
    mType = type;
    mRes = res;

    char buf[30];
    sprintf(buf, "Error: %08lX", mRes);
    C2D_TextBufClear(mTextBuf);
    C2D_TextParse(&mTitle, mTextBuf, buf);
    C2D_TextParse(&mMessage, mTextBuf, message.c_str());
    C2D_TextOptimize(&mTitle);
    C2D_TextOptimize(&mMessage);
}

void Info::draw(void)
{
    if ((mType == TYPE_INFO && mTTL > 0) || (mType == TYPE_ERROR && mTTL > 0 && mRes != 0))
    {
        float w, hres, hmessage;
        C2D_TextGetDimensions(&mMessage, 0.46f, 0.46f, NULL, &hmessage);
        u8 alpha = mTTL > 255 ? 255 : mTTL;
        u32 color = 0, bordercolor = 0, bgcolor = 0;
        
        if (mType == TYPE_ERROR)
        {
            color = C2D_Color32(255, 255, 255, alpha);
            bordercolor = C2D_Color32(70, 70, 70, alpha);
            bgcolor = C2D_Color32(79, 79, 79, alpha);
        }
        else if (mType == TYPE_INFO)
        {
            color = C2D_Color32(255, 255, 255, alpha);
            bordercolor = C2D_Color32(70, 70, 70, alpha);
            bgcolor = C2D_Color32(138, 138, 138, alpha);
        }

        C2D_TextGetDimensions(&mTitle, 0.6f, 0.6f, &w, &hres);
        const float spacing = (mh - hres - hmessage) / 3;
        C2D_DrawRectSolid(mx - 2, my - 2, 0.65f, mw + 4, mh + 4, bordercolor);
        C2D_DrawRectSolid(mx, my, 0.65f, mw, mh, bgcolor);
        C2D_DrawText(&mTitle, C2D_WithColor, mx + (mw - w) / 2, my + spacing, 0.65f, 0.6f, 0.6f, color);
        C2D_DrawText(&mMessage, C2D_WithColor, mx + 10, my + 2*spacing + hres, 0.65f, 0.46f, 0.46f, color);

        mTTL--;
    }
}