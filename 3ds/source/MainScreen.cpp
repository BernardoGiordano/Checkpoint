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

#include "MainScreen.hpp"
#include "KeyboardManager.hpp"
#include "ScriptPickerOverlay.hpp"
#include "ScriptRequestOverlays.hpp"
#include "SettingsScreen.hpp"
#include "backupsize.hpp"
#include "backuptarget.hpp"
#include "configuration.hpp"
#include "i18n.hpp"
#include "io.hpp"
#include "loader.hpp"
#include "outcomemessages.hpp"
#include "paths.hpp"
#include "progress.hpp"
#include "scriptrunner.hpp"
#include "server.hpp"
#include "textpool.hpp"
#include "transfer.hpp"
#include "transferjob.hpp"
#include "transferstatus.hpp"
#include <3ds.h>
#include <optional>

// ---- v4 main-page geometry ----------------------------------------------
// Top grid (400x240): 8 columns x 4 rows of native 48px SMDH tiles, 1px gap,
// centered. Full-size icons (no downscale) keep them crisp, matching the old
// main screen; the 1px gap gives the grid a little breathing room.
static constexpr size_t rowlen = 4, collen = 8;
static constexpr int HEADER_H = 24, FOOTER_H = 20;
static constexpr int TILE = 48, GAP = 1;
static constexpr int GRID_LEFT = (400 - (8 * TILE + 7 * GAP)) / 2; // = 4
static constexpr int GRID_TOP  = HEADER_H + 1;                     // = 25

namespace {
    std::optional<std::u16string> chooseBackupDst(const BackupTarget& target, size_t cellIndex)
    {
        if (cellIndex != 0) {
            return target.fullPath(cellIndex);
        }
        std::string suggestion = DateTime::dateTimeStr();
        // Quick backup (or multi-select) skips the swkbd prompt and takes the
        // timestamp directly
        const bool skipKeyboard = MS::multipleSelectionEnabled() || Configuration::getInstance().quickBackup();
        std::u16string name     = skipKeyboard ? StringUtils::UTF8toUTF16(suggestion.c_str()) : KeyboardManager::get().keyboard(suggestion);
        if (name.empty()) {
            return std::nullopt;
        }
        return target.rootPath() + StringUtils::UTF8toUTF16("/") + name;
    }

    // Draws a single button-glyph footer hint line, centered on `screenW`.
    void drawHints(int screenW, int y, const std::string& text)
    {
        TextPool::get().drawCentered(text, 0, screenW, y, 0.47f, COLOR_MUTED);
    }

    // Layout of the Save / Extdata segmented control in the bottom header. Both
    // drawBottom() and the touch hit-test derive from this so the box and the
    // labels can never drift apart. Cell widths follow the rendered label width.
    struct SegGeometry {
        int segX, segY, segH, pad, cellSaveW, cellExtW;
        int width(void) const { return cellSaveW + cellExtW; }
    };

    SegGeometry segmentGeometry(void)
    {
        TextPool& text = TextPool::get();
        SegGeometry g;
        g.segH      = 16;
        g.segY      = 4;
        g.pad       = 7;
        g.cellSaveW = (int)ceilf(text.width(i18n::t("main.tab_save"), 0.4f)) + g.pad * 2;
        g.cellExtW  = (int)ceilf(text.width(i18n::t("main.tab_extdata"), 0.4f)) + g.pad * 2;
        g.segX      = 320 - 6 - g.cellSaveW - g.cellExtW;
        return g;
    }
}

MainScreen::MainScreen(void) : hid(rowlen * collen, collen)
{
    selectionTimer  = 0;
    refreshTimer    = 0;
    transferEnabled = Configuration::getInstance().transferEnabled();

    // Detail action row: three 96px slots. Backup is the primary (accent),
    // Restore secondary, the middle slot contextual - Scripts by default, Send
    // on a highlighted backup.
    buttonBackupAL  = std::make_unique<Clickable>(8, 182, 96, 30, COLOR_ACCENT, COLOR_WHITE, i18n::t("main.backup_short"), true);
    buttonRestoreAL = std::make_unique<Clickable>(216, 182, 96, 30, COLOR_RAISED, COLOR_TEXT, i18n::t("main.restore_short"), true);
    // Full-width batch-backup button shown only while multi-selecting.
    buttonBackupAll = std::make_unique<Clickable>(8, 182, 304, 30, COLOR_ACCENT, COLOR_WHITE, i18n::t("main.backup_selected"), true);
    buttonSend      = std::make_unique<Clickable>(112, 182, 96, 30, COLOR_RAISED, COLOR_TEXT, i18n::t("transfer.send"), true);
    buttonScripts   = std::make_unique<Clickable>(112, 182, 96, 30, COLOR_RAISED, COLOR_TEXT, i18n::t("main.scripts"), true);
    directoryList   = std::make_unique<BackupList>(12, 70, 296, 106, 5);
    buttonBackupAll->canChangeColorWhenSelected(true);
    buttonBackupAL->canChangeColorWhenSelected(true);
    buttonRestoreAL->canChangeColorWhenSelected(true);
    buttonSend->canChangeColorWhenSelected(true);
    buttonScripts->canChangeColorWhenSelected(true);

    ver = StringUtils::versionString();

    C2D_PlainImageTint(&flagTint, COLOR_TEAL, 1.0f);
    C2D_PlainImageTint(&checkboxTint, COLOR_BLUE, 1.0f); // blue check on the white selection chip
    C2D_PlainImageTint(&starTint, COLOR_BLACK, 1.0f);    // black star on the gold favorite chip
}

int MainScreen::cellX(size_t i) const
{
    return GRID_LEFT + (int)((i % (rowlen * collen)) % collen) * (TILE + GAP);
}

