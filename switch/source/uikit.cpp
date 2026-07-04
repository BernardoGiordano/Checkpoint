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
    // The button glyphs are drawn from the shared font at this
    // pixel size in the hint bar (a ~20px circle, matching the 20px slot).
    constexpr int HINT_GLYPH_SIZE = 20;

    int hintItemWidth(const UiKit::HintItem& item)
    {
        u32 gw, tw;
        Gfx::GetTextDimensions(HINT_GLYPH_SIZE, UiKit::buttonGlyph(item.glyph).c_str(), &gw, NULL);
        Gfx::GetTextDimensions(13, item.label.c_str(), &tw, NULL);
        return (int)gw + 7 + (int)tw;
    }
}

std::string UiKit::buttonGlyph(const std::string& key)
{
    // shared-font code points: each is a filled circle/pill
    // with the button symbol already inside it.
    if (key == "A")
        return "";
    if (key == "B")
        return "";
    if (key == "X")
        return "";
    if (key == "Y")
        return "";
    if (key == "L")
        return "";
    if (key == "R")
        return "";
    if (key == "ZL")
        return "";
    if (key == "ZR")
        return "";
    if (key == "+")
        return "";
    if (key == "-")
        return "";
    return key;
}

void UiKit::drawHintCircle(int x, int y, const std::string& glyph)
{
    // The system glyph already includes the circle; draw it centered in the
    // 20px-tall hint slot rather than compositing a circle + letter by hand.
    const std::string g = buttonGlyph(glyph);
    Color glyphColor    = COLOR_TEXT;
    u32 tw, th;
    Gfx::GetTextDimensions(HINT_GLYPH_SIZE, g.c_str(), &tw, &th);
    Gfx::DrawText(HINT_GLYPH_SIZE, x, y + (20 - (int)th) / 2, glyphColor, g.c_str());
}

void UiKit::drawHintBar(const std::vector<HintItem>& items)
{
    // Opaque fill so an overlay's hint bar fully hides the page's hint bar
    // underneath (the scrim only dims it, leaving both right-aligned rows to
    // bleed together otherwise).
    Gfx::DrawRect(0, HINTBAR_Y, SCREEN_W, HINTBAR_H, COLOR_BG);
    Gfx::DrawRect(0, HINTBAR_Y, SCREEN_W, 1, COLOR_STROKE1);

    int totalW = 0;
    for (size_t i = 0; i < items.size(); i++) {
        totalW += hintItemWidth(items[i]);
        if (i + 1 < items.size())
            totalW += 24;
    }

    int x = SCREEN_W - 26 - totalW;
    int y = HINTBAR_Y + (HINTBAR_H - 20) / 2;
    for (size_t i = 0; i < items.size(); i++) {
        u32 gw;
        Gfx::GetTextDimensions(HINT_GLYPH_SIZE, buttonGlyph(items[i].glyph).c_str(), &gw, NULL);
        drawHintCircle(x, y, items[i].glyph);
        x += (int)gw + 7;

        u32 tw, th;
        Gfx::GetTextDimensions(13, items[i].label.c_str(), &tw, &th);
        Gfx::DrawText(13, x, y + (20 - (int)th) / 2, COLOR_TEXT2, items[i].label.c_str());
        x += (int)tw;
        if (i + 1 < items.size())
            x += 24;
    }
}

void UiKit::drawToggle(int x, int y, bool on)
{
    const int w = 46, h = 27, knob = 21, inset = 3;

    // Rectangular track + square knob (radius 0) to match the squared controls
    // elsewhere in the redesign.
    Color track = on ? COLOR_ACCENT : COLOR_FILL3;
    Shapes::fillRound(x, y, w, h, 0, track);

    Color knobColor = on ? COLOR_WHITE : makeColor(142, 142, 153, 255);
    int knobX       = on ? x + w - inset - knob : x + inset;
    Shapes::fillRound(knobX, y + inset, knob, knob, 0, knobColor);
}

int UiKit::segmentedWidth(const std::vector<std::string>& options)
{
    int w = 4 * 2; // container padding, both sides
    for (const std::string& opt : options) {
        u32 tw;
        Gfx::GetTextDimensions(13, opt.c_str(), &tw, NULL);
        w += (int)tw + 20 * 2;
    }
    return w;
}

int UiKit::drawSegmented(int x, int y, const std::vector<std::string>& options, int activeIndex)
{
    u32 refH;
    Gfx::GetTextDimensions(13, "Ag", NULL, &refH);
    const int segH       = (int)refH + 8 * 2;
    const int totalW     = segmentedWidth(options);
    const int containerH = segH + 4 * 2;

    // Rectangular segments (radius 0): rounded corners rasterise pixelated at
    // this size, so the segmented control is kept square like the other options.
    Shapes::fillRound(x, y, totalW, containerH, 0, COLOR_FILL2);

    int cx = x + 4, cy = y + 4;
    for (size_t i = 0; i < options.size(); i++) {
        u32 tw, th;
        Gfx::GetTextDimensions(13, options[i].c_str(), &tw, &th);
        int segW    = (int)tw + 20 * 2;
        bool active = (int)i == activeIndex;
        if (active) {
            Shapes::fillRound(cx, cy, segW, segH, 0, COLOR_ACCENT);
        }
        Color textColor = active ? COLOR_WHITE : COLOR_TEXT2;
        Gfx::DrawText(13, cx + 20, cy + (segH - (int)th) / 2, textColor, options[i].c_str());
        cx += segW;
    }
    return totalW;
}

void UiKit::drawSectionLabel(int x, int y, const std::string& text)
{
    Gfx::DrawText(11, x, y, COLOR_TEXT3, text.c_str());
}
