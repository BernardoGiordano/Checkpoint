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

#ifndef UIKIT_HPP
#define UIKIT_HPP

#include "gfx.hpp"
#include <string>
#include <vector>

// Shared primitives both MainScreen and SettingsScreen draw from. Geometry
// constants below are the ones every screen positions itself against.
namespace UiKit {
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
    constexpr int TOPBAR_H  = 56;
    constexpr int HINTBAR_H = 46;
    constexpr int HINTBAR_Y = SCREEN_H - HINTBAR_H;

    // One hint-bar entry: a button key ("A", "B", "-", …) shown as its system
    // glyph, plus its label, e.g. {"A", "Select"}.
    struct HintItem {
        std::string glyph;
        std::string label;
    };

    // shared-font glyph for a controller button ("A",
    // "B", "X", "Y", "L", "R", "ZL", "ZR", "+", "-") — a circle/pill with the
    // button symbol already inside it. Preferred over any hand-drawn circle
    // wherever a controller button is shown. Unknown keys pass through verbatim.
    std::string buttonGlyph(const std::string& key);

    // Draws buttonGlyph(glyph) centered in the 20px-tall hint slot whose top-left
    // is (x, y). The glyph supplies its own circle, so nothing is composited here.
    void drawHintCircle(int x, int y, const std::string& glyph);

    // The bottom hint bar, identical shape on both screens: top border, items
    // right-aligned ending 26px from the right edge, 24px between items.
    void drawHintBar(const std::vector<HintItem>& items);

    // 46x27 toggle pill; `on` selects accent/fill-3 track + knob position.
    void drawToggle(int x, int y, bool on);

    // Segmented control ("Theme" row): fill-2 pill container, one segment
    // per option, the option at activeIndex drawn accent+white. Returns the
    // total drawn width.
    int drawSegmented(int x, int y, const std::vector<std::string>& options, int activeIndex);
    int segmentedWidth(const std::vector<std::string>& options);

    // 11px/600 uppercase, text-3, used for both "BACKUPS · 9" (1a) and every
    // settings section label (1b). Does not upper-case `text` itself — pass it
    // already uppercased so callers control acronyms (e.g. "BCAT").
    void drawSectionLabel(int x, int y, const std::string& text);
}

#endif
