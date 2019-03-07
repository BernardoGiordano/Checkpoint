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

#include "info.hpp"

Info::Info(void)
{
    mw = 480;
    mh = 140;
    mx = (1280 - mw) / 2;
    my = 720 - mh - 48;
}

void Info::init(std::string title, std::string message, int ttl, Info_t type)
{
    mTTL = ttl;
    mType = type;
    mRes = 0;
    mTitle = title;
    mMessage = message;
}

void Info::init(Result res, std::string message, int ttl, Info_t type)
{
    mTTL = ttl;
    mType = type;
    mRes = res;
    mMessage = message;
    mTitle = StringUtils::format("Error: %08lX", mRes);
}

void Info::draw(void)
{
    if ((mType == TYPE_INFO && mTTL > 0) || (mType == TYPE_ERROR && mTTL > 0 && mRes != 0))
    {
        u32 w, hres = 44, hmessage;
        u8 alpha = mTTL > 255 ? 255 : mTTL;
        SDL_Color color, bordercolor, bgcolor;
        
        if (mType == TYPE_ERROR)
        {
            color = FC_MakeColor(255, 255, 255, alpha);
            bordercolor = FC_MakeColor(70, 70, 70, alpha);
            bgcolor = FC_MakeColor(79, 79, 79, alpha);
        }
        else if (mType == TYPE_INFO)
        {
            color = FC_MakeColor(255, 255, 255, alpha);
            bordercolor = FC_MakeColor(70, 70, 70, alpha);
            bgcolor = FC_MakeColor(138, 138, 138, alpha);
        }

        SDLH_GetTextDimensions(36, mTitle.c_str(), &w, &hmessage);
        const u32 spacing = (mh - hres - hmessage) / 3;
        SDLH_DrawRect(mx - 2, my - 2, mw + 4, mh + 4, bordercolor);
        SDLH_DrawRect(mx, my, mw, mh, bgcolor);
        SDLH_DrawText(36, mx + (mw - w) / 2, my + spacing, color, mTitle.c_str());
        SDLH_DrawText(20, mx + 10, my + 2*spacing + hres, color, mMessage.c_str());

        mTTL--;
    }
}