int MainScreen::cellY(size_t i) const
{
    return GRID_TOP + (int)((i % (rowlen * collen)) / collen) * (TILE + GAP);
}

void MainScreen::drawSelector(void) const
{
    const int x = cellX(hid.index());
    const int y = cellY(hid.index());
    // No wash over the icon: the pulsing ring alone marks the selection, which
    // keeps bright icons from turning into a bright-on-bright smear.
    Gui::drawPulsingOutline(x, y, TILE, TILE, 2, COLOR_RING);
}

void MainScreen::drawTile(size_t k) const
{
    const int x = cellX(k);
    const int y = cellY(k);
    C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, COLOR_CARD);
    C2D_Image icon = TitleCatalog::get().icon(k, backupKind);
    if (icon.subtex->width == 48) {
        // Native 48px SMDH icon fills the 48px tile 1:1 — no scaling, no aliasing.
        C2D_DrawImageAt(icon, x, y, 0.5f, nullptr, 1.0f, 1.0f);
    }
    else {
        // Smaller icons (DS/other) sit centered, unscaled.
        const int off = (TILE - icon.subtex->width) / 2;
        C2D_DrawImageAt(icon, x + off, y + off, 0.5f, nullptr, 1.0f, 1.0f);
    }
}

void MainScreen::drawTop(void) const
{
    auto selEnt          = MS::selectedEntries();
    const bool multi     = MS::multipleSelectionEnabled();
    const size_t entries = hid.maxVisibleEntries();
    const size_t count   = TitleCatalog::get().getTitleCount(backupKind);
    const size_t max     = hid.maxEntries(count) + 1;

    TextPool& text = TextPool::get();
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_TargetClear(g_bottom, COLOR_BASE);
    C2D_SceneBegin(g_top);

    // Header bar. Slightly thinner than HEADER_H so the top screen reads lighter;
    // the tile grid stays put because GRID_TOP is fixed, not derived from this.
    static constexpr int TOP_HEADER_H = 22;
    C2D_DrawRectSolid(0, 0, 0.5f, 400, TOP_HEADER_H, COLOR_SURFACE);
    C2D_DrawRectSolid(0, TOP_HEADER_H, 0.5f, 400, 1, COLOR_LINE);
    // Brand mark + wordmark. Flag tinted with the Checkpoint primary (accent).
    C2D_ImageTint brandTint;
    C2D_PlainImageTint(&brandTint, COLOR_ACCENT, 1.0f);
    C2D_DrawImageAt(flag, 6, 3, 0.5f, &brandTint, 1.0f, 1.0f);
    float nameX = 6 + ceilf(flag.subtex->width * 1.0f) + 6;
    nameX += text.draw("Checkpoint", nameX, 4, 0.5f, COLOR_TEXT) + 6;
    text.draw(ver, nameX, 6, 0.4f, COLOR_FAINT);

    // Right cluster: time, count / multi-select badge.
    std::string timeStr = DateTime::timeStr();
    {
        float w = text.width(timeStr, 0.42f);
        text.draw(timeStr, 400 - 6 - w, 6, 0.42f, COLOR_FAINT);

        if (multi) {
            std::string badge = i18n::t("main.selected_badge", {std::to_string(selEnt.size())});
            float bw          = text.width(badge, 0.42f);
            float bx          = 400 - 6 - w - 8 - bw - 12;
            C2D_DrawRectSolid(bx - 6, 4, 0.5f, bw + 12, 16, COLOR_ACCENT);
            text.draw(badge, bx, 6, 0.42f, COLOR_WHITE);
        }
        else {
            std::string cnt = i18n::t("main.titles_count", {std::to_string(count)});
            float cw        = text.width(cnt, 0.42f);
            text.draw(cnt, 400 - 6 - w - 10 - cw, 6, 0.42f, COLOR_MUTED);
        }
    }

    LoadProgress loadProgress = TitleCatalog::get().progress();
    if (loadProgress.active) {
        int percentage = loadProgress.percent();
        if (percentage >= 100) {
            percentage = 99;
        }
        std::string msg = i18n::t("main.loading", {std::to_string(percentage)});
        text.drawCentered(msg, 0, 400, ceilf((240 - 0.6f * fontGetInfo(NULL)->lineFeed) / 2), 0.6f, COLOR_TEXT, 0.9f);
        return;
    }

    // Tiles.
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        drawTile(k);
    }

    // Multi-select veil + badges, favorite pips.
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        const int x = cellX(k), y = cellY(k);
        const bool checked = !selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end();

        if (multi && !checked && k != hid.fullIndex()) {
            C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, COLOR_DIM);
        }
        // Corner badges. The chip is 16px; the sprite art is 24px, so it is offset
        // by (16-24)/2 = -4 on both axes to sit centered on the chip. The favorite
        // star is drawn first so the multi-select check lands on top of it — when a
        // title is both, the selection state must stay visible.
        constexpr int CHIP = 16, SPR = 24, SPR_OFF = (CHIP - SPR) / 2;
        const int cx = x + TILE - CHIP - 1, cy = y + 1;
        if (k < gridFavorites.size() && gridFavorites[k]) {
            C2D_DrawRectSolid(cx, cy, 0.5f, CHIP, CHIP, COLOR_GOLD);
            C2D_SpriteSetPos(&star, cx + SPR_OFF, cy + SPR_OFF);
            C2D_DrawSpriteTinted(&star, &starTint);
        }
        if (checked) {
            C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, C2D_Color32(122, 66, 196, 90));
            C2D_DrawRectSolid(cx, cy, 0.5f, CHIP, CHIP, COLOR_WHITE);
            C2D_SpriteSetPos(&checkbox, cx + SPR_OFF, cy + SPR_OFF);
            C2D_DrawSpriteTinted(&checkbox, &checkboxTint);
        }
    }

    // Breathing selector drawn last so its ring sits above every veil and badge.
    if (count > 0) {
        drawSelector();
    }

    // Footer hint bar. Slightly thinner than FOOTER_H; top nudged down so it clears
    // the tile grid, which ends at y=220.
    static constexpr int TOP_FOOTER_TOP = 222;
    C2D_DrawRectSolid(0, TOP_FOOTER_TOP, 0.5f, 400, 240 - TOP_FOOTER_TOP, COLOR_SURFACE);
    C2D_DrawRectSolid(0, TOP_FOOTER_TOP - 1, 0.5f, 400, 1, COLOR_LINE);
    if (ScriptRunner::get().active()) {
        // Script status strip: name + last gui_status text from the worker,
        // plus the hold-B abort hint.
        std::string line = i18n::t("scripts.running", {ScriptRunner::get().scriptName()});
        if (ScriptRunner::get().cancelRequested()) {
            line += "  -  " + i18n::t("transfer.cancelling");
        }
        else {
            std::string status = ScriptRunner::get().bridge().statusText();
            if (!status.empty()) {
                line += "  -  " + status;
            }
            line += "  -  " + i18n::t("transfer.cancel_hint");
        }
        drawHints(400, 224, line);
    }
    else if (multi) {
        drawHints(400, 224, " Tag     hold all     Backup all     Clear");
    }
    else {
        drawHints(400, 224, " Open     Tag     Extdata    SELECT Settings");
    }

    // Live transfer status (network sends draw their own modal on the bottom).
    const TransferSnapshot& ts = mTransfer;
    if (ts.active && ts.kind == TransferKind::Network) {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
        u64 total = ts.bytesTotal, done = ts.bytesDone;
        int pct            = total > 0 ? (int)((done * 100) / total) : 0;
        float frac         = total > 0 ? (float)done / (float)total : 0.0f;
        std::string prefix = ts.mode.empty() ? i18n::t("main.transferring") : ts.mode;

        const int mw = 260, mh = 120;
        const int mx = (400 - mw) / 2, my = (240 - mh) / 2;
        C2D_DrawRectSolid(mx, my, 0.5f, mw, mh, COLOR_CARD);
        Gui::drawOutline(mx, my, mw, mh, 2, COLOR_ACCENT);

        text.drawCentered(i18n::t("main.in_progress", {prefix}), mx, mw, my + 12, 0.55f, COLOR_TEXT);

        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
        Gui::drawProgressBar(mx + 12, my + 52, mw - 24, 10, frac, TransferStatus::bytesToMB(done, total), pctStr);

        std::string hint = TransferStatus::cancelRequested() ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint");
        text.drawCentered(hint, mx, mw, my + mh - 24, 0.5f, COLOR_FAINT);
    }
}

