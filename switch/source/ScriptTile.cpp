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

#include "ScriptTile.hpp"
#include "gfx.hpp"
#include "gfxutils.hpp"
#include "scriptbar.hpp"
#include "shapes.hpp"
#include <algorithm>
#include <string>

void ScriptTile::card(int x, int y, int w, int h)
{
    Shapes::cardRound(x, y, w, h, RADIUS, COLOR_SURFACE, COLOR_STROKE2, 1);
}

int ScriptTile::frame(int x, int y, int w, int h, const std::string& title, const std::string& note)
{
    card(x, y, w, h);

    int noteW = 0;
    if (!note.empty()) {
        u32 nw, nh;
        Gfx::GetTextDimensions(NOTE_SIZE, note.c_str(), &nw, &nh);
        Gfx::DrawText(NOTE_SIZE, x + w - PAD - (int)nw, y + PAD + 6, COLOR_TEXT3, note.c_str());
        noteW = (int)nw + 16; // gap between the title and the note
    }

    const std::string fitted = trimToFit(title, w - 2 * PAD - noteW, TITLE_SIZE);
    Gfx::DrawText(TITLE_SIZE, x + PAD, y + PAD, COLOR_TEXT, fitted.c_str());

    const int lineY = y + HEADER_H;
    Gfx::DrawRect(x + PAD, lineY, w - 2 * PAD, 1, COLOR_STROKE1);
    return lineY + 18;
}

void ScriptTile::hints(int x, int y, int w, const std::string& text)
{
    u32 hw, hh;
    Gfx::GetTextDimensions(NOTE_SIZE, text.c_str(), &hw, &hh);
    Gfx::DrawText(NOTE_SIZE, x + w - PAD - (int)hw, y + (HINT_H - (int)hh) / 2, COLOR_TEXT2, text.c_str());
}

int ScriptTile::uiFrame(const std::string& title, const std::string& note)
{
    // The log stays on screen behind the card, dimmed: the answer a script is
    // waiting for usually depends on what it just printed.
    Gfx::DrawRect(0, 0, SCREEN_W, SCREEN_H, COLOR_SCRIM);
    return frame(UI_X, UI_Y, UI_W, UI_H, title, note);
}

void ScriptTile::uiHints(const std::string& text)
{
    hints(UI_X, uiBodyBottom(), UI_W, text);
}

void ScriptTile::bar(int x, int y, int w, const ScriptConsole::Layer& layer)
{
    const float frac = ScriptBar::fraction(layer);

    Shapes::fillRound(x, y, w, BAR_H, BAR_H / 2, COLOR_FILL2);
    const int fillW = (int)(w * frac);
    if (fillW > 0) {
        Shapes::fillRound(x, y, std::max(fillW, BAR_H), BAR_H, BAR_H / 2, COLOR_ACCENT);
    }

    // An idle slot still says what it is and where it got to, dimmed: a bare
    // track with nothing under it reads as a bar that is stuck.
    const Color labelColor = layer.active ? COLOR_TEXT2 : COLOR_TEXT3;
    const Color rightColor = layer.active ? COLOR_TEXT : COLOR_TEXT3;

    const std::string right = ScriptBar::rightText(layer);
    u32 rw = 0, rh = 0;
    if (!right.empty()) {
        Gfx::GetTextDimensions(NOTE_SIZE, right.c_str(), &rw, &rh);
        Gfx::DrawText(NOTE_SIZE, x + w - (int)rw, y + BAR_H + 6, rightColor, right.c_str());
    }
    if (!layer.label.empty()) {
        Gfx::DrawText(NOTE_SIZE, x, y + BAR_H + 6, labelColor, trimToFit(layer.label, w - (int)rw - 12, NOTE_SIZE).c_str());
    }
}

size_t ScriptTile::collectBars(const ScriptConsole::ProgressSnapshot& snapshot, ScriptConsole::Layer* out)
{
    size_t count = 0;
    // Every slot the run has claimed, active or not: an idle slot draws as an
    // empty track so the stack keeps its height between items.
    for (size_t i = 0; i < snapshot.layerSlots; i++) {
        out[count++] = snapshot.layers[i];
    }
    if (snapshot.ioSlot) {
        out[count++] = snapshot.io;
    }
    return count;
}
