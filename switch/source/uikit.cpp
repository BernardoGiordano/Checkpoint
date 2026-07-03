/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

#include "uikit.hpp"
#include "colors.hpp"
#include "shapes.hpp"
#include <algorithm>

namespace {
    int hintItemWidth(const UiKit::HintItem& item)
    {
        u32 tw;
        SDLH_GetTextDimensions(13, item.label.c_str(), &tw, NULL);
        return 20 + 7 + (int)tw;
    }
}

void UiKit::drawHintCircle(int x, int y, const std::string& glyph)
{
    const int size = 20;
    Shapes::fillRound(x, y, size, size, size / 2, COLOR_FILL3);

    SDL_Color glyphColor = FC_MakeColor(232, 232, 240, 255);
    u32 tw, th;
    SDLH_GetTextDimensions(11, glyph.c_str(), &tw, &th);
    SDLH_DrawText(11, x + (size - (int)tw) / 2, y + (size - (int)th) / 2, glyphColor, glyph.c_str());
}

void UiKit::drawHintBar(const std::vector<HintItem>& items)
{
    SDLH_DrawRect(0, HINTBAR_Y, SCREEN_W, 1, COLOR_STROKE1);

    int totalW = 0;
    for (size_t i = 0; i < items.size(); i++) {
        totalW += hintItemWidth(items[i]);
        if (i + 1 < items.size())
            totalW += 24;
    }

    int x = SCREEN_W - 26 - totalW;
    int y = HINTBAR_Y + (HINTBAR_H - 20) / 2;
    for (size_t i = 0; i < items.size(); i++) {
        drawHintCircle(x, y, items[i].glyph);
        x += 20 + 7;

        u32 tw, th;
        SDLH_GetTextDimensions(13, items[i].label.c_str(), &tw, &th);
        SDLH_DrawText(13, x, y + (20 - (int)th) / 2, COLOR_TEXT2, items[i].label.c_str());
        x += (int)tw;
        if (i + 1 < items.size())
            x += 24;
    }
}

int UiKit::keySquareWidth(const std::string& label)
{
    u32 tw;
    SDLH_GetTextDimensions(11, label.c_str(), &tw, NULL);
    return std::max(22, (int)tw + 12);
}

void UiKit::drawKeySquare(int x, int y, const std::string& label, bool onFilledButton)
{
    u32 tw, th;
    SDLH_GetTextDimensions(11, label.c_str(), &tw, &th);
    int w = std::max(22, (int)tw + 12);
    int h = 22;

    SDL_Color bg = onFilledButton ? FC_MakeColor(0, 0, 0, 71) : COLOR_FILL3;
    Shapes::fillRound(x, y, w, h, 7, bg);

    SDL_Color textColor = onFilledButton ? COLOR_WHITE : COLOR_TEXT;
    SDLH_DrawText(11, x + (w - (int)tw) / 2, y + (h - (int)th) / 2, textColor, label.c_str());
}

void UiKit::drawToggle(int x, int y, bool on)
{
    const int w = 46, h = 27, knob = 21, inset = 3;

    SDL_Color track = on ? COLOR_ACCENT : COLOR_FILL3;
    Shapes::fillRound(x, y, w, h, h / 2, track);

    SDL_Color knobColor = on ? COLOR_WHITE : FC_MakeColor(142, 142, 153, 255);
    int knobX           = on ? x + w - inset - knob : x + inset;
    Shapes::fillRound(knobX, y + inset, knob, knob, knob / 2, knobColor);
}

int UiKit::segmentedWidth(const std::vector<std::string>& options)
{
    int w = 4 * 2; // container padding, both sides
    for (const std::string& opt : options) {
        u32 tw;
        SDLH_GetTextDimensions(13, opt.c_str(), &tw, NULL);
        w += (int)tw + 20 * 2;
    }
    return w;
}

int UiKit::drawSegmented(int x, int y, const std::vector<std::string>& options, int activeIndex)
{
    u32 refH;
    SDLH_GetTextDimensions(13, "Ag", NULL, &refH);
    const int segH       = (int)refH + 8 * 2;
    const int totalW     = segmentedWidth(options);
    const int containerH = segH + 4 * 2;

    // Rectangular segments (radius 0): rounded corners rasterise pixelated at
    // this size, so the segmented control is kept square like the other options.
    Shapes::fillRound(x, y, totalW, containerH, 0, COLOR_FILL2);

    int cx = x + 4, cy = y + 4;
    for (size_t i = 0; i < options.size(); i++) {
        u32 tw, th;
        SDLH_GetTextDimensions(13, options[i].c_str(), &tw, &th);
        int segW    = (int)tw + 20 * 2;
        bool active = (int)i == activeIndex;
        if (active) {
            Shapes::fillRound(cx, cy, segW, segH, 0, COLOR_ACCENT);
        }
        SDL_Color textColor = active ? COLOR_WHITE : COLOR_TEXT2;
        SDLH_DrawText(13, cx + 20, cy + (segH - (int)th) / 2, textColor, options[i].c_str());
        cx += segW;
    }
    return totalW;
}

void UiKit::drawSectionLabel(int x, int y, const std::string& text)
{
    SDLH_DrawText(11, x, y, COLOR_TEXT3, text.c_str());
}