void MainScreen::drawBottom(void) const
{
    TextPool& text = TextPool::get();
    C2D_SceneBegin(g_bottom);

    if (selected.valid) {
        // Header: title name, Save/Extdata segmented control.
        C2D_DrawRectSolid(0, 0, 0.5f, 320, HEADER_H, COLOR_SURFACE);
        C2D_DrawRectSolid(0, HEADER_H, 0.5f, 320, 1, COLOR_LINE);

        // Segmented Save / Extdata toggle (right-aligned in the header) — laid out
        // first so the title name below knows how much width it can use.
        const SegGeometry seg = segmentGeometry();
        {
            C2D_DrawRectSolid(seg.segX, seg.segY, 0.5f, seg.width(), seg.segH, COLOR_RAISED);
            const bool onSave = backupKind == BackupKind::Save;
            C2D_DrawRectSolid(
                onSave ? seg.segX : seg.segX + seg.cellSaveW, seg.segY, 0.5f, onSave ? seg.cellSaveW : seg.cellExtW, seg.segH, COLOR_ACCENT);
            text.draw(i18n::t("main.tab_save"), seg.segX + seg.pad, seg.segY + 2, 0.4f, onSave ? COLOR_WHITE : COLOR_MUTED);
            text.draw(i18n::t("main.tab_extdata"), seg.segX + seg.cellSaveW + seg.pad, seg.segY + 2, 0.4f, onSave ? COLOR_MUTED : COLOR_WHITE);
        }

        std::string name = text.truncate(selected.name, seg.segX - 8 - 8, 0.5f);
        text.draw(name, 8, 4, 0.5f, COLOR_TEXT);

        // Thin info line: cart identifier, media type, favorite.
        {
            float x         = 8;
            const float y   = 28;
            const float sep = 6;
            x += text.draw(selected.cartId, x, y, 0.42f, COLOR_MUTED) + sep;
            x += text.draw("·  " + selected.mediaType, x, y, 0.42f, COLOR_MUTED) + sep;
            if (selected.favorite) {
                text.draw("·  ★ " + i18n::t("main.favorite"), x, y, 0.42f, COLOR_GOLD);
            }
        }

        // Backups card. Rows (entry 0 "New backup", then existing backups with
        // their async sizes) were rebuilt by refreshSelected() when they changed.
        C2D_DrawRectSolid(8, 46, 0.5f, 304, 132, COLOR_CARD);
        C2D_DrawRectSolid(8, 67, 0.5f, 304, 1, COLOR_LINE);
        text.draw(i18n::t("main.backups"), 16, 49, 0.45f, COLOR_MUTED);
        {
            // Total: shown once the worker has resolved it; until then a "…" placeholder.
            std::string meta = selected.backupCount == 0 ? i18n::t("main.no_backups")
                               : selected.totalSize      ? i18n::t("main.backups_meta",
                                                               {std::to_string(selected.backupCount), StringUtils::humanBytes(*selected.totalSize)})
                                                         : i18n::t("main.backups_meta_pending", {std::to_string(selected.backupCount)});
            float w          = text.width(meta, 0.42f);
            text.draw(meta, 312 - 8 - w, 50, 0.42f, COLOR_FAINT);
        }
        directoryList->draw(g_bottomScrollEnabled);

        // Actions. While multi-selecting, one full-width Backup button replaces the
        // per-title Backup/Restore pair and drives the whole tagged batch.
        if (MS::multipleSelectionEnabled()) {
            buttonBackupAll->text(i18n::t("main.backup_n_selected", {std::to_string(MS::selectedEntries().size())}) + " ");
            buttonBackupAll->draw(0.6f, COLOR_RING);
        }
        // A highlighted existing backup (not an action row) gets a Backup / Send /
        // Restore trio so Send is a one-touch, contextual action.
        else if (transferEnabled && g_bottomScrollEnabled && directoryList->index() > 0 && !isReceiveRow(directoryList->index())) {
            buttonBackupAL->text(i18n::t("main.backup_short"));
            buttonSend->text(i18n::t("transfer.send"));
            buttonRestoreAL->text(i18n::t("main.restore_short"));
            buttonBackupAL->draw(0.6f, COLOR_RING);
            buttonSend->draw(0.6f, COLOR_ACCENT);
            buttonRestoreAL->draw(0.6f, COLOR_RING);
        }
        else {
            buttonBackupAL->text(i18n::t("main.backup_short"));
            buttonScripts->text(i18n::t("main.scripts"));
            buttonRestoreAL->text(i18n::t("main.restore_short"));
            buttonBackupAL->draw(0.6f, COLOR_RING);
            buttonScripts->draw(0.6f, COLOR_ACCENT);
            buttonRestoreAL->draw(0.6f, COLOR_RING);
        }
    }

    // Subtle scrim while no title is opened: the top grid holds focus, so the
    // detail panel reads as inactive until the user drills in (A / Go to saves).
    if (!g_bottomScrollEnabled) {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_SCRIM);
    }

    // Footer hint bar.
    C2D_DrawRectSolid(0, 220, 0.5f, 320, FOOTER_H, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 320, 1, COLOR_LINE);
    drawHints(320, 223,
        MS::multipleSelectionEnabled() ? i18n::t("main.backup_selected") + "      " + i18n::t("main.clear_selection")
        : g_bottomScrollEnabled        ? " " + i18n::t("hint.confirm") + "      " + i18n::t("hint.delete") + "      " + i18n::t("hint.back")
                                       : " " + i18n::t("main.hint.go_saves"));

    // Live local-copy progress modal (network sends draw on the top screen).
    const TransferSnapshot& ts = mTransfer;
    if (ts.active && ts.kind != TransferKind::Network) {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);

        const bool multiSelect = ts.saveTotal > 1;
        const int mx = 30, mw = 260;
        const int mh = multiSelect ? 162 : 130;
        const int my = multiSelect ? 40 : 65;
        C2D_DrawRectSolid(mx, my, 0.5f, mw, mh, COLOR_CARD);
        Gui::drawOutline(mx, my, mw, mh, 2, COLOR_ACCENT);

        std::string titleStr = i18n::t("main.in_progress", {ts.mode.empty() ? i18n::t("main.copying") : ts.mode});
        text.drawCentered(titleStr, mx, mw, my + 10, 0.55f, COLOR_TEXT);

        std::string fname = StringUtils::UTF16toUTF8(ts.currentFile);
        text.drawCentered(fname, mx, mw, my + 30, 0.5f, COLOR_FAINT);

        const int barX = mx + 12, barW = mw - 24, barH = 10;
        auto drawProgressBar = [&](int y, float frac, const char* leftLabel, const char* rightLabel) {
            Gui::drawProgressBar(barX, y, barW, barH, frac, leftLabel, rightLabel);
        };

        int barY = my + 52;
        if (multiSelect) {
            float overallProgress       = (float)ts.saveCount / (float)ts.saveTotal;
            std::string overallCountStr = i18n::t("main.save_n", {std::to_string(ts.saveCount + 1), std::to_string(ts.saveTotal)});
            char overallPctStr[8];
            snprintf(overallPctStr, sizeof(overallPctStr), "%d%%", (int)(overallProgress * 100));
            drawProgressBar(barY, overallProgress, overallCountStr.c_str(), overallPctStr);
            barY += 30;
        }
        float progress       = (ts.copyTotal > 0) ? (float)ts.copyCount / (float)ts.copyTotal : 0.0f;
        std::string countStr = i18n::t("main.file_n", {std::to_string(ts.copyCount), std::to_string(ts.copyTotal)});
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%", (int)((progress > 1.0f ? 1.0f : progress) * 100));
        drawProgressBar(barY, progress, countStr.c_str(), pctStr);
        barY += 30;
        float fileProgress = (ts.currentFileSize > 0) ? (float)ts.currentFileOffset / (float)ts.currentFileSize : 0.0f;
        char kbStr[32];
        snprintf(kbStr, sizeof(kbStr), "%.1f / %.1f KB", ts.currentFileOffset / 1024.0f, ts.currentFileSize / 1024.0f);
        char filePctStr[8];
        snprintf(filePctStr, sizeof(filePctStr), "%d%%", (int)((fileProgress > 1.0f ? 1.0f : fileProgress) * 100));
        drawProgressBar(barY, fileProgress, kbStr, filePctStr);

        // Hold-B-to-cancel is offered for a backup only (see update()).
        if (ts.mode == "Backup") {
            std::string hint = TransferStatus::cancelRequested() ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint");
            text.drawCentered(hint, mx, mw, my + mh - 17, 0.4f, COLOR_FAINT);
        }
    }
}

