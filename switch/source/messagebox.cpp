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

#include "messagebox.hpp"

MessageBox::MessageBox(SDL_Color colorBg, SDL_Color colorText)
{
    mColorBg = colorBg;
    mColorText = colorText;
    mList.clear();
}

void MessageBox::push_message(const std::string& newmessage)
{
    mList.push_back(newmessage);
}

void MessageBox::clear(void)
{
    mList.clear();
}

void MessageBox::draw(void)
{
    static const u32 spacingFromEdges = 20;
    static const u32 offset = 10;

    u32 w = 0, mh;
    u32 widths[mList.size()];
    for (size_t i = 0, sz = mList.size(); i < sz; i++)
    {
        GetTextDimensions(6, mList.at(i).c_str(), &widths[i], &mh);
        if (widths[i] > w)
        {
            w = widths[i];
        }
    }
    mh += offset;
    w += 2*spacingFromEdges; 

    const u32 h = mh*mList.size() + 2*spacingFromEdges - offset;
    const u32 x = (1280-w)/2;
    const u32 y = (720-h)/2;

    SDL_DrawRect(x - 2, y - 2, w + 4, h + 4, COLOR_BLACK);
    SDL_DrawRect(x, y, w, h, mColorBg);
    
    for (size_t i = 0, sz = mList.size(); i < sz; i++)
    {
        SDL_DrawText(6, ceil((1280 - widths[i]) / 2), y + spacingFromEdges + mh*i, mColorText, mList.at(i).c_str());
    }
}