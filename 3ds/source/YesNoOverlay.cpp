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
    textBuf = C2D_TextBufNew(64);
    C2D_TextParse(&text, textBuf, mtext.c_str());
    C2D_TextOptimize(&text);

    yesFunc = callbackYes;
    noFunc  = callbackNo;

    posx = ceilf(320 - text.width * 0.6) / 2;
    posy = 40 + ceilf(120 - 0.6f * fontGetInfo(NULL)->lineFeed) / 2;

    buttonYes = std::make_unique<Clickable>(42, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE000 Yes", true);
    buttonNo  = std::make_unique<Clickable>(162, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE001 No", true);
}

YesNoOverlay::~YesNoOverlay(void)
{
    C2D_TextBufDelete(textBuf);
}

void YesNoOverlay::drawTop(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
}

void YesNoOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
    C2D_DrawText(&text, C2D_WithColor, posx, posy, 0.5f, 0.6f, 0.6f, COLOR_WHITE);
    C2D_DrawRectSolid(40, 160, 0.5f, 240, 40, COLOR_GREY_LIGHT);

    buttonYes->draw(0.7, 0);
    buttonNo->draw(0.7, 0);

    if (hid.index() == 0) {
        Gui::drawPulsingOutline(42, 162, 116, 36, 2, COLOR_BLUE);
    }
    else {
        Gui::drawPulsingOutline(162, 162, 116, 36, 2, COLOR_BLUE);
    }
}

void YesNoOverlay::update(TouchScreen* touch)
{
    hid.update(2);

    hid.index(buttonYes->held() ? 0 : buttonNo->held() ? 1 : hid.index());
    buttonYes->selected(hid.index() == 0);
    buttonNo->selected(hid.index() == 1);

    if (buttonYes->released() || ((hidKeysDown() & KEY_A) && hid.index() == 0)) {
        yesFunc();
    }
    else if (buttonNo->released() || (hidKeysDown() & KEY_B) || ((hidKeysDown() & KEY_A) && hid.index() == 1)) {
        noFunc();
    }
}