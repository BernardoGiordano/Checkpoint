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
#include "glyphs.hpp"
#include "gui.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include "scriptconsole.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>

namespace {
    constexpr float LOG_SIZE   = 1.0f; // one atlas texel per pixel; MONO_SIZE picks the size
    constexpr float TITLE_SIZE = 0.45f;
    constexpr float SMALL_SIZE = 0.42f;
    // Rows are packed tighter than the face's own line height: a transcript is
    // read in blocks, and the extra leading only costs rows.
    constexpr float LOG_LINE_TIGHTEN = 1.0f;
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
    const float advance = TextPool::get().monoAdvance(LOG_SIZE);
    // SCROLLBAR_W keeps the longest line clear of the scrollbar track.
    constexpr int SCROLLBAR_W = 8;
    const int usable          = ScriptTile::LOG_W - 2 * ScriptTile::LOG_PAD - SCROLLBAR_W;
    if (advance > 0.0f) {
        ScriptConsole::get().setWidth((size_t)(usable / advance));
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

void ScriptScreen::drawTop(void) const
{
    TextPool& text         = TextPool::get();
    ScriptConsole& console = ScriptConsole::get();

    // Every screen clears both targets and binds the top one before it draws:
    // main.cpp only binds g_bottom, between doDrawTop and doDrawBottom.
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_TargetClear(g_bottom, COLOR_BASE);
    C2D_SceneBegin(g_top);

    ScriptTile::card(ScriptTile::LOG_X, ScriptTile::LOG_Y, ScriptTile::LOG_W, ScriptTile::LOG_H, COLOR_LINE, 0.5f);

    const int innerX = ScriptTile::LOG_X + ScriptTile::LOG_PAD;
    const int innerW = ScriptTile::LOG_W - 2 * ScriptTile::LOG_PAD;

    // ---- header: what is running, and how far the pane is scrolled back ----
    text.draw(text.truncate(mName, innerW - 120, TITLE_SIZE), innerX, ScriptTile::LOG_Y + ScriptTile::LOG_PAD, TITLE_SIZE, COLOR_TEXT, 0.5f);
    {
        // The L/R hint lives here, on the screen no dialog covers, so the bottom
        // tile's hint line is free to describe only whatever is asking there.
        const std::string note = sScrollBack > 0 ? StringUtils::format("-%d", sScrollBack) : i18n::t("scripts.scroll_logs");
        const float w          = text.width(note, SMALL_SIZE);
        text.draw(
            note, innerX + innerW - w, ScriptTile::LOG_Y + ScriptTile::LOG_PAD + 1, SMALL_SIZE, sScrollBack > 0 ? COLOR_RING : COLOR_FAINT, 0.5f);
    }
    const int headerLineY = ScriptTile::LOG_Y + ScriptTile::LOG_HEADER_H;
    C2D_DrawRectSolid((float)innerX, (float)headerLineY, 0.5f, (float)innerW, 1.0f, COLOR_LINE);

    // ---- log rows: the whole rest of the screen ----------------------------
    // The progress bars live on the bottom tile, next to the status line the
    // user is already reading; this screen is nothing but transcript.
    const int logBottom = ScriptTile::LOG_Y + ScriptTile::LOG_H - ScriptTile::LOG_PAD;
    const float rowH    = std::max(1.0f, text.monoLineHeight(LOG_SIZE) - LOG_LINE_TIGHTEN);
    const int listTop   = headerLineY + 4;
    sRows               = std::max(1, (int)((logBottom - listTop) / rowH));

    const size_t total = console.lineCount();
    if (total == 0) {
        text.drawCentered(i18n::t("scripts.log_empty"), ScriptTile::LOG_X, ScriptTile::LOG_W, listTop + 20, SMALL_SIZE, COLOR_FAINT, 0.5f);
        return;
    }

    // Pinned to the tail unless the user scrolled back, so a talking script
    // pushes the view along instead of scrolling out from under them.
    const size_t maxFirst = total > (size_t)sRows ? total - (size_t)sRows : 0;
    const size_t back     = std::min((size_t)sScrollBack, maxFirst);
    const size_t first    = maxFirst - back;

    // Re-copy only when the content or the window actually moved: at 60 fps a
    // chatty script would otherwise cost a few hundred string copies a frame.
    const unsigned gen = console.generation();
    if (gen != mSeenGeneration || first != mSeenFirst || mVisible.size() != (size_t)sRows) {
        console.copyWindow(first, (size_t)sRows, mVisible);
        mSeenGeneration = gen;
        mSeenFirst      = first;
    }

    float y = (float)listTop;
    for (const std::string& line : mVisible) {
        text.drawMono(line, (float)innerX, y, LOG_SIZE, COLOR_MUTED, 0.5f);
        y += rowH;
    }

    // Scrollbar, only once there is more than one screenful.
    if (total > (size_t)sRows) {
        const float trackY = (float)listTop, trackH = (float)(logBottom - listTop);
        const float thumbH = std::max(12.0f, trackH * (float)sRows / (float)total);
        const float thumbY = trackY + (trackH - thumbH) * (float)first / (float)maxFirst;
        const float barX   = (float)(ScriptTile::LOG_X + ScriptTile::LOG_W - 5);
        C2D_DrawRectSolid(barX, trackY, 0.5f, 2.0f, trackH, COLOR_LINE);
        C2D_DrawRectSolid(barX, thumbY, 0.5f, 2.0f, thumbH, COLOR_ACCENT);
    }
}

void ScriptScreen::drawBottom(void) const
{
    TextPool& text = TextPool::get();

    const bool cancelling = ScriptRunner::get().cancelRequested();
    const std::string note =
        mFinished ? i18n::t("scripts.exit_code", {std::to_string(mOutcome.exitValue)}) : (cancelling ? i18n::t("transfer.cancelling") : "");
    const int bodyY = ScriptTile::uiFrame(mName, note, mFinished ? COLOR_ACCENT : COLOR_LINE, 0.5f);

    const int innerX = ScriptTile::UI_X + ScriptTile::PAD;
    const int innerW = ScriptTile::UI_W - 2 * ScriptTile::PAD;

    if (mFinished) {
        const std::string headline = mOutcome.cancelled        ? i18n::t("scripts.aborted", {mOutcome.scriptName})
                                     : mOutcome.exitValue == 0 ? i18n::t("scripts.success", {mOutcome.scriptName})
                                                               : i18n::t("scripts.failed", {mOutcome.scriptName});
        text.drawWrapped(headline, (float)innerX, (float)(bodyY + 10), 0.5f, COLOR_TEXT, (float)innerW, 0.5f);
        ScriptTile::uiHints(std::string(GLYPH_A) + " " + i18n::t("scripts.close"), 0.5f);
        return;
    }

    // A running script: its own status line, or a neutral "working" while it has
    // not set one.
    std::string status = ScriptRunner::get().bridge().statusText();
    if (status.empty()) {
        status = i18n::t("scripts.working");
    }
    text.drawWrapped(status, (float)innerX, (float)(bodyY + 10), 0.5f, COLOR_TEXT, (float)innerW, 0.5f);

    // Progress bars, stacked upwards from just above the hint line so a script
    // opening a second layer grows the block instead of shifting it.
    ScriptConsole::Layer bars[ScriptTile::MAX_BARS];
    const size_t barCount = ScriptTile::collectBars(ScriptConsole::get().progress(), bars);
    const int barsTop     = ScriptTile::uiBodyBottom() - 4 - (int)barCount * ScriptTile::BAR_ROW_H;
    for (size_t i = 0; i < barCount; i++) {
        ScriptTile::bar(innerX, barsTop + (int)i * ScriptTile::BAR_ROW_H, innerW, bars[i], 0.5f);
    }

    const std::string hint = cancelling ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint");
    ScriptTile::uiHints(hint, 0.5f);
}

void ScriptScreen::update(const InputState& input)
{
    (void)input;

    if (auto result = ScriptRunner::get().takeResult()) {
        // HOME was blocked for the whole run (picoc cannot be preempted).
        aptSetHomeAllowed(true);
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

    if (mFinished && (hidKeysDown() & (KEY_A | KEY_B | KEY_START))) {
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
