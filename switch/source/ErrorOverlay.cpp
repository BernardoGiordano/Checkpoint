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

#include "ErrorOverlay.hpp"
#include "ModalChrome.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"

ErrorOverlay::ErrorOverlay(Screen& screen, Result mres, const std::string& mtext) : Overlay(screen)
{
    res    = mres;
    text   = mtext;
    button = std::make_unique<Clickable>(
        ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, COLOR_BG, COLOR_WHITE, "OK", true);
    button->selected(true);
}

void ErrorOverlay::draw(void) const
{
    ModalChrome::dim();
    ModalChrome::drawCard(COLOR_BLACK);
    Gfx::DrawText(20, ModalChrome::TEXT_X, ModalChrome::CARD_Y + 16, COLOR_DANGER,
        StringUtils::format("%s: 0x%0llX", i18n::t("common.error").c_str(), res).c_str());
    ModalChrome::drawText(text, COLOR_WHITE);
    button->draw(ModalChrome::BTN_SIZE, COLOR_DANGER);
    drawPulsingOutline(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 4, COLOR_DANGER);
}

void ErrorOverlay::update(const InputState& input)
{
    const u64 kDown = input.kDown;
    if (button->released() || (kDown & HidNpadButton_A) || (kDown & HidNpadButton_B)) {
        screen.removeOverlay();
    }
}