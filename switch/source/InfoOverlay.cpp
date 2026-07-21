/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#include "InfoOverlay.hpp"
#include "ModalChrome.hpp"
#include "gfxutils.hpp"

InfoOverlay::InfoOverlay(Screen& screen, const std::string& mtext) : Overlay(screen)
{
    text   = mtext;
    button = std::make_unique<Clickable>(
        ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, COLOR_SURFACE, COLOR_TEXT, "OK", true);
    button->selected(true);
}

void InfoOverlay::draw(void) const
{
    ModalChrome::dim();
    ModalChrome::drawCard(COLOR_SURFACE);
    ModalChrome::drawText(text, COLOR_TEXT);
    button->draw(ModalChrome::BTN_SIZE, COLOR_ACCENT);
    drawPulsingOutline(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 4, COLOR_ACCENT);
}

void InfoOverlay::update(const InputState& input)
{
    const u64 kDown = input.kDown;
    if (button->released() || (kDown & HidNpadButton_A) || (kDown & HidNpadButton_B)) {
        screen.removeOverlay();
    }
}