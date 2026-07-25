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
#include "ScriptScreen.hpp"
#include "colors.hpp"
#include "gfx.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "scriptrunner.hpp"
#include "shapes.hpp"
#include "uikit.hpp"
#include <algorithm>

namespace {
    constexpr int INNER_X = ScriptTile::UI_X + ScriptTile::PAD;
    constexpr int INNER_W = ScriptTile::UI_W - 2 * ScriptTile::PAD;

    ScriptUiBridge& bridge(void)
    {
        return ScriptRunner::get().bridge();
    }
}

/* ---- shared tile chrome ------------------------------------------------- */

ScriptTileOverlay::ScriptTileOverlay(Screen& screen, std::string title) : Overlay(screen), mTitle(std::move(title)) {}

void ScriptTileOverlay::draw(void) const
{
    // Focused log: draw nothing at all, not even the scrim. The point is the
    // lines the card was covering, and ScriptScreen's hint row is what says the
    // dialog is still there.
    if (ScriptScreen::logFocused()) {
        return;
    }

    // uiFrame dims the log and centres the card over it: the transcript stays
    // legible around the dialog, which is usually the context for the answer.
    const int bodyY = ScriptTile::uiFrame(mTitle, headerNote());
    drawBody(bodyY);
    // Every dialog offers the same escape to the transcript, so the Y hint is
    // appended here rather than repeated in four hints() overrides.
    ScriptTile::uiHints(hints() + "   " + UiKit::buttonGlyph("Y") + " " + i18n::t("scripts.view_log"));
}

void ScriptTileOverlay::update(const InputState& input)
{
    if (input.kDown & HidNpadButton_Y) {
        ScriptScreen::toggleLogFocus();
        return;
    }
    // With the log focused the pad belongs to the transcript (main.cpp scrolls
    // it), so no key can answer the request by accident while the card that
    // asked it is off screen.
    if (!ScriptScreen::logFocused()) {
        handleInput(input);
    }
}

void ScriptTileOverlay::answer(UiResponse resp)
{
    bridge().respond(std::move(resp));
    me.reset();
}

void ScriptTileOverlay::drawWrappedBody(const std::string& text, int bodyY) const
{
    Gfx::DrawTextBox(ScriptTile::BODY_SIZE, INNER_X, bodyY, COLOR_TEXT, INNER_W, text.c_str());
}

void ScriptTileOverlay::drawButton(int x, int w, const std::string& label, bool focused) const
{
    // Square fill, not fillRound: focusRing is a square pulsing outline (it
    // ignores its radius), so a rounded body would leave the corners of the
    // selected button sticking out of its own ring.
    Gfx::DrawRect(x, buttonRowY(), w, BTN_H, focused ? COLOR_ACCENT : COLOR_FILL1);
    u32 lw, lh;
    Gfx::GetTextDimensions(ScriptTile::BODY_SIZE, label.c_str(), &lw, &lh);
    Gfx::DrawText(
        ScriptTile::BODY_SIZE, x + (w - (int)lw) / 2, buttonRowY() + (BTN_H - (int)lh) / 2, focused ? COLOR_WHITE : COLOR_TEXT2, label.c_str());
    if (focused) {
        Shapes::focusRing(x, buttonRowY(), w, BTN_H, 8, COLOR_ACCENT);
    }
}

/* ---- gui_message -------------------------------------------------------- */

ScriptMessageOverlay::ScriptMessageOverlay(Screen& screen, std::string text)
    : ScriptTileOverlay(screen, ScriptRunner::get().scriptName()), mText(std::move(text))
{
}

void ScriptMessageOverlay::drawBody(int bodyY) const
{
    drawWrappedBody(mText, bodyY);
    drawButton(INNER_X, INNER_W, "OK", true);
}

std::string ScriptMessageOverlay::hints(void) const
{
    // Only this dialog's own keys: the log scroll hint sits on the tile's hint
    // line, which the card does not reach, so every script dialog's hint line
    // is about the dialog and nothing else.
    return UiKit::buttonGlyph("A") + " OK";
}

void ScriptMessageOverlay::handleInput(const InputState& input)
{
    if (input.kDown & (HidNpadButton_A | HidNpadButton_B)) {
        answer(UiResponse{});
    }
}

