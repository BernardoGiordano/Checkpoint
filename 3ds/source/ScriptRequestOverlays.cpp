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
#include "colors.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "i18n.hpp"
#include "scriptrunner.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>

namespace {
    constexpr float BODY_SIZE = 0.5f, ROW_SIZE = 0.46f, BTN_SIZE = 0.5f;
    // Button row along the tile's lower edge, above the hint line.
    constexpr int BTN_H = 30, BTN_GAP = 10;

    int buttonRowY(void)
    {
        return ScriptTile::uiBodyBottom() - BTN_H - 6;
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

/* ---- shared tile chrome ------------------------------------------------- */

ScriptTileOverlay::ScriptTileOverlay(Screen& screen, std::string title) : Overlay(screen), mTitle(std::move(title)) {}

void ScriptTileOverlay::drawBottom(void) const
{
    // No dim: the tile covers the whole bottom screen already, and the top
    // screen must stay untouched so the log pane keeps reading.
    const int bodyY = ScriptTile::uiFrame(mTitle, headerNote(), COLOR_ACCENT, ScriptTile::Z);
    drawBody(bodyY);
    ScriptTile::uiHints(hints(), ScriptTile::Z);
}

void ScriptTileOverlay::answer(UiResponse resp)
{
    ScriptRunner::get().bridge().respond(std::move(resp));
    screen.removeOverlay();
}

void ScriptTileOverlay::drawWrappedBody(const std::string& text, int bodyY) const
{
    TextPool& text_pool = TextPool::get();
    const float maxW    = (float)(ScriptTile::UI_W - 2 * ScriptTile::PAD);
    std::string wrapped = StringUtils::wrap(text, BODY_SIZE, maxW);

    // Cut to the lines that fit above the button row rather than letting the
    // paragraph run under it.
    const float lineH  = StringUtils::textHeight("A", BODY_SIZE);
    const int maxLines = std::max(1, (int)((buttonRowY() - 6 - bodyY) / (lineH > 0 ? lineH : 1)));
    int lines          = 1;
    for (size_t i = 0; i < wrapped.size(); i++) {
        if (wrapped[i] == '\n' && ++lines > maxLines) {
            wrapped = wrapped.substr(0, i) + "...";
            break;
        }
    }
    text_pool.drawWrapped(wrapped, (float)(ScriptTile::UI_X + ScriptTile::PAD), (float)bodyY, BODY_SIZE, COLOR_TEXT, maxW, ScriptTile::Z);
}

/* ---- gui_message -------------------------------------------------------- */

ScriptMessageOverlay::ScriptMessageOverlay(Screen& screen, std::string text)
    : ScriptTileOverlay(screen, ScriptRunner::get().scriptName()), mText(std::move(text))
{
    mButton = std::make_unique<Clickable>(
        ScriptTile::UI_X + ScriptTile::PAD, buttonRowY(), ScriptTile::UI_W - 2 * ScriptTile::PAD, BTN_H, COLOR_ACCENT, COLOR_WHITE, " OK", true);
    mButton->selected(true);
}

void ScriptMessageOverlay::drawBody(int bodyY) const
{
    drawWrappedBody(mText, bodyY);
    mButton->draw(BTN_SIZE, COLOR_RING);
    Gui::drawPulsingOutline(ScriptTile::UI_X + ScriptTile::PAD, buttonRowY(), ScriptTile::UI_W - 2 * ScriptTile::PAD, BTN_H, 2, COLOR_RING);
}

std::string ScriptMessageOverlay::hints(void) const
{
    // Only this dialog's own keys: the L/R log hint lives on the top screen,
    // which no dialog covers, so every script dialog's hint line is about the
    // dialog and nothing else.
    return std::string(GLYPH_A) + " OK";
}

void ScriptMessageOverlay::update(const InputState& input)
{
    (void)input;
    if (mButton->released() || (hidKeysDown() & (KEY_A | KEY_B))) {
        answer(UiResponse{});
    }
}

/* ---- gui_confirm -------------------------------------------------------- */

namespace {
    // Two half-width buttons on the tile's button row, confirm on the left, the
    // same order YesNoOverlay uses everywhere else in the app.
    constexpr int confirmHalf(void)
    {
        return (ScriptTile::UI_W - 2 * ScriptTile::PAD - BTN_GAP) / 2;
    }
    constexpr int confirmLeftX(void)
    {
        return ScriptTile::UI_X + ScriptTile::PAD;
    }
    constexpr int confirmRightX(void)
    {
        return confirmLeftX() + confirmHalf() + BTN_GAP;
    }
}

ScriptConfirmOverlay::ScriptConfirmOverlay(Screen& screen, std::string text)
    : ScriptTileOverlay(screen, ScriptRunner::get().scriptName()), mText(std::move(text))
{
    mConfirm = std::make_unique<Clickable>(
        confirmLeftX(), buttonRowY(), confirmHalf(), BTN_H, COLOR_ACCENT, COLOR_WHITE, " " + i18n::t("hint.confirm"), true);
    mCancel = std::make_unique<Clickable>(
        confirmRightX(), buttonRowY(), confirmHalf(), BTN_H, COLOR_RAISED, COLOR_TEXT, " " + i18n::t("common.cancel"), true);
}

void ScriptConfirmOverlay::drawBody(int bodyY) const
{
    drawWrappedBody(mText, bodyY);
    mConfirm->selected(mConfirmSelected);
    mCancel->selected(!mConfirmSelected);
    mConfirm->draw(BTN_SIZE, COLOR_RING);
    mCancel->draw(BTN_SIZE, COLOR_RING);
    Gui::drawPulsingOutline(mConfirmSelected ? confirmLeftX() : confirmRightX(), buttonRowY(), confirmHalf(), BTN_H, 2, COLOR_RING);
}

std::string ScriptConfirmOverlay::hints(void) const
{
    // A activates whichever button is highlighted, which is not always Confirm —
    // so the hint says "select", the same word the pickers use for A.
    return std::string(GLYPH_A) + " " + i18n::t("overlay.select") + "     " + GLYPH_B + " " + i18n::t("common.cancel");
}

void ScriptConfirmOverlay::update(const InputState& input)
{
    (void)input;
    const u32 kDown = hidKeysDown();
    if (kDown & (KEY_LEFT | KEY_RIGHT)) {
        mConfirmSelected = (kDown & KEY_LEFT) != 0;
    }

    UiResponse resp;
    if (mConfirm->released() || ((kDown & KEY_A) && mConfirmSelected)) {
        resp.confirmed = true;
        answer(std::move(resp));
        return;
    }
    if (mCancel->released() || (kDown & KEY_B) || (kDown & KEY_A)) {
        answer(std::move(resp));
    }
}

/* ---- shared list body --------------------------------------------------- */

ScriptListOverlay::ScriptListOverlay(Screen& screen, std::string prompt, size_t count)
    : ScriptTileOverlay(screen, std::move(prompt)), mCount(count), mCursor(VISIBLE)
{
    mCursor.setCount(count);
}

std::string ScriptListOverlay::headerNote(void) const
{
    return mCount > 0 ? StringUtils::format("%zu / %zu", mCursor.index() + 1, mCount) : "";
}

void ScriptListOverlay::drawBody(int bodyY) const
{
    if (mCount == 0) {
        TextPool::get().drawCentered(
            i18n::t("scripts.no_items"), ScriptTile::UI_X, ScriptTile::UI_W, bodyY + 30, BODY_SIZE, COLOR_FAINT, ScriptTile::Z);
        return;
    }

    const size_t first = mCursor.offset();
    for (size_t i = 0; i < VISIBLE && first + i < mCount; i++) {
        const size_t k = first + i;
        const int rowY = bodyY + (int)i * ROW_H;
        const bool sel = k == mCursor.index();
        if (sel) {
            C2D_DrawRectSolid(
                (float)(ScriptTile::UI_X + 4), (float)rowY, ScriptTile::Z, (float)(ScriptTile::UI_W - 8), (float)(ROW_H - 2), COLOR_ROW_SELECT);
            Gui::drawOutline(ScriptTile::UI_X + 4, rowY, ScriptTile::UI_W - 8, ROW_H - 2, 1, COLOR_ACCENT);
        }
        drawRow(k, rowY, sel);
    }
}

/* ---- gui_pick_one ------------------------------------------------------- */

ScriptPickOneOverlay::ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items)
    : ScriptListOverlay(screen, prompt, items.size()), mItems(std::move(items))
{
}

void ScriptPickOneOverlay::drawRow(size_t k, int rowY, bool selected) const
{
    TextPool& text   = TextPool::get();
    const float maxW = ScriptTile::UI_W - 2 * ScriptTile::PAD - 8;
    text.draw(text.truncate(flattenLine(mItems[k]), maxW, ROW_SIZE), ScriptTile::UI_X + ScriptTile::PAD, rowY + 5, ROW_SIZE,
        selected ? COLOR_TEXT : COLOR_MUTED, ScriptTile::Z);
}

std::string ScriptPickOneOverlay::hints(void) const
{
    if (mCount == 0) {
        return std::string(GLYPH_B) + " " + i18n::t("common.cancel");
    }
    return std::string(GLYPH_A) + " " + i18n::t("overlay.select") + "     " + GLYPH_B + " " + i18n::t("common.cancel");
}

void ScriptPickOneOverlay::update(const InputState& input)
{
    (void)input;
    const u32 kDown = hidKeysDown();
    mCursor.update(kDown, hidKeysHeld(), svcGetSystemTick());

    if ((kDown & KEY_A) && mCount > 0) {
        UiResponse resp;
        resp.index = (int)mCursor.index();
        answer(std::move(resp));
        return;
    }
    if (kDown & KEY_B) {
        UiResponse resp;
        resp.index = -1;
        answer(std::move(resp));
    }
}

/* ---- gui_pick_many ------------------------------------------------------ */

ScriptPickManyOverlay::ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected)
    : ScriptListOverlay(screen, prompt, items.size()), mItems(std::move(items)), mSelected(std::move(preselected))
{
    mSelected.resize(mItems.size(), false);
}