void MainScreen::update(const InputState& input)
{
    // Snapshot the live transfer state once for this frame; both draw*() read the
    // member instead of re-locking TransferStatus. Done before any early return
    // so the modal keeps updating even while a TransferJob is active.
    mTransfer = TransferStatus::snapshot();

    // Hold-B-to-cancel for the transfer modal. Must run before the TransferJob
    // early returns below: while a transfer is in flight update() bails out right
    // after the result poll, so this is the only input the modal gets. Cancel is
    // offered for a network send and for a local backup (mode == "Backup"), never
    // for a restore — a half-written restore must never be left on the cartridge.
    const bool cancellable = mTransfer.active && (mTransfer.kind == TransferKind::Network || mTransfer.mode == "Backup");
    if (cancellable) {
        if (hidKeysHeld() & KEY_B) {
            if (++mCancelHoldFrames >= 45 && !TransferStatus::cancelRequested()) {
                TransferStatus::requestCancel();
            }
        }
        else {
            mCancelHoldFrames = 0;
        }
    }
    else {
        mCancelHoldFrames = 0;
    }

    // Re-read in case the Settings page toggled it while this screen was parked.
    transferEnabled = Configuration::getInstance().transferEnabled();

    if (auto result = TransferJob::get().takeResult()) {
        // A backup/restore changed one or more folders; drop every cached total so
        // they get re-walked off-thread (a batch may have touched many titles).
        BackupSizeCache::get().invalidateAll();
        if (result->cancelled) {
            // User-requested stop of a backup: neutral info, not an error.
            currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("transfer.cancelled"));
        }
        else if (result->ok) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, result->successMsg);
        }
        else if (result->send) {
            if (result->send->stage == Transfer::SendStage::EmptyBackup) {
                currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.backup_empty"));
            }
            else if (result->send->stage == Transfer::SendStage::Cancelled) {
                // User-requested stop: neutral info, not an error.
                currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("transfer.cancelled"));
            }
            else {
                currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, OutcomeMessages::sendError(*result->send));
            }
        }
        else {
            std::string message = result->isRestore ? OutcomeMessages::restoreError(result->stage, result->dataType)
                                                    : OutcomeMessages::backupError(result->stage, result->dataType);
            currentOverlay      = std::make_shared<ErrorOverlay>(*this, result->res, message);
        }
        return;
    }

    if (TransferJob::get().active()) {
        return;
    }

    // Script outcome / activity. While a script runs this screen only pumps its
    // UI-bridge requests; normal input is ignored (same policy as TransferJob).
    if (auto script = ScriptRunner::get().takeResult()) {
        aptSetHomeAllowed(true);
        if (script->cancelled) {
            // User-requested abort: neutral info, not an error.
            currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("scripts.aborted", {script->scriptName}));
        }
        else if (script->exitValue == 0) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("scripts.success", {script->scriptName}));
        }
        else {
            // The output tail carries picoc's diagnostic; the card fits a few lines.
            std::string detail = script->output.size() > 120 ? script->output.substr(script->output.size() - 120) : script->output;
            currentOverlay     = std::make_shared<ErrorOverlay>(
                *this, script->exitValue, i18n::t("scripts.failed", {script->scriptName}) + (detail.empty() ? "" : "\n" + detail));
        }
        return;
    }
    if (ScriptRunner::get().active()) {
        // The hold-B kill switch lives in main.cpp (it must keep counting
        // while a script-raised overlay replaces this update); here we only
        // pump the bridge.
        pumpScriptRequests();
        return;
    }

    if (Transfer::consumePendingRefresh() || g_titlesDirty) {
        g_titlesDirty = false;
        refreshTitlesFull();
    }
    updateSelector();
    handleEvents(input);

    // Refresh the snapshot last so the frame drawn right after already shows the
    // selection/kind changes the handlers above made.
    refreshSelected();

    // Kick off (or no-op) an off-thread size walk for the selected title; the
    // cache coalesces repeats and refreshSelected() picks the total up through
    // the generation bump once the worker lands it.
    if (selected.valid) {
        BackupSizeCache::get().request(selected.id, backupKind, selected.rootPath);
    }
}

