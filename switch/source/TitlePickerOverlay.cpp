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

#include "TitlePickerOverlay.hpp"
#include "colors.hpp"
#include "gfxutils.hpp"
#include "shapes.hpp"
#include "uikit.hpp"

namespace {
    constexpr int PANEL_X = 290, PANEL_Y = 100, PANEL_W = 700, PANEL_H = 520;
    constexpr int LIST_X = PANEL_X + 20, LIST_Y = PANEL_Y + 64;
    constexpr int LIST_W  = PANEL_W - 40;
    constexpr int ROW_H   = 52;
    constexpr int ROW_GAP = 6;
    constexpr int VISIBLE = (PANEL_H - 64 - 20) / (ROW_H + ROW_GAP);
}

TitlePickerOverlay::TitlePickerOverlay(
    Screen& screen, const std::string& heading, std::vector<std::pair<u64, std::string>> items, std::function<void(u64)> onPick)
    : Overlay(screen), mHeading(heading), mItems(std::move(items)), mOnPick(std::move(onPick))
{
}

void TitlePickerOverlay::draw(void) const
{
    Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
    Shapes::cardRound(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

    u32 hh;
    Gfx::GetTextDimensions(20, mHeading.c_str(), NULL, &hh);
    Gfx::DrawText(20, LIST_X, PANEL_Y + 20, COLOR_TEXT, mHeading.c_str());

    if (mItems.empty()) {
        Gfx::DrawText(15, LIST_X, LIST_Y + 8, COLOR_TEXT2, "No titles available.");
    }

    for (int i = mScroll; i < (int)mItems.size() && i < mScroll + VISIBLE; i++) {
        const int y      = LIST_Y + (i - mScroll) * (ROW_H + ROW_GAP);
        const bool focus = i == mCursor;
        Shapes::fillRound(LIST_X, y, LIST_W, ROW_H, 0, focus ? COLOR_ACCENT_TINT : COLOR_FILL1);
        Color fg = focus ? COLOR_TEXT : COLOR_TEXT2;
        u32 nh;
        Gfx::GetTextDimensions(15, "Ag", NULL, &nh);
        std::string name = trimToFit(mItems[i].second, LIST_W - 28, 15);
        Gfx::DrawText(15, LIST_X + 14, y + (ROW_H - (int)nh) / 2, fg, name.c_str());
        if (focus) {
            Shapes::focusRing(LIST_X, y, LIST_W, ROW_H, 0, COLOR_ACCENT);
        }
    }

    UiKit::drawHintBar({
        {"A", "Choose"},
        {"B", "Cancel"},
    });
}

void TitlePickerOverlay::update(const InputState& input)
{
    const u64 kdown = input.kDown;

    if (kdown & HidNpadButton_B) {
        me.reset();
        return;
    }

    if (!mItems.empty()) {
        if (kdown & HidNpadButton_Up) {
            mCursor = mCursor > 0 ? mCursor - 1 : (int)mItems.size() - 1;
        }
        else if (kdown & HidNpadButton_Down) {
            mCursor = mCursor < (int)mItems.size() - 1 ? mCursor + 1 : 0;
        }
        if (mCursor < mScroll)
            mScroll = mCursor;
        else if (mCursor >= mScroll + VISIBLE)
            mScroll = mCursor - VISIBLE + 1;

        if (kdown & HidNpadButton_A) {
            u64 id = mItems[mCursor].first;
            // Dismiss first: mOnPick may raise its own follow-up (a keyboard),
            // and it captures state that outlives this overlay.
            auto pick = mOnPick;
            me.reset();
            if (pick)
                pick(id);
            return;
        }
    }
}
