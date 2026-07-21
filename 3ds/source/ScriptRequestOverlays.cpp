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
#include "glyphs.hpp"
#include "i18n.hpp"
#include "scriptrunner.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>

namespace {
    void respond(UiResponse resp)
    {
        ScriptRunner::get().bridge().respond(std::move(resp));
    }

    // List rows are single-line, but script-supplied strings can carry newlines
    // (3DS SMDH long titles store the two-line marquee form as "line1\nline2").
    // C2D_DrawText honours those breaks and wraps the row mid-screen, so collapse
    // any CR/LF run to a single space before the row is measured and drawn.
    std::string flattenLine(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        bool pendingSpace = false;
        for (char c : s) {
            if (c == '\n' || c == '\r') {
                pendingSpace = !out.empty();
                continue;
            }
            if (pendingSpace) {
                out.push_back(' ');
                pendingSpace = false;
            }
            out.push_back(c);
        }
        return out;
    }
}

/* ---- gui_message ------------------------------------------------------- */

ScriptMessageOverlay::ScriptMessageOverlay(Screen& screen, const std::string& text) : Overlay(screen)
{
    mButton = std::make_unique<Clickable>(
        ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, COLOR_ACCENT, COLOR_WHITE, " OK", true);
    mButton->selected(true);
    mText = StringUtils::wrap(text, SIZE, ModalChrome::TEXT_MAX_W);
    mPosx = ceilf((320 - StringUtils::textWidth(mText, SIZE)) / 2);
    mPosy = 54 + ceilf((88 - StringUtils::textHeight(mText, SIZE)) / 2);
}

void ScriptMessageOverlay::drawTop(void) const
{
    ModalChrome::dimTop();
}

void ScriptMessageOverlay::drawBottom(void) const
{
    ModalChrome::dimBottom();
    ModalChrome::drawCard(COLOR_LINE);
    TextPool::get().draw(mText, mPosx, mPosy, SIZE, COLOR_TEXT);
    mButton->draw(0.55f, COLOR_RING);
    Gui::drawPulsingOutline(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 2, COLOR_RING);
}

void ScriptMessageOverlay::update(const InputState& input)
{
    (void)input;
    if (mButton->released() || (hidKeysDown() & (KEY_A | KEY_B))) {
        respond(UiResponse{});
        screen.removeOverlay();
    }
}

/* ---- gui_pick_one ------------------------------------------------------ */

ScriptPickOneOverlay::ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items)
    : ListPickerOverlay(screen, prompt, 48, 28, 0.5f), mItems(std::move(items))
{
}

int ScriptPickOneOverlay::rowCount(void) const
{
    return (int)mItems.size();
}

void ScriptPickOneOverlay::drawEmptyMessage(void) const
{
    TextPool::get().drawCentered(i18n::t("scripts.no_items"), 0, 400, 110, 0.5f, COLOR_MUTED, OVERLAY_Z);
}

void ScriptPickOneOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    TextPool& text = TextPool::get();
    text.draw(text.truncate(flattenLine(mItems[k]), 324, 0.46f), 40, rowY + 5, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string ScriptPickOneOverlay::bottomHints(void) const
{
    return std::string(GLYPH_A) + " " + i18n::t("overlay.select") + "      " + GLYPH_B + " " + i18n::t("common.cancel");
}

void ScriptPickOneOverlay::update(const InputState& input)
{
    (void)input;
    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();
    if ((kDown & KEY_A) && count > 0) {
        UiResponse resp;
        resp.index = (int)mHid.fullIndex();
        respond(std::move(resp));
        screen.removeOverlay();
        return;
    }
    if (kDown & KEY_B) {
        UiResponse resp;
        resp.index = -1;
        respond(std::move(resp));
        screen.removeOverlay();
        return;
    }
}

/* ---- gui_pick_many ----------------------------------------------------- */

ScriptPickManyOverlay::ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected)
    : ListPickerOverlay(screen, prompt, 48, 28, 0.47f), mItems(std::move(items)), mSelected(std::move(preselected))
{
    mSelected.resize(mItems.size(), false);
}

int ScriptPickManyOverlay::rowCount(void) const
{
    return (int)mItems.size();
}

void ScriptPickManyOverlay::drawEmptyMessage(void) const
{
    TextPool::get().drawCentered(i18n::t("scripts.no_items"), 0, 400, 110, 0.5f, COLOR_MUTED, OVERLAY_Z);
}

void ScriptPickManyOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    TextPool& text = TextPool::get();
    text.draw(mSelected[k] ? "[x]" : "[  ]", 40, rowY + 5, 0.46f, mSelected[k] ? COLOR_ACCENT : COLOR_FAINT, OVERLAY_Z);
    text.draw(text.truncate(flattenLine(mItems[k]), 300, 0.46f), 66, rowY + 5, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string ScriptPickManyOverlay::bottomHints(void) const
{
    return std::string(GLYPH_A) + " " + i18n::t("scripts.toggle") + "      START " + i18n::t("hint.confirm") + "      " + GLYPH_B + " " +
           i18n::t("common.cancel");
}

void ScriptPickManyOverlay::update(const InputState& input)
{
    (void)input;
    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();
    if ((kDown & KEY_A) && count > 0) {
        const size_t k = mHid.fullIndex();
        mSelected[k]   = !mSelected[k];
    }
    if (kDown & KEY_START) {
        UiResponse resp;
        resp.confirmed = true;
        resp.selected  = mSelected;
        respond(std::move(resp));
        screen.removeOverlay();
        return;
    }
    if (kDown & KEY_B) {
        UiResponse resp;
        resp.confirmed = false;
        respond(std::move(resp));
        screen.removeOverlay();
        return;
    }
}