void MainScreen::refreshSelected(void)
{
    TitleCatalog& catalog = TitleCatalog::get();

    // While the catalog reloads, show no detail card; the generation bump at the
    // end of the load triggers the rebuild.
    if (catalog.progress().active) {
        if (selected.valid) {
            selected.valid = false;
            directoryList->clear();
        }
        return;
    }

    const u32 catalogGen = catalog.generation();
    const u32 sizeGen    = BackupSizeCache::get().generation();
    const size_t count   = (size_t)catalog.getTitleCount(backupKind);

    if (selected.valid == (count > 0) && selected.fullIndex == hid.fullIndex() && selected.kind == backupKind && selected.catalogGen == catalogGen &&
        selected.sizeGen == sizeGen && selected.transferRow == transferEnabled) {
        return; // snapshot still describes what is on screen
    }

    if (!selected.valid || selected.kind != backupKind || selected.catalogGen != catalogGen) {
        gridFavorites.assign(count, 0);
        for (size_t i = 0; i < count; i++) {
            gridFavorites[i] = catalog.favorite(i, backupKind) ? 1 : 0;
        }
    }

    selected.valid       = count > 0;
    selected.fullIndex   = hid.fullIndex();
    selected.kind        = backupKind;
    selected.catalogGen  = catalogGen;
    selected.sizeGen     = sizeGen;
    selected.transferRow = transferEnabled;

    if (!selected.valid) {
        directoryList->clear();
        return;
    }

    Title title;
    catalog.getTitle(title, selected.fullIndex, backupKind);
    BackupTarget target = title.backup(backupKind);

    selected.id        = title.id();
    selected.rootPath  = target.rootPath();
    selected.name      = title.shortDescription();
    selected.cartId    = title.productCode[0] != '\0' ? std::string(title.productCode) : i18n::t("main.system_title");
    selected.mediaType = title.mediaTypeString();
    selected.favorite  = catalog.favorite(selected.fullIndex, backupKind);
    selected.totalSize = BackupSizeCache::get().total(selected.id, backupKind);

    // Rows: entry 0 is the "New backup" affordance, entry 1 the "Receive" action
    // (while the transfer feature is enabled), the rest are existing backups
    // labelled with their (async) size. rowToCell()/cellToRow() depend on this
    // layout.
    std::vector<std::u16string> dirs = target.backups();
    directoryList->clear();
    for (size_t i = 0; i < dirs.size(); i++) {
        if (i == 0) {
            directoryList->push_back(i18n::t("main.new_backup"),
                backupKind == BackupKind::Save ? i18n::t("main.from_current_save") : i18n::t("main.from_current_extdata"), BackupList::RowKind::New);
            if (transferEnabled) {
                directoryList->push_back(i18n::t("transfer.receive"), i18n::t("main.from_wireless"), BackupList::RowKind::Receive);
            }
        }
        else {
            std::optional<u64> bs = BackupSizeCache::get().backupSize(target.fullPath(i));
            directoryList->push_back(StringUtils::UTF16toUTF8(dirs.at(i)), bs.has_value() ? StringUtils::humanBytes(*bs) : std::string("…"),
                BackupList::RowKind::Existing);
        }
    }
    selected.backupCount = dirs.empty() ? 0 : dirs.size() - 1; // entry 0 is "New..."
}

