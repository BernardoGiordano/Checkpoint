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

ErrorOverlay::ErrorOverlay(Screen& screen, Result mres, const std::string& mtext) : Overlay(screen)
{
    res  = mres;
    text = mtext;
    SDLH_GetTextDimensions(28, text.c_str(), &textw, &texth);
    button = std::make_unique<Clickable>(322, 462, 636, 56, COLOR_BLACK_DARKERR, COLOR_WHITE, "OK", true);
    button->selected(true);
}

void ErrorOverlay::draw(void) const
{
    SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);
    SDLH_DrawRect(320, 200, 640, 260, COLOR_BLACK);
    SDLH_DrawText(20, 330, 210, COLOR_RED, StringUtils::format("Error: 0x%0llX", res).c_str());
    SDLH_DrawText(28, ceilf(1280 - textw) / 2, 200 + ceilf((260 - texth) / 2), COLOR_WHITE, text.c_str());
    button->draw(28, COLOR_RED);
    drawPulsingOutline(322, 462, 636, 56, 4, COLOR_RED);
}

void ErrorOverlay::update(const InputState& input)
{
    const u64 kDown = input.kDown;
    if (button->released() || (kDown & HidNpadButton_A) || (kDown & HidNpadButton_B)) {
        screen.removeOverlay();
    }
}