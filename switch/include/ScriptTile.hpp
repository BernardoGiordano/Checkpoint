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
#include <string>

// The chrome of the script session's screen. With one screen there is one tile:
// the *log* tile, which owns the display for the whole run and carries the
// transcript, the run's status in its header and the progress bars along its
// bottom edge. A second, half-width tile mirroring the last log line was only
// ever a duplicate of what the transcript already said.
//
// Whatever the script *asks* is a modal instead: a centred card over a dimmed
// log, sized once here so a message, a confirm and the two pickers all land in
// the same box. Everything goes through the helpers below, so a dialog can
// never land somewhere the idle screen did not.
namespace ScriptTile {
    constexpr int SCREEN_W = 1280, SCREEN_H = 720;
    constexpr int MARGIN = 24;

    // The one tile: the whole screen.
    constexpr int LOG_X = MARGIN, LOG_Y = MARGIN;
    constexpr int LOG_W = SCREEN_W - 2 * MARGIN, LOG_H = SCREEN_H - 2 * MARGIN;

    // The modal card a script request draws in, centred over the log. Wide
    // enough for a picker row to read as a row, short enough that the
    // transcript stays visible above and below it.
    constexpr int UI_W = 820, UI_H = 540;
    constexpr int UI_X = (SCREEN_W - UI_W) / 2, UI_Y = (SCREEN_H - UI_H) / 2;

    constexpr int PAD = 24, RADIUS = 12;
    // Header band (title plus the hairline under it) and the hint band at a
    // tile's lower edge.
    constexpr int HEADER_H = 62, HINT_H = 44;

    // Text sizes shared by the tile and the modals that draw over it.
    constexpr int TITLE_SIZE = 22, BODY_SIZE = 19, NOTE_SIZE = 15;
    // The transcript: small and tightly leaded, because a log is scanned in
    // blocks and every pixel of leading costs a row.
    constexpr int LOG_SIZE = 14, LOG_LINE_TIGHTEN = 3;

    // Tile background + border.
    void card(int x, int y, int w, int h);

    // Draws a tile's card and its header: `title` on the left, ellipsised to
    // fit, `note` right-aligned beside it, a hairline under both. Returns the y
    // of the first free row below.
    int frame(int x, int y, int w, int h, const std::string& title, const std::string& note = "");

    // The hint line along a tile's lower edge.
    void hints(int x, int y, int w, const std::string& text);

    // The modal card: a dimmed log behind it, then frame() over the card.
    int uiFrame(const std::string& title, const std::string& note = "");
    void uiHints(const std::string& text);

    // The y one past the last row a tile's body may use (its hint band's top).
    constexpr int bodyBottom(int y, int h)
    {
        return y + h - HINT_H;
    }
    constexpr int uiBodyBottom(void)
    {
        return bodyBottom(UI_Y, UI_H);
    }
    constexpr int logBodyBottom(void)
    {
        return bodyBottom(LOG_Y, LOG_H);
    }

    // One progress bar: track, fill, and the label / right-hand value beneath.
    // Total height is BAR_ROW_H.
    constexpr int BAR_H = 14, BAR_ROW_H = 44;
    void bar(int x, int y, int w, const ScriptConsole::Layer& layer);

    // Collects the active bars of `snapshot`, outermost first, into `out` (room
    // for MAX_BARS). Returns how many were written. The script's own layers come
    // first, then the reserved IO bar the native long calls drive.
    constexpr size_t MAX_BARS = ScriptConsole::MAX_LAYERS + 1;
    size_t collectBars(const ScriptConsole::ProgressSnapshot& snapshot, ScriptConsole::Layer* out);
}

#endif
