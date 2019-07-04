/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#include "YesNoOverlay.hpp"

YesNoOverlay::YesNoOverlay(
    Screen& screen, const std::string& mtext, const std::function<void()>& callbackYes, const std::function<void()>& callbackNo)
    : Overlay(screen), hid(2, 2)
{
    text    = mtext;
    yesFunc = callbackYes;
    noFunc  = callbackNo;
    SDLH_GetTextDimensions(28, text.c_str(), &textw, &texth);
    buttonYes = std::make_unique<Clickable>(322, 462, 316, 56, theme().c3, theme().c6, "Yes", true);
    buttonNo  = std::make_unique<Clickable>(642, 462, 316, 56, theme().c3, theme().c6, "No", true);
}

void YesNoOverlay::draw(void) const
{
    SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);
    SDLH_DrawRect(320, 200, 640, 260, theme().c3);
    SDLH_DrawText(28, ceilf(1280 - textw) / 2, 200 + ceilf((260 - texth) / 2), theme().c6, text.c_str());
    drawOutline(322, 462, 316, 56, 2, theme().c5);
    drawOutline(642, 462, 316, 56, 2, theme().c5);
    buttonYes->draw(28, COLOR_BLUE);
    buttonNo->draw(28, COLOR_BLUE);

    if (hid.index() == 0) {
        drawPulsingOutline(324, 464, 312, 52, 4, COLOR_BLUE);
    }
    else {
        drawPulsingOutline(644, 464, 312, 52, 4, COLOR_BLUE);
    }
}

void YesNoOverlay::update(touchPosition* touch)
{
    hid.update(2);

    hid.index(buttonYes->held() ? 0 : buttonNo->held() ? 1 : hid.index());
    buttonYes->selected(hid.index() == 0);
    buttonNo->selected(hid.index() == 1);

    if (buttonYes->released() || ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) && hid.index() == 0)) {
        yesFunc();
    }
    else if (buttonNo->released() || (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B) || ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) && hid.index() == 1)) {
        noFunc();
    }
}