/* ---- gui_confirm -------------------------------------------------------- */

ScriptConfirmOverlay::ScriptConfirmOverlay(Screen& screen, std::string text)
    : ScriptTileOverlay(screen, ScriptRunner::get().scriptName()), mText(std::move(text))
{
}

void ScriptConfirmOverlay::drawBody(int bodyY) const
{
    drawWrappedBody(mText, bodyY);
    // Two half-width buttons, confirm on the left — the order YesNoOverlay uses
    // everywhere else in the app.
    const int half = (INNER_W - BTN_GAP) / 2;
    drawButton(INNER_X, half, i18n::t("hint.confirm"), mConfirmSelected);
    drawButton(INNER_X + half + BTN_GAP, half, i18n::t("common.cancel"), !mConfirmSelected);
}

std::string ScriptConfirmOverlay::hints(void) const
{
    // A activates whichever button is highlighted, which is not always Confirm —
    // so the hint says "choose", the same word the pickers use for A.
    return UiKit::buttonGlyph("A") + " " + i18n::t("overlay.choose") + "   " + UiKit::buttonGlyph("B") + " " + i18n::t("common.cancel");
}

void ScriptConfirmOverlay::handleInput(const InputState& input)
{
    if (input.kDown & (HidNpadButton_Left | HidNpadButton_Right)) {
        mConfirmSelected = (input.kDown & HidNpadButton_Left) != 0;
    }

    UiResponse resp;
    if ((input.kDown & HidNpadButton_A) && mConfirmSelected) {
        resp.confirmed = true;
        answer(std::move(resp));
        return;
    }
    if (input.kDown & (HidNpadButton_A | HidNpadButton_B)) {
        answer(std::move(resp));
    }
}

/* ---- shared list body --------------------------------------------------- */

ScriptListOverlay::ScriptListOverlay(Screen& screen, std::string prompt, size_t count) : ScriptTileOverlay(screen, std::move(prompt)), mCount(count)
{
}

int ScriptListOverlay::visibleRows(void) const
{
    // The list ends where the hint band starts; there is no button row here.
    const int room = ScriptTile::uiBodyBottom() - (ScriptTile::UI_Y + ScriptTile::HEADER_H + 18);
    return std::max(1, room / (ROW_H + ROW_GAP));
}

std::string ScriptListOverlay::headerNote(void) const
{
    return mCount > 0 ? std::to_string(mCursor + 1) + " / " + std::to_string(mCount) : "";
}

void ScriptListOverlay::drawBody(int bodyY) const
{
    if (mCount == 0) {
        Gfx::DrawText(ScriptTile::BODY_SIZE, INNER_X, bodyY, COLOR_TEXT3, i18n::t("scripts.no_items").c_str());
        return;
    }

    const int rows = visibleRows();
    for (size_t i = 0; i < (size_t)rows && mScroll + i < mCount; i++) {
        const size_t k     = mScroll + i;
        const int rowY     = bodyY + (int)i * (ROW_H + ROW_GAP);
        const bool focused = k == mCursor;
        Shapes::fillRound(INNER_X, rowY, INNER_W, ROW_H, 8, focused ? COLOR_ACCENT_TINT : COLOR_FILL1);
        drawRow(k, rowY, focused);
        if (focused) {
            Shapes::focusRing(INNER_X, rowY, INNER_W, ROW_H, 8, COLOR_ACCENT);
        }
    }
}

void ScriptListOverlay::moveCursor(const InputState& input)
{
    if (mCount == 0) {
        return;
    }
    if (input.kDown & HidNpadButton_Up) {
        mCursor = mCursor > 0 ? mCursor - 1 : mCount - 1;
    }
    else if (input.kDown & HidNpadButton_Down) {
        mCursor = mCursor + 1 < mCount ? mCursor + 1 : 0;
    }

    const size_t rows = (size_t)visibleRows();
    if (mCursor < mScroll) {
        mScroll = mCursor;
    }
    else if (mCursor >= mScroll + rows) {
        mScroll = mCursor - rows + 1;
    }
}

/* ---- gui_pick_one ------------------------------------------------------- */

