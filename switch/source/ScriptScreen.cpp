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

#include "ScriptScreen.hpp"
#include "KeyboardManager.hpp"
#include "ScriptRequestOverlays.hpp"
#include "ScriptTile.hpp"
#include "colors.hpp"
#include "gfx.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include "scriptconsole.hpp"
#include "scriptlogview.hpp"
#include "shapes.hpp"
#include "uikit.hpp"
#include <algorithm>

namespace {
    // Room the scrollbar track keeps clear on the log tile's right edge.
    constexpr int SCROLLBAR_W = 10;

    // Shared-font glyphs for the scroll hint: the up/down half of the D-Pad,
    // then L and R. Spelled out here rather than through UiKit::buttonGlyph
    // because these are the flat outline shapes that read as a row, not the
    // filled pills the button hints use.
    const std::string SCROLL_GLYPHS = "";

    // Line pitch of the transcript, tighter than the face's own leading but
    // never so tight that 14px glyphs touch.
    int logRowHeight(void)
    {
        u32 h = 0;
        Gfx::GetTextDimensions(ScriptTile::LOG_SIZE, "0", NULL, &h, FontFamily::Mono);
        return std::max((int)h - ScriptTile::LOG_LINE_TIGHTEN, ScriptTile::LOG_SIZE + 2);
    }
}

bool ScriptScreen::sShowing  = false;
bool ScriptScreen::sLogFocus = false;

ScriptScreen::ScriptScreen(std::shared_ptr<Screen> previous, std::string scriptName) : mPrevious(std::move(previous)), mName(std::move(scriptName))
{
    ScriptLogView::get().reset();
    sShowing  = true;
    sLogFocus = false;
}

ScriptScreen::~ScriptScreen(void)
{
    sShowing  = false;
    sLogFocus = false;
}

bool ScriptScreen::showing(void)
{
    return sShowing;
}

bool ScriptScreen::logFocused(void)
{
    return sLogFocus;
}

void ScriptScreen::toggleLogFocus(void)
{
    sLogFocus = !sLogFocus;
}

void ScriptScreen::configureLogWidth(void)
{
    u32 advance = 0;
    Gfx::GetTextDimensions(ScriptTile::LOG_SIZE, "0", &advance, NULL, FontFamily::Mono);
    const int usable = ScriptTile::LOG_W - 2 * ScriptTile::PAD - SCROLLBAR_W;
    // The tile is now the whole screen, so a row holds well over a hundred
    // columns; ScriptConsole clamps to its own sane maximum.
    if (advance > 0) {
        ScriptConsole::get().setWidth((size_t)(usable / (int)advance));
    }
}