void MainScreen::refreshTitlesFull(void)
{
    hid.reset();
    MS::clearSelectedEntries();
    directoryList->resetIndex();
    Threads::executeTask(TitleCatalog::loadTitlesThread);
    refreshTimer = 0;
}

void MainScreen::updateSelector(void)
{
    if (TitleCatalog::get().progress().active) {
        return;
    }

    if (!g_bottomScrollEnabled) {
        size_t count = TitleCatalog::get().getTitleCount(backupKind);
        if (count > 0) {
            hid.update(count);
            directoryList->resetIndex();
        }
    }
    else {
        directoryList->update();
    }
}

void MainScreen::doBackup(size_t fullIndex, size_t cellIndex)
{
    Title title;
    TitleCatalog::get().getTitle(title, fullIndex, backupKind);
    BackupTarget target = title.backup(backupKind);

    auto dst = chooseBackupDst(target, cellIndex);
    removeOverlay();
    if (!dst) {
        return;
    }

    std::string dataType = target.dataTypeName();
    TransferJob::get().enqueueBackup(std::move(title), backupKind, *dst, std::move(dataType));
}

void MainScreen::doRestore(size_t fullIndex, size_t cellIndex)
{
    Title title;
    TitleCatalog::get().getTitle(title, fullIndex, backupKind);
    BackupTarget target = title.backup(backupKind);

    std::u16string src     = target.fullPath(cellIndex);
    std::string dataType   = target.dataTypeName();
    std::string successMsg = i18n::t("outcome.restore_success", {nameFromCell(cellIndex)});
    removeOverlay();

    TransferJob::get().enqueueRestore(std::move(title), backupKind, std::move(src), std::move(dataType), std::move(successMsg));
}

void MainScreen::requestRestore(size_t cellIndex)
{
    auto run = [this, cellIndex]() {
        this->doRestore(hid.fullIndex(), cellIndex);
        TransferJob::get().start();
    };
    if (Configuration::getInstance().confirmRestore()) {
        currentOverlay = std::make_shared<YesNoOverlay>(*this, i18n::t("main.confirm_restore"), run, [this]() { this->removeOverlay(); });
    }
    else {
        run();
    }
}

void MainScreen::startScriptPicker(void)
{
    if (TitleCatalog::get().progress().active || TransferJob::get().active() || ScriptRunner::get().active()) {
        return;
    }

    const bool hasTitle = selected.valid;
    const u64 titleId   = selected.id;
    // Bundled (romfs) scripts first, then SD: an SD file of the same name wins.
    std::vector<ScriptCatalog::Root> universalRoots = {
        {Paths::bundledUniversalScriptsDir(), ScriptCatalog::Source::Bundled},
        {Paths::universalScriptsDir(), ScriptCatalog::Source::Sd},
    };
    std::vector<ScriptCatalog::Root> specificRoots;
    if (hasTitle) {
        specificRoots = {
            {Paths::bundledScriptsDirFor(titleId), ScriptCatalog::Source::Bundled},
            {Paths::scriptsDirFor(titleId), ScriptCatalog::Source::Sd},
        };
    }
    auto entries   = ScriptCatalog::scan(universalRoots, specificRoots);
    currentOverlay = std::make_shared<ScriptPickerOverlay>(
        *this, std::move(entries), hasTitle ? selected.name : "", [this, hasTitle, titleId](const ScriptCatalog::Entry& entry) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, i18n::t("scripts.confirm_run", {entry.name}),
                [this, entry, hasTitle, titleId]() {
                    this->removeOverlay();
                    std::string idHex = hasTitle ? StringUtils::format("%016llX", titleId) : "";
                    // HOME stays blocked for the whole run (PKSM precedent): picoc
                    // cannot be preempted, so a buggy script requires a reboot.
                    aptSetHomeAllowed(false);
                    if (!ScriptRunner::get().start(entry.path, entry.name, std::move(idHex))) {
                        aptSetHomeAllowed(true);
                        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, i18n::t("scripts.start_failed"));
                    }
                },
                [this]() { this->removeOverlay(); });
        });
}