void ScriptPickManyOverlay::drawRow(size_t k, int rowY, bool selected) const
{
    TextPool& text   = TextPool::get();
    const int textX  = ScriptTile::UI_X + ScriptTile::PAD + 26;
    const float maxW = (float)(ScriptTile::UI_X + ScriptTile::UI_W - ScriptTile::PAD - 4 - textX);
    text.draw(mSelected[k] ? "[x]" : "[ ]", ScriptTile::UI_X + ScriptTile::PAD, rowY + 5, ROW_SIZE, mSelected[k] ? COLOR_ACCENT : COLOR_FAINT,
        ScriptTile::Z);
    text.draw(text.truncate(flattenLine(mItems[k]), maxW, ROW_SIZE), textX, rowY + 5, ROW_SIZE, selected ? COLOR_TEXT : COLOR_MUTED, ScriptTile::Z);
}

std::string ScriptPickManyOverlay::hints(void) const
{
    if (mCount == 0) {
        return std::string(GLYPH_B) + " " + i18n::t("common.cancel");
    }
    // X confirms, not START: it is the same key the Switch build uses, and START
    // already means "close the finished run" on this screen.
    return std::string(GLYPH_A) + " " + i18n::t("scripts.toggle") + "   " + GLYPH_X + " " + i18n::t("hint.confirm") + "   " + GLYPH_B + " " +
           i18n::t("common.cancel");
}

void ScriptPickManyOverlay::update(const InputState& input)
{
    (void)input;
    const u32 kDown = hidKeysDown();
    mCursor.update(kDown, hidKeysHeld(), svcGetSystemTick());

    if ((kDown & KEY_A) && mCount > 0) {
        const size_t k = mCursor.index();
        mSelected[k]   = !mSelected[k];
    }
    if (kDown & KEY_X) {
        UiResponse resp;
        resp.confirmed = true;
        resp.selected  = mSelected;
        answer(std::move(resp));
        return;
    }
    if (kDown & KEY_B) {
        answer(UiResponse{});
    }
}
