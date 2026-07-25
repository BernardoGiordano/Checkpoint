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
#include "gui.hpp"
#include "scriptbar.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <algorithm>

namespace {
    constexpr float TITLE_SIZE = 0.5f, NOTE_SIZE = 0.42f, HINT_SIZE = 0.42f;
    constexpr float BAR_LABEL_SIZE = 0.4f;
}

void ScriptTile::card(int x, int y, int w, int h, u32 outline, float z)
{
    C2D_DrawRectSolid((float)x, (float)y, z, (float)w, (float)h, COLOR_CARD);
    // drawOutline draws *around* the rect, so the ring sits in the margin the
    // tile leaves against the screen edge rather than eating a content pixel.
    Gui::drawOutline(x, y, w, h, 1, outline);
}

int ScriptTile::uiFrame(const std::string& title, const std::string& note, u32 outline, float z)
{
    TextPool& text = TextPool::get();
    card(UI_X, UI_Y, UI_W, UI_H, outline, z);

    float noteW = 0.0f;
    if (!note.empty()) {
        noteW = text.width(note, NOTE_SIZE);
        text.draw(note, UI_X + UI_W - PAD - noteW, UI_Y + PAD + 2, NOTE_SIZE, COLOR_FAINT, z);
        noteW += 8; // gap between the title and the note
    }
    const float titleMax = UI_W - 2 * PAD - noteW;
    text.draw(text.truncate(title, titleMax, TITLE_SIZE), UI_X + PAD, UI_Y + PAD - 2, TITLE_SIZE, COLOR_TEXT, z);

    const int lineY = UI_Y + HEADER_H;
    C2D_DrawRectSolid((float)(UI_X + PAD), (float)lineY, z, (float)(UI_W - 2 * PAD), 1.0f, COLOR_LINE);
    return lineY + 8;
}

void ScriptTile::uiHints(const std::string& hints, float z)
{
    TextPool::get().drawCentered(hints, UI_X, UI_W, uiBodyBottom() + 4, HINT_SIZE, COLOR_MUTED, z);
}

void ScriptTile::bar(int x, int y, int w, const ScriptConsole::Layer& layer, float z)
{
    TextPool& text   = TextPool::get();
    const float frac = ScriptBar::fraction(layer);

    C2D_DrawRectSolid((float)x, (float)y, z, (float)w, (float)BAR_H, COLOR_BLACK_MEDIUM);
    const float fillW = w * frac;
    if (fillW > 0.0f) {
        C2D_DrawRectSolid((float)x, (float)y, z, fillW, (float)BAR_H, COLOR_ACCENT);
    }

    // An idle slot still says what it is and where it got to, dimmed: a bare
    // track with nothing under it reads as a bar that is stuck.
    const u32 labelColor = layer.active ? COLOR_MUTED : COLOR_FAINT;
    const u32 rightColor = layer.active ? COLOR_TEXT : COLOR_FAINT;

    const std::string right = ScriptBar::rightText(layer);
    const float rightW      = right.empty() ? 0.0f : text.width(right, BAR_LABEL_SIZE);
    const float labelY      = y + BAR_H + 2;
    if (!right.empty()) {
        text.draw(right, x + w - rightW, labelY, BAR_LABEL_SIZE, rightColor, z);
    }
    if (!layer.label.empty()) {
        text.draw(text.truncate(layer.label, w - rightW - 8, BAR_LABEL_SIZE), x, labelY, BAR_LABEL_SIZE, labelColor, z);
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