void MainScreen::pumpScriptRequests(void)
{
    ScriptUiBridge& bridge = ScriptRunner::get().bridge();
    const UiRequest* req   = bridge.pending();
    if (!req) {
        return;
    }

    // This runs only while no overlay is up (an overlay's update replaces the
    // screen's), so each pending request is mapped to its overlay exactly once;
    // the overlay answers the bridge before dismissing itself.
    switch (req->kind) {
        case UiRequest::Kind::Message:
            currentOverlay = std::make_shared<ScriptMessageOverlay>(*this, req->prompt);
            break;
        case UiRequest::Kind::Confirm:
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, req->prompt,
                [this]() {
                    UiResponse resp;
                    resp.confirmed = true;
                    ScriptRunner::get().bridge().respond(std::move(resp));
                    this->removeOverlay();
                },
                [this]() {
                    ScriptRunner::get().bridge().respond(UiResponse{});
                    this->removeOverlay();
                });
            break;
        case UiRequest::Kind::PickOne:
            currentOverlay = std::make_shared<ScriptPickOneOverlay>(*this, req->prompt, req->items);
            break;
        case UiRequest::Kind::PickMany:
            currentOverlay = std::make_shared<ScriptPickManyOverlay>(*this, req->prompt, req->items, req->preselected);
            break;
        case UiRequest::Kind::Keyboard: {
            // swkbd runs on the main thread - the reason the bridge exists.
            UiResponse resp;
            resp.text = KeyboardManager::get().text("", req->prompt, req->maxChars > 0 ? (size_t)req->maxChars - 1 : 0);
            bridge.respond(std::move(resp));
            break;
        }
        case UiRequest::Kind::Numpad: {
            UiResponse resp;
            resp.index = KeyboardManager::get().numpad(req->prompt, req->numMin, req->numMax);
            bridge.respond(std::move(resp));
            break;
        }
    }
}

void MainScreen::handleEvents(const InputState& input)
{
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    // SELECT opens Settings, but not while the catalog is still loading titles.
    if ((kDown & KEY_SELECT) && !TitleCatalog::get().progress().active) {
        g_pendingScreen = std::make_shared<SettingsScreen>(g_screen);
        return;
    }

    // Touch the Save / Extdata segmented control in the bottom header to switch
    // kinds. Geometry mirrors drawBottom() exactly so the hit box tracks the label.
    if (selected.valid && (kDown & KEY_TOUCH)) {
        const SegGeometry seg = segmentGeometry();
        if (input.py >= seg.segY && input.py < seg.segY + seg.segH && input.px >= seg.segX && input.px < seg.segX + seg.width()) {
            BackupKind want = input.px < seg.segX + seg.cellSaveW ? BackupKind::Save : BackupKind::Extdata;
            if (want != backupKind) {
                hid.reset();
                backupKind            = want;
                g_bottomScrollEnabled = false;
                MS::clearSelectedEntries();
                directoryList->resetIndex();
                updateButtons();
            }
            return;
        }
    }

    if (kDown & KEY_A) {
        if (g_bottomScrollEnabled) {
            if (0 == directoryList->index()) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, i18n::t("main.confirm_backup_title"),
                    [this]() {
                        this->doBackup(hid.fullIndex(), 0);
                        TransferJob::get().start();
                    },
                    [this]() { this->removeOverlay(); });
            }
            else if (isReceiveRow(directoryList->index())) {
                startTransferReceive();
            }
            else {
                requestRestore(rowToCell(directoryList->index()));
            }
        }
        else {
            if (!MS::multipleSelectionEnabled()) {
                g_bottomScrollEnabled = true;
                updateButtons();
            }
        }
    }

    if (kDown & KEY_B) {
        g_bottomScrollEnabled = false;
        MS::clearSelectedEntries();
        directoryList->resetIndex();
        updateButtons();
    }

    if (kDown & KEY_X) {
        if (g_bottomScrollEnabled) {
            size_t index = directoryList->index();
            if (index > 0 && !isReceiveRow(index)) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, i18n::t("main.confirm_delete"),
                    [this, index]() {
                        Title title;
                        TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);
                        std::u16string path = title.backup(backupKind).fullPath(rowToCell(index));
                        io::deleteBackupFolder(path);
                        TitleCatalog::get().refreshDirectories(title.id());
                        directoryList->setIndex(index - 1);
                        // Folder shrank; drop its cached size so it is re-walked off-thread.
                        BackupSizeCache::get().invalidate(title.id(), backupKind, title.backup(backupKind).rootPath());
                        this->removeOverlay();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }
        else {
            hid.reset();
            backupKind = backupKind == BackupKind::Save ? BackupKind::Extdata : BackupKind::Save;
            MS::clearSelectedEntries();
            directoryList->resetIndex();
        }
    }

    if (kDown & KEY_Y) {
        if (g_bottomScrollEnabled) {
            directoryList->resetIndex();
            g_bottomScrollEnabled = false;
        }
        MS::addSelectedEntry(hid.fullIndex());
        updateButtons();
    }

    if (kHeld & KEY_Y) {
        selectionTimer++;
    }
    else {
        selectionTimer = 0;
    }

    if (selectionTimer > 90) {
        MS::clearSelectedEntries();
        for (size_t i = 0, sz = TitleCatalog::get().getTitleCount(backupKind); i < sz; i++) {
            MS::addSelectedEntry(i);
        }
        selectionTimer = 0;
    }

    if (kHeld & KEY_B) {
        refreshTimer++;
    }
    else {
        refreshTimer = 0;
    }

    if (refreshTimer > 90) {
        refreshTitlesFull();
    }

    // From the snapshot the current frame was drawn from, so input maps to the
    // buttons the user actually sees.
    // A highlighted existing backup shows the Backup / Send / Restore trio; Send
    // goes straight to startTransferSend() (which validates and prompts).
    const bool sendContext =
        transferEnabled && selected.valid && g_bottomScrollEnabled && directoryList->index() > 0 && !isReceiveRow(directoryList->index());

    if (sendContext && buttonSend->released()) {
        startTransferSend();
        return;
    }

    // The default action row shows the Scripts middle button (everywhere except
    // multi-select and the send context).
    const bool scriptsShown = !MS::multipleSelectionEnabled() && !sendContext;
    if (scriptsShown && buttonScripts->released()) {
        startScriptPicker();
        return;
    }

    if (MS::multipleSelectionEnabled()) {
        // One large Backup button (touch or A) backs up the whole tagged batch;
        // it replaces the per-title Backup/Restore pair while multi-selecting.
        if (buttonBackupAll->released() || (kDown & KEY_A) || (kDown & KEY_L)) {
            directoryList->resetIndex();
            std::vector<size_t> list = MS::selectedEntries();
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                doBackup(list.at(i), directoryList->index());
            }
            TransferJob::get().start();
            MS::clearSelectedEntries();
            updateButtons();
        }
    }
    else {
        if (buttonBackupAL->released() || (kDown & KEY_L)) {
            if (g_bottomScrollEnabled) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, i18n::t("main.confirm_backup_save"),
                    [this]() {
                        // The Receive action row has no backup behind it; treat it
                        // like the "New backup" row (cell 0) for a Backup press.
                        const size_t row = directoryList->index();
                        this->doBackup(hid.fullIndex(), isReceiveRow(row) ? 0 : rowToCell(row));
                        TransferJob::get().start();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }

        if (buttonRestoreAL->released() || (kDown & KEY_R)) {
            size_t row = directoryList->index();
            if (g_bottomScrollEnabled && row > 0 && !isReceiveRow(row)) {
                requestRestore(rowToCell(row));
            }
        }
    }
}

