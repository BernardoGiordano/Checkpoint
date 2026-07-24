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

#ifndef SCRIPTTILE_HPP
#define SCRIPTTILE_HPP

#include "colors.hpp"
#include "scriptconsole.hpp"
#include <citro2d.h>
#include <string>

// The chrome of the script session's two tiles. A running script owns both
// screens, laid out like a tiling window manager: one tile per screen, each
// inset from the screen edge by MARGIN, never overlapping and never floating.
//
// The top screen is the *log* tile and nothing else — the whole transcript, as
// many rows as the screen holds. The bottom screen is the *interaction* tile:
// ScriptScreen draws the status line and the progress bars there while the
// script works, and each script request overlay redraws it as its own dialog.
// Both go through the helpers here, so a dialog can never land somewhere the
// idle tile did not, and the log is never covered by either.
namespace ScriptTile {
    constexpr int MARGIN = 6;

    // Top screen (400x240): the log transcript, full height.
    constexpr int LOG_X = MARGIN, LOG_Y = MARGIN;
    constexpr int LOG_W = 400 - 2 * MARGIN, LOG_H = 240 - 2 * MARGIN;

    // Bottom screen (320x240): status and progress, or whatever the script is
    // asking.
    constexpr int UI_X = MARGIN, UI_Y = MARGIN;
    constexpr int UI_W = 320 - 2 * MARGIN, UI_H = 240 - 2 * MARGIN;

    // Padding from a tile's edge to its content, the header band's height (title
    // text plus the hairline under it) and the hint band reserved at the bottom.
    constexpr int PAD = 10, HEADER_H = 30, HINT_H = 22;

    // The log tile pays less for its chrome than the interaction tile: every
    // pixel it gives back is another readable row of transcript.
    constexpr int LOG_PAD = 6, LOG_HEADER_H = 22;

    // Overlays draw above the screen layer; the tiles themselves are at 0.5.
    constexpr float Z = 0.62f;

    // Tile background + hairline outline.
    void card(int x, int y, int w, int h, u32 outline, float z);

    // Draws the bottom tile's card and its header (`title`, ellipsised to fit,
    // with `note` right-aligned on the same line). Returns the y of the first
    // free row under the header hairline.
    int uiFrame(const std::string& title, const std::string& note = "", u32 outline = COLOR_LINE, float z = Z);

    // The hint line along the bottom tile's lower edge.
    void uiHints(const std::string& hints, float z = Z);

    // The y one past the last row the body may use (the hint band's top).
    constexpr int uiBodyBottom(void)
    {
        return UI_Y + UI_H - HINT_H;
    }

    // One progress bar: track, fill, and the label / right-hand value beneath.
    // Total height is BAR_ROW_H. Unlike Gui::drawProgressBar this takes a depth,
    // because the bars live on the bottom tile and have to sit *above* a card
    // that an overlay may have drawn at Z.
    constexpr int BAR_H = 6, BAR_ROW_H = 22;
    void bar(int x, int y, int w, const ScriptConsole::Layer& layer, float z = Z);

    // Collects the active bars of `snapshot`, outermost first, into `out` (room
    // for MAX_BARS). Returns how many were written. The script's own layers come
    // first, then the reserved IO bar the native long calls drive.
    constexpr size_t MAX_BARS = ScriptConsole::MAX_LAYERS + 1;
    size_t collectBars(const ScriptConsole::ProgressSnapshot& snapshot, ScriptConsole::Layer* out);
}

#endif
