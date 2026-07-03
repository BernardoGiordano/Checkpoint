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

#include "YesNoOverlay.hpp"
#include "textpool.hpp"

YesNoOverlay::YesNoOverlay(
    Screen& screen, const std::string& mtext, const std::function<void()>& callbackYes, const std::function<void()>& callbackNo)
    : Overlay(screen), text(mtext), hid(2, 2)
{
    yesFunc = callbackYes;
    noFunc  = callbackNo;

    buttonNo  = std::make_unique<Clickable>(46, 142, 110, 32, COLOR_RAISED, COLOR_TEXT, "\uE001 Cancel", true);
    buttonYes = std::make_unique<Clickable>(164, 142, 110, 32, COLOR_ACCENT, COLOR_WHITE, "\uE000 Confirm", true);
}

void YesNoOverlay::drawTop(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
}

void YesNoOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(34, 54, 0.5f, 252, 132, COLOR_CARD);
    Gui::drawOutline(34, 54, 252, 132, 2, COLOR_LINE);
    TextPool::get().drawCentered(text, 0, 320, 84, 0.55f, COLOR_TEXT);

    buttonYes->draw(0.55f, COLOR_RING);
    buttonNo->draw(0.55f, COLOR_RING);

    if (hid.index() == 0) {
        Gui::drawPulsingOutline(164, 142, 110, 32, 2, COLOR_RING);
    }
    else {
        Gui::drawPulsingOutline(46, 142, 110, 32, 2, COLOR_RING);
    }
}

void YesNoOverlay::update(const InputState& input)
{
    (void)input;
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