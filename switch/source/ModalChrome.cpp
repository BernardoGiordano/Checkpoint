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

#include "ModalChrome.hpp"

void ModalChrome::dim(void)
{
    Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
}

void ModalChrome::drawCard(Color surface)
{
    Gfx::DrawRect(CARD_X, CARD_Y, CARD_W, CARD_H, surface);
}

void ModalChrome::drawText(const std::string& text, Color color, int size)
{
    // Text band = from below the card's top padding down to just above the
    // button row. Center the wrapped block in it, both axes.
    const int bandTop = CARD_Y + PAD;
    const int bandBot = BTN_Y - 16;
    u32 w, h;
    Gfx::MeasureTextBox(size, text.c_str(), TEXT_MAX_W, &w, &h);
    const int x = CARD_X + ((int)CARD_W - (int)w) / 2;
    const int y = bandTop + ((bandBot - bandTop) - (int)h) / 2;
    Gfx::DrawTextBox(size, x, y, color, TEXT_MAX_W, text.c_str());
}
