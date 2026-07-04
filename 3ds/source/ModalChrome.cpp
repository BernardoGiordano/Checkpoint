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

void ModalChrome::dimTop(float z)
{
    C2D_DrawRectSolid(0, 0, z, 400, 240, COLOR_OVERLAY);
}

void ModalChrome::dimBottom(float z)
{
    C2D_DrawRectSolid(0, 0, z, 320, 240, COLOR_OVERLAY);
}

void ModalChrome::drawCard(u32 outlineColor)
{
    C2D_DrawRectSolid(CARD_X, CARD_Y, 0.5f, CARD_W, CARD_H, COLOR_CARD);
    Gui::drawOutline(CARD_X, CARD_Y, CARD_W, CARD_H, 2, outlineColor);
}

void ModalChrome::drawListCard(void)
{
    C2D_DrawRectSolid(LIST_X, LIST_Y, 0.6f, LIST_W, LIST_H, COLOR_CARD);
    Gui::drawOutline(LIST_X, LIST_Y, LIST_W, LIST_H, 2, COLOR_ACCENT);
}
