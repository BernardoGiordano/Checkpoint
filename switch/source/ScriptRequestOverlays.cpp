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

#include "ScriptRequestOverlays.hpp"
#include "ModalChrome.hpp"
#include "colors.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "scriptrunner.hpp"
#include "shapes.hpp"
#include "uikit.hpp"
#include <cmath>

namespace {
    // Same panel geometry as the script/title pickers.
    constexpr int PANEL_X = 290, PANEL_Y = 100, PANEL_W = 700, PANEL_H = 520;
    constexpr int LIST_X = PANEL_X + 20, LIST_Y = PANEL_Y + 64;
    constexpr int LIST_W  = PANEL_W - 40;
    constexpr int ROW_H   = 52;
    constexpr int ROW_GAP = 6;
    constexpr int VISIBLE = (PANEL_H - 64 - 20) / (ROW_H + ROW_GAP);

    ScriptUiBridge& bridge(void)
    {
        return ScriptRunner::get().bridge();
    }

    void drawListPanel(const std::string& prompt, const std::vector<std::string>& items, int cursor, int scroll, const std::vector<bool>* selected)
    {
        Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
        Shapes::cardRound(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

        Gfx::DrawText(20, LIST_X, PANEL_Y + 20, COLOR_TEXT, trimToFit(prompt, LIST_W, 20).c_str());

        if (items.empty()) {
            Gfx::DrawText(15, LIST_X, LIST_Y + 8, COLOR_TEXT2, i18n::t("scripts.no_items").c_str());
        }

        for (int i = scroll; i < (int)items.size() && i < scroll + VISIBLE; i++) {
            const int y      = LIST_Y + (i - scroll) * (ROW_H + ROW_GAP);
            const bool focus = i == cursor;
            Shapes::fillRound(LIST_X, y, LIST_W, ROW_H, 0, focus ? COLOR_ACCENT_TINT : COLOR_FILL1);
            const Color fg = focus ? COLOR_TEXT : COLOR_TEXT2;

            int textX = LIST_X + 14;
            if (selected) {
                // Checkbox: a small square, filled accent with a check when on.
                const int boxY = y + (ROW_H - 22) / 2;
                if ((*selected)[i]) {
                    Shapes::fillRound(textX, boxY, 22, 22, 4, COLOR_ACCENT);
                    u32 gw, gh;
                    Gfx::GetTextDimensions(14, "", &gw, &gh);
                    Gfx::DrawText(14, textX + (22 - (int)gw) / 2, boxY + (22 - (int)gh) / 2, COLOR_WHITE, "");
                }
                else {
                    Shapes::strokeRound(textX, boxY, 22, 22, 4, 2, COLOR_STROKE3);
                }
                textX += 22 + 12;
            }

            u32 nh;
            Gfx::GetTextDimensions(15, "Ag", NULL, &nh);
            std::string name = trimToFit(items[i], LIST_X + LIST_W - 14 - textX, 15);
            Gfx::DrawText(15, textX, y + (ROW_H - (int)nh) / 2, fg, name.c_str());
            if (focus) {
                Shapes::focusRing(LIST_X, y, LIST_W, ROW_H, 0, COLOR_ACCENT);
            }
        }
    }

    void moveCursor(const InputState& input, int count, int& cursor, int& scroll)
    {
        if (count <= 0) {
            return;
        }
        if (input.kDown & HidNpadButton_Up) {
            cursor = cursor > 0 ? cursor - 1 : count - 1;
        }
        else if (input.kDown & HidNpadButton_Down) {
            cursor = cursor < count - 1 ? cursor + 1 : 0;
        }
        if (cursor < scroll)
            scroll = cursor;
        else if (cursor >= scroll + VISIBLE)
            scroll = cursor - VISIBLE + 1;
    }
}

/* ---- gui_message ------------------------------------------------------- */

ScriptMessageOverlay::ScriptMessageOverlay(Screen& screen, const std::string& text) : Overlay(screen), mText(text) {}

void ScriptMessageOverlay::draw(void) const
{
    // InfoOverlay's chrome, redrawn here because the dismissal must answer the
    // bridge instead of just closing.
    ModalChrome::dim();
    ModalChrome::drawCard(COLOR_SURFACE);
    ModalChrome::drawText(mText, COLOR_TEXT);

    Shapes::fillRound(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 0, COLOR_SURFACE);
    u32 ow, oh;
    Gfx::GetTextDimensions(ModalChrome::BTN_SIZE, "OK", &ow, &oh);
    Gfx::DrawText(ModalChrome::BTN_SIZE, ModalChrome::BTN_WIDE_X + (ModalChrome::BTN_WIDE_W - (int)ow) / 2,
        ModalChrome::BTN_Y + (ModalChrome::BTN_H - (int)oh) / 2, COLOR_ACCENT, "OK");
    drawPulsingOutline(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 4, COLOR_ACCENT);
}

void ScriptMessageOverlay::update(const InputState& input)
{
    if (input.kDown & (HidNpadButton_A | HidNpadButton_B)) {
        bridge().respond(UiResponse{});
        me.reset();
    }
}

/* ---- gui_pick_one ------------------------------------------------------ */

ScriptPickOneOverlay::ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items)
    : Overlay(screen), mPrompt(prompt), mItems(std::move(items))
{
}

void ScriptPickOneOverlay::draw(void) const
{
    drawListPanel(mPrompt, mItems, mCursor, mScroll, nullptr);
    UiKit::drawHintBar({
        {"A", i18n::t("overlay.choose")},
        {"B", i18n::t("common.cancel")},
    });
}

void ScriptPickOneOverlay::update(const InputState& input)
{
    if (input.kDown & HidNpadButton_B) {
        UiResponse resp;
        resp.index = -1;
        bridge().respond(std::move(resp));
        me.reset();
        return;
    }

    moveCursor(input, (int)mItems.size(), mCursor, mScroll);

    if ((input.kDown & HidNpadButton_A) && !mItems.empty()) {
        UiResponse resp;
        resp.index = mCursor;
        bridge().respond(std::move(resp));
        me.reset();
    }
}

/* ---- gui_pick_many ----------------------------------------------------- */

ScriptPickManyOverlay::ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected)
    : Overlay(screen), mPrompt(prompt), mItems(std::move(items)), mSelected(std::move(preselected))
{
    mSelected.resize(mItems.size(), false);
}

void ScriptPickManyOverlay::draw(void) const
{
    drawListPanel(mPrompt, mItems, mCursor, mScroll, &mSelected);
    UiKit::drawHintBar({
        {"A", i18n::t("scripts.toggle")},
        {"X", i18n::t("overlay.choose")},
        {"B", i18n::t("common.cancel")},
    });
}

void ScriptPickManyOverlay::update(const InputState& input)
{
    if (input.kDown & HidNpadButton_B) {
        bridge().respond(UiResponse{}); // confirmed = false
        me.reset();
        return;
    }

    moveCursor(input, (int)mItems.size(), mCursor, mScroll);

    if ((input.kDown & HidNpadButton_A) && !mItems.empty()) {
        mSelected[mCursor] = !mSelected[mCursor];
    }

    if (input.kDown & HidNpadButton_X) {
        UiResponse resp;
        resp.confirmed = true;
        resp.selected  = mSelected;
        bridge().respond(std::move(resp));
        me.reset();
    }
}
