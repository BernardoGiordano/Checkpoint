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
#include "shapes.hpp"
#include "uikit.hpp"
#include <algorithm>

namespace {
    // Room the scrollbar track keeps clear on the log tile's right edge.
    constexpr int SCROLLBAR_W = 10;

    // Line pitch of the transcript, tighter than the face's own leading but
    // never so tight that 14px glyphs touch.
    int logRowHeight(void)
    {
        u32 h = 0;
        Gfx::GetTextDimensions(ScriptTile::LOG_SIZE, "0", NULL, &h, FontFamily::Mono);
        return std::max((int)h - ScriptTile::LOG_LINE_TIGHTEN, ScriptTile::LOG_SIZE + 2);
    }
}

int ScriptScreen::sScrollBack = 0;
int ScriptScreen::sRows       = 1;
bool ScriptScreen::sShowing   = false;

ScriptScreen::ScriptScreen(std::shared_ptr<Screen> previous, std::string scriptName) : mPrevious(std::move(previous)), mName(std::move(scriptName))
{
    sScrollBack = 0;
    sShowing    = true;
}

ScriptScreen::~ScriptScreen(void)
{
    sShowing = false;
}

bool ScriptScreen::showing(void)
{
    return sShowing;
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

void ScriptScreen::scrollLogLines(int lines)
{
    const int total   = (int)ScriptConsole::get().lineCount();
    const int maxBack = std::max(0, total - sRows);
    sScrollBack       = std::clamp(sScrollBack + lines, 0, maxBack);
}

void ScriptScreen::scrollLog(int pages)
{
    // A page keeps one row of context, the way a pager does.
    scrollLogLines(pages * std::max(1, sRows - 1));
}

void ScriptScreen::draw(void) const
{
    ScriptConsole& console = ScriptConsole::get();
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

    if (mFinished) {
        ScriptTile::hints(ScriptTile::LOG_X, ScriptTile::logBodyBottom(), ScriptTile::LOG_W,
            UiKit::buttonGlyph("A") + " " + i18n::t("scripts.close") + "   " + i18n::t("scripts.scroll_logs"));
    }
    else {
        const std::string hint = cancelling ? i18n::t("transfer.cancelling") : (UiKit::buttonGlyph("B") + " " + i18n::t("main.to_cancel"));
        ScriptTile::hints(ScriptTile::LOG_X, ScriptTile::logBodyBottom(), ScriptTile::LOG_W, hint + "   " + i18n::t("scripts.scroll_logs"));
    }

    // Scrollback marker, left-aligned on the hint row: the header's right edge
    // already belongs to the status note, and the hint row's right edge is now
    // the button hints.
    if (sScrollBack > 0) {
        const std::string back = "-" + std::to_string(sScrollBack);
        u32 bw, bh;
        Gfx::GetTextDimensions(ScriptTile::NOTE_SIZE, back.c_str(), &bw, &bh);
        (void)bw;
        Gfx::DrawText(
            ScriptTile::NOTE_SIZE, innerX, ScriptTile::logBodyBottom() + (ScriptTile::HINT_H - (int)bh) / 2, COLOR_ACCENT_LIGHT, back.c_str());
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
    sRows             = std::max(1, (logBottom - listTop) / rowH);

    const size_t total = console.lineCount();
    if (total == 0) {
        Gfx::DrawTextBox(ScriptTile::NOTE_SIZE, innerX, listTop, COLOR_TEXT3, innerW, i18n::t("scripts.log_empty").c_str());
        return;
    }

    // Pinned to the tail unless the user scrolled back, so a talking script
    // pushes the view along instead of scrolling out from under them.
    const size_t maxFirst = total > (size_t)sRows ? total - (size_t)sRows : 0;
    const size_t first    = maxFirst - std::min((size_t)sScrollBack, maxFirst);

    // Re-copy only when the content or the window actually moved: at 60 fps a
    // chatty script would otherwise cost hundreds of string copies a frame.
    const unsigned gen = console.generation();
    if (gen != mSeenGeneration || first != mSeenFirst || mVisible.size() != (size_t)sRows) {
        console.copyWindow(first, (size_t)sRows, mVisible);
        mSeenGeneration = gen;
        mSeenFirst      = first;
    }

    int y = listTop;
    for (const std::string& line : mVisible) {
        Gfx::DrawText(ScriptTile::LOG_SIZE, innerX, y, COLOR_TEXT2, line.c_str(), FontFamily::Mono);
        y += rowH;
    }

    if (total > (size_t)sRows) {
        const int trackY = listTop, trackH = logBottom - listTop;
        const int thumbH = std::max(24, (int)((float)trackH * (float)sRows / (float)total));
        const int thumbY = trackY + (int)((float)(trackH - thumbH) * (float)first / (float)maxFirst);
        const int barX   = ScriptTile::LOG_X + ScriptTile::LOG_W - ScriptTile::PAD + 6;
        Shapes::fillRound(barX, trackY, 3, trackH, 1, COLOR_FILL2);
        Shapes::fillRound(barX, thumbY, 3, thumbH, 1, COLOR_ACCENT);
    }
}

void ScriptScreen::update(const InputState& input)
{
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
        // The hold-B kill switch and the L/R log scrolling live in main.cpp:
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