ScriptPickOneOverlay::ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items)
    : ScriptListOverlay(screen, prompt, items.size()), mItems(std::move(items))
{
}

void ScriptPickOneOverlay::drawRow(size_t k, int rowY, bool focused) const
{
    u32 th;
    Gfx::GetTextDimensions(ScriptTile::NOTE_SIZE, "Ag", NULL, &th);
    const int textX          = INNER_X + 14;
    const std::string fitted = trimToFit(mItems[k], INNER_X + INNER_W - 14 - textX, ScriptTile::NOTE_SIZE);
    Gfx::DrawText(ScriptTile::NOTE_SIZE, textX, rowY + (ROW_H - (int)th) / 2, focused ? COLOR_TEXT : COLOR_TEXT2, fitted.c_str());
}

std::string ScriptPickOneOverlay::hints(void) const
{
    if (mCount == 0) {
        return UiKit::buttonGlyph("B") + " " + i18n::t("common.cancel");
    }
    return UiKit::buttonGlyph("A") + " " + i18n::t("overlay.choose") + "   " + UiKit::buttonGlyph("B") + " " + i18n::t("common.cancel");
}

void ScriptPickOneOverlay::handleInput(const InputState& input)
{
    if (input.kDown & HidNpadButton_B) {
        UiResponse resp;
        resp.index = -1;
        answer(std::move(resp));
        return;
    }

    moveCursor(input);

    if ((input.kDown & HidNpadButton_A) && mCount > 0) {
        UiResponse resp;
        resp.index = (int)mCursor;
        answer(std::move(resp));
    }
}

/* ---- gui_pick_many ------------------------------------------------------ */

ScriptPickManyOverlay::ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected)
    : ScriptListOverlay(screen, prompt, items.size()), mItems(std::move(items)), mSelected(std::move(preselected))
{
    mSelected.resize(mItems.size(), false);
}

void ScriptPickManyOverlay::drawRow(size_t k, int rowY, bool focused) const
{
    // Checkbox: a small square, filled accent with a check when on.
    const int boxX = INNER_X + 14, boxY = rowY + (ROW_H - 22) / 2;
    if (mSelected[k]) {
        Shapes::fillRound(boxX, boxY, 22, 22, 4, COLOR_ACCENT);
        u32 gw, gh;
        Gfx::GetTextDimensions(14, "", &gw, &gh);
        Gfx::DrawText(14, boxX + (22 - (int)gw) / 2, boxY + (22 - (int)gh) / 2 + 2, COLOR_WHITE, "");
    }
    else {
        Shapes::strokeRound(boxX, boxY, 22, 22, 4, 2, COLOR_STROKE3);
    }

    u32 th;
    Gfx::GetTextDimensions(ScriptTile::NOTE_SIZE, "Ag", NULL, &th);
    const int textX          = boxX + 22 + 12;
    const std::string fitted = trimToFit(mItems[k], INNER_X + INNER_W - 14 - textX, ScriptTile::NOTE_SIZE);
    Gfx::DrawText(ScriptTile::NOTE_SIZE, textX, rowY + (ROW_H - (int)th) / 2, focused ? COLOR_TEXT : COLOR_TEXT2, fitted.c_str());
}

std::string ScriptPickManyOverlay::hints(void) const
{
    if (mCount == 0) {
        return UiKit::buttonGlyph("B") + " " + i18n::t("common.cancel");
    }
    return UiKit::buttonGlyph("A") + " " + i18n::t("scripts.toggle") + "   " + UiKit::buttonGlyph("X") + " " + i18n::t("hint.confirm") + "   " +
           UiKit::buttonGlyph("B") + " " + i18n::t("common.cancel");
}

void ScriptPickManyOverlay::handleInput(const InputState& input)
{
    if (input.kDown & HidNpadButton_B) {
        answer(UiResponse{}); // confirmed = false
        return;
    }

    moveCursor(input);

    if ((input.kDown & HidNpadButton_A) && mCount > 0) {
        mSelected[mCursor] = !mSelected[mCursor];
    }

    if (input.kDown & HidNpadButton_X) {
        UiResponse resp;
        resp.confirmed = true;
        resp.selected  = mSelected;
        answer(std::move(resp));
    }
}