void MainScreen::updateButtons(void)
{
    if (MS::multipleSelectionEnabled()) {
        buttonBackupAL->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_RAISED, COLOR_MUTED);
        buttonScripts->setColors(COLOR_RAISED, COLOR_MUTED);
    }
    else {
        buttonBackupAL->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_RAISED, COLOR_TEXT);
        buttonScripts->setColors(COLOR_RAISED, COLOR_TEXT);
    }
}

std::string MainScreen::nameFromCell(size_t index) const
{
    return directoryList->name(cellToRow(index));
}

bool MainScreen::isReceiveRow(size_t row) const
{
    return selected.transferRow && row == 1;
}

size_t MainScreen::rowToCell(size_t row) const
{
    return (selected.transferRow && row > 1) ? row - 1 : row;
}

size_t MainScreen::cellToRow(size_t cell) const
{
    return (selected.transferRow && cell > 0) ? cell + 1 : cell;
}

void MainScreen::startTransferReceive(void)
{
    // Receiver lifetime == overlay lifetime (B in the overlay stops it), so no
    // background-receiver state is introduced here.
    std::string error;
    if (!Transfer::startReceiver(error)) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, error.empty() ? i18n::t("main.receiver_failed") : error);
    }
    else {
        currentOverlay = std::make_shared<ReceiveOverlay>(*this);
    }
}

void MainScreen::startTransferSend(void)
{
    if (TitleCatalog::get().getTitleCount(backupKind) <= 0) {
        currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.no_titles"));
        return;
    }

    const size_t row = directoryList->index();
    if (!g_bottomScrollEnabled || row == 0 || isReceiveRow(row)) {
        currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.select_backup_send"));
        return;
    }
    size_t cellIndex = rowToCell(row);

    Title title;
    TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);
    BackupTarget target = title.backup(backupKind);

    std::string backupName    = nameFromCell(cellIndex);
    std::u16string backupPath = target.fullPath(cellIndex);

    std::string ipPort = KeyboardManager::get().text(Configuration::getInstance().lastTransferAddress(), i18n::t("main.receiver_ip_port"), 32);
    if (ipPort.empty()) {
        return;
    }
    auto dst = Transfer::parseTarget(ipPort);
    if (!dst) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, i18n::t("main.invalid_ip_port"));
        return;
    }
    // Remember a valid address so the next send prefills the keyboard with it.
    Configuration::getInstance().setLastTransferAddress(ipPort);

    std::string pin = KeyboardManager::get().text("1234", i18n::t("main.pin_prompt"), 5);
    if (pin.empty()) {
        return;
    }
    if (!Transfer::validPin(pin)) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, i18n::t("main.pin_invalid"));
        return;
    }

    // Keyboard + validation happened above on the UI thread; the blocking IO
    // (zip + socket) runs on the TransferJob worker. Title and params go by
    // value so nothing here needs to outlive this frame.
    std::string dataType = target.dataTypeName();
    TransferJob::get().enqueueSend(
        std::move(title), std::move(backupPath), std::move(backupName), std::move(dataType), std::move(dst->ip), dst->port, std::move(pin));
    TransferJob::get().start();
}
