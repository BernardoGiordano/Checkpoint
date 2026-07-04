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

#include "ListPickerOverlay.hpp"
#include "ModalChrome.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>

ListPickerOverlay::ListPickerOverlay(Screen& screen, const std::string& prompt, int listTop, int rowH, float hintScale)
    : Overlay(screen), mHid(VISIBLE, 1), mPrompt(prompt), mListTop(listTop), mRowH(rowH), mHintScale(hintScale)
{
}

void ListPickerOverlay::drawTop(void) const
{
    TextPool& text  = TextPool::get();
    const int count = rowCount();

    ModalChrome::dimTop(0.6f);
    ModalChrome::drawListCard();

    // Header: prompt on the left, "n / total" counter on the right.
    text.draw(mPrompt, 36, 22, 0.5f, COLOR_TEXT, OVERLAY_Z);
    if (count > 0) {
        std::string counter = StringUtils::format("%d / %d", mHid.fullIndex() + 1, count);
        float w             = text.width(counter, 0.42f);
        text.draw(counter, 364 - w, 24, 0.42f, COLOR_FAINT, OVERLAY_Z);
    }
    drawHeaderExtra();
    C2D_DrawRectSolid(36, mListTop - 6, 0.6f, 328, 1, COLOR_LINE);

    if (count == 0) {
        drawEmptyMessage();
        return;
    }

    const int start = mHid.page() * (int)VISIBLE;
    for (int i = 0; i < (int)VISIBLE && start + i < count; i++) {
        const int k    = start + i;
        const int rowY = mListTop + i * mRowH;
        const bool sel = i == (int)mHid.index();
        if (sel) {
            C2D_DrawRectSolid(30, rowY, 0.6f, 340, mRowH - 2, COLOR_ROW_SELECT);
            Gui::drawOutline(30, rowY, 340, mRowH - 2, 1, COLOR_ACCENT);
        }
        drawRowContent(k, rowY, sel);
    }
}

void ListPickerOverlay::drawBottom(void) const
{
    ModalChrome::dimBottom(0.6f);
    TextPool::get().drawCentered(bottomHints(), 0, 320, 112, mHintScale, COLOR_TEXT, OVERLAY_Z);
}