void ScriptScreen::draw(void) const
{
    ScriptConsole& console = ScriptConsole::get();
    ScriptLogView& log     = ScriptLogView::get();
    Gfx::ClearScreen(COLOR_BG);

    /* ---- the one tile: the transcript, headed by the run's status -------- */
    // The status line is the header's note rather than a tile of its own: it is
    // one short string, and a half-screen tile that only ever showed the last
    // thing the log already says is a duplicate, not a pane.
    const bool cancelling  = ScriptRunner::get().cancelRequested();
    const std::string note = mFinished    ? (mOutcome.cancelled           ? i18n::t("scripts.aborted", {mOutcome.scriptName})
                                                : mOutcome.exitValue == 0 ? i18n::t("scripts.success", {mOutcome.scriptName})
                                                                          : i18n::t("scripts.failed", {mOutcome.scriptName}))
                             : cancelling ? i18n::t("transfer.cancelling")
                                          : [] {
                                                std::string status = ScriptRunner::get().bridge().statusText();
                                                return status.empty() ? i18n::t("scripts.working") : status;
                                            }();

    const int bodyY  = ScriptTile::frame(ScriptTile::LOG_X, ScriptTile::LOG_Y, ScriptTile::LOG_W, ScriptTile::LOG_H, mName, note);
    const int innerX = ScriptTile::LOG_X + ScriptTile::PAD;
    const int innerW = ScriptTile::LOG_W - 2 * ScriptTile::PAD;

    const std::string lead = mFinished    ? (UiKit::buttonGlyph("A") + " " + i18n::t("scripts.close"))
                             : cancelling ? i18n::t("transfer.cancelling")
                                          : (UiKit::buttonGlyph("B") + " " + i18n::t("main.to_cancel"));
    // The way back to a dialog the user pushed aside: while the log has focus
    // its card is not drawn at all, so this hint is the only thing on screen
    // saying the script is still waiting for an answer.
    const std::string peek  = (sLogFocus && currentOverlay) ? UiKit::buttonGlyph("Y") + " " + i18n::t("scripts.back_to_dialog") + "   " : "";
    const std::string hints = lead + "   " + peek + SCROLL_GLYPHS + " " + i18n::t("scripts.scroll_logs");
    ScriptTile::hints(ScriptTile::LOG_X, ScriptTile::logBodyBottom(), ScriptTile::LOG_W, hints);

    // Scrollback marker, immediately to the left of the hints rather than in
    // place of them or off at the row's far edge: the keys stay named while the
    // pane is off the tail, which is exactly when the user is looking for them.
    if (log.scrollBack() > 0) {
        const std::string back = "-" + std::to_string(log.scrollBack());
        u32 bw, bh, hw;
        Gfx::GetTextDimensions(ScriptTile::NOTE_SIZE, back.c_str(), &bw, &bh);
        Gfx::GetTextDimensions(ScriptTile::NOTE_SIZE, hints.c_str(), &hw, NULL);
        const int x = ScriptTile::LOG_X + ScriptTile::LOG_W - ScriptTile::PAD - (int)hw - (int)bw - 12;
        Gfx::DrawText(ScriptTile::NOTE_SIZE, x, ScriptTile::logBodyBottom() + (ScriptTile::HINT_H - (int)bh) / 2, COLOR_ACCENT_LIGHT, back.c_str());
    }

    /* ---- progress bars, stacked up from above the hint line -------------- */
    ScriptConsole::Layer bars[ScriptTile::MAX_BARS];
    const size_t barCount = ScriptTile::collectBars(console.progress(), bars);

    const int logBottom = ScriptTile::logBodyBottom() - (int)barCount * ScriptTile::BAR_ROW_H;
    for (size_t i = 0; i < barCount; i++) {
        ScriptTile::bar(innerX, logBottom + (int)i * ScriptTile::BAR_ROW_H + 8, innerW, bars[i]);
    }

    const int rowH    = logRowHeight();
    const int listTop = bodyY;
    log.setViewportRows((logBottom - listTop) / rowH);

    const ScriptLogView::Window win = log.window();
    if (win.total == 0) {
        Gfx::DrawTextBox(ScriptTile::NOTE_SIZE, innerX, listTop, COLOR_TEXT3, innerW, i18n::t("scripts.log_empty").c_str());
        return;
    }

    int y = listTop;
    for (const std::string& line : log.rows()) {
        Gfx::DrawText(ScriptTile::LOG_SIZE, innerX, y, COLOR_TEXT2, line.c_str(), FontFamily::Mono);
        y += rowH;
    }

    // The thumb is never shorter than a grabbable stub, whatever fraction of
    // the log fits on screen.
    if (win.scrollbar) {
        const int trackY = listTop, trackH = logBottom - listTop;
        const int thumbH = std::max(24, (int)((float)trackH * win.thumbSpan));
        const int thumbY = trackY + (int)((float)(trackH - thumbH) * win.thumbTravel);
        const int barX   = ScriptTile::LOG_X + ScriptTile::LOG_W - ScriptTile::PAD + 6;
        Shapes::fillRound(barX, trackY, 3, trackH, 1, COLOR_FILL2);
        Shapes::fillRound(barX, thumbY, 3, thumbH, 1, COLOR_ACCENT);
    }
}

void ScriptScreen::update(const InputState& input)
{
    // Reached only with no overlay up (Screen::doUpdate gives one the input),
    // so there is no pending request left to go back to: the next dialog opens
    // in front, not hidden behind a focus the user set two requests ago.
    sLogFocus = false;

    if (auto result = ScriptRunner::get().takeResult()) {
        // HOME was blocked for the whole run (picoc cannot be preempted).
        appletEndBlockingHomeButton();
        mOutcome  = *result;
        mFinished = true;
        // The last log line is the run's verdict, so the pane reads as a
        // transcript that ends where the script did.
        ScriptConsole::get().log(mOutcome.cancelled        ? i18n::t("scripts.aborted", {mOutcome.scriptName})
                                 : mOutcome.exitValue == 0 ? i18n::t("scripts.success", {mOutcome.scriptName})
                                                           : i18n::t("scripts.failed", {mOutcome.scriptName}));
    }

    if (ScriptRunner::get().active()) {
        // The hold-B kill switch and the D-Pad log scrolling live in main.cpp:
        // both must keep working while a script-raised overlay owns update().
        pumpScriptRequests(ScriptRunner::get().bridge(), *this);
        return;
    }

    if (mFinished && (input.kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))) {
        g_pendingScreen = mPrevious;
    }
}

void ScriptScreen::showMessage(const std::string& prompt)
{
    currentOverlay = std::make_shared<ScriptMessageOverlay>(*this, prompt);
}

void ScriptScreen::showConfirm(const std::string& prompt)
{
    currentOverlay = std::make_shared<ScriptConfirmOverlay>(*this, prompt);
}

void ScriptScreen::showPickOne(const std::string& prompt, const std::vector<std::string>& items)
{
    currentOverlay = std::make_shared<ScriptPickOneOverlay>(*this, prompt, items);
}

void ScriptScreen::showPickMany(const std::string& prompt, const std::vector<std::string>& items, const std::vector<bool>& preselected)
{
    currentOverlay = std::make_shared<ScriptPickManyOverlay>(*this, prompt, items, preselected);
}

std::string ScriptScreen::keyboard(const std::string& prompt, size_t maxChars)
{
    // swkbd runs on the main thread - the reason the bridge exists.
    return KeyboardManager::get().text("", prompt, maxChars);
}

int ScriptScreen::numpad(const std::string& prompt, int min, int max)
{
    return KeyboardManager::get().numpad(prompt, min, max);
}
