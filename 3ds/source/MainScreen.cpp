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
#include "SettingsScreen.hpp"
#include "backupsize.hpp"
#include "backuptarget.hpp"
#include "configuration.hpp"
#include "io.hpp"
#include "loader.hpp"
#include "progress.hpp"
#include "server.hpp"
#include "textpool.hpp"
#include "transfer.hpp"
#include "transferjob.hpp"
#include "transferstatus.hpp"
#include <3ds.h>
#include <cctype>
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
        std::u16string name =
            MS::multipleSelectionEnabled() ? StringUtils::UTF8toUTF16(suggestion.c_str()) : KeyboardManager::get().keyboard(suggestion);
        if (name.empty()) {
            return std::nullopt;
        }
        return target.rootPath() + StringUtils::UTF8toUTF16("/") + name;
    }

    std::string backupErrorMessage(io::BackupStage stage, const char* dataType)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return "Failed to open save archive.";
            case io::BackupStage::DeleteDst:
                return "Failed to delete the existing backup\ndirectory recursively.";
            case io::BackupStage::CreateDst:
                return "Failed to create destination directory.";
            default:
                return std::string("Failed to backup ") + dataType + ".";
        }
    }

    std::string restoreErrorMessage(io::BackupStage stage, const char* dataType)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return "Failed to open save archive.";
            case io::BackupStage::ReadFile:
                return "Failed to read save file backup.";
            case io::BackupStage::Commit:
                return "Failed to commit save data.";
            case io::BackupStage::SecureValue:
                return "Failed to fix secure value.";
            default:
                return std::string("Failed to restore ") + dataType + ".";
        }
    }

    std::string sendErrorMessage(const Transfer::SendOutcome& outcome)
    {
        switch (outcome.stage) {
            case Transfer::SendStage::Zip:
                return "Failed to create backup package.";
            case Transfer::SendStage::Socket:
                return "Failed to open socket.";
            case Transfer::SendStage::Resolve:
                return "Invalid IP address.";
            case Transfer::SendStage::Connect:
                return "Failed to connect.";
            case Transfer::SendStage::Response:
                return outcome.detail.empty() ? "Receiver returned no response." : "Receiver error: " + outcome.detail;
            default:
                return "Transfer failed.";
        }
    }

    std::string rawKeyboard(const std::string& suggestion, const std::string& hint, size_t maxLen)
    {
        SwkbdState swkbd;
        const size_t kBufSize = 64;
        size_t limit          = std::min(maxLen, kBufSize);
        if (limit < 2) {
            limit = 2;
        }
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, limit - 1);
        swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);
        swkbdSetInitialText(&swkbd, suggestion.c_str());
        swkbdSetHintText(&swkbd, hint.c_str());
        char buf[kBufSize]   = {0};
        SwkbdButton button   = swkbdInputText(&swkbd, buf, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        return button == SWKBD_BUTTON_CONFIRM ? std::string(buf) : std::string();
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
        g.cellSaveW = (int)ceilf(text.width("Save", 0.4f)) + g.pad * 2;
        g.cellExtW  = (int)ceilf(text.width("Extdata", 0.4f)) + g.pad * 2;
        g.segX      = 320 - 6 - g.cellSaveW - g.cellExtW;
        return g;
    }
}

MainScreen::MainScreen(void) : hid(rowlen * collen, collen)
{
    selectionTimer  = 0;
    refreshTimer    = 0;
    transferEnabled = Configuration::getInstance().transferEnabled();

    // Detail action buttons. Backup is the primary (accent), Restore secondary.
    buttonBackup  = std::make_unique<Clickable>(8, 182, 148, 30, COLOR_ACCENT, COLOR_WHITE, "Backup ", true);
    buttonRestore = std::make_unique<Clickable>(164, 182, 148, 30, COLOR_RAISED, COLOR_TEXT, "Restore ", true);
    // Narrower Backup/Restore + Coins trio shown side by side on the Activity Log title.
    buttonBackupAL  = std::make_unique<Clickable>(8, 182, 96, 30, COLOR_ACCENT, COLOR_WHITE, "Backup", true);
    buttonPlayCoins = std::make_unique<Clickable>(112, 182, 96, 30, COLOR_RAISED, COLOR_TEXT, "Coins", true);
    buttonRestoreAL = std::make_unique<Clickable>(216, 182, 96, 30, COLOR_RAISED, COLOR_TEXT, "Restore", true);
    // Full-width batch-backup button shown only while multi-selecting.
    buttonBackupAll = std::make_unique<Clickable>(8, 182, 304, 30, COLOR_ACCENT, COLOR_WHITE, "Backup selected", true);
    buttonTransfer  = std::make_unique<Clickable>(4, 223, 96, 16, COLOR_RAISED, COLOR_MUTED, "Transfer", true);
    directoryList   = std::make_unique<BackupList>(12, 70, 296, 106, 5);
    buttonBackup->canChangeColorWhenSelected(true);
    buttonBackupAll->canChangeColorWhenSelected(true);
    buttonRestore->canChangeColorWhenSelected(true);
    buttonPlayCoins->canChangeColorWhenSelected(true);
    buttonBackupAL->canChangeColorWhenSelected(true);
    buttonRestoreAL->canChangeColorWhenSelected(true);
    buttonTransfer->canChangeColorWhenSelected(true);

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

    // Header bar.
    C2D_DrawRectSolid(0, 0, 0.5f, 400, HEADER_H, COLOR_SURFACE);
    C2D_DrawRectSolid(0, HEADER_H, 0.5f, 400, 1, COLOR_LINE);
    // Brand mark + wordmark. Teal on dark, Checkpoint blue on light.
    C2D_ImageTint brandTint;
    C2D_PlainImageTint(&brandTint, Configuration::getInstance().theme() == "light" ? COLOR_BLUE : COLOR_TEAL, 1.0f);
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
            std::string badge = StringUtils::format("%zu selected", selEnt.size());
            float bw          = text.width(badge, 0.42f);
            float bx          = 400 - 6 - w - 8 - bw - 12;
            C2D_DrawRectSolid(bx - 6, 4, 0.5f, bw + 12, 16, COLOR_ACCENT);
            text.draw(badge, bx, 6, 0.42f, COLOR_WHITE);
        }
        else {
            std::string cnt = StringUtils::format("%zu titles", count);
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
        std::string msg = StringUtils::format("Loading titles... %d%%", percentage);
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

    // Footer hint bar.
    C2D_DrawRectSolid(0, 220, 0.5f, 400, FOOTER_H, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 400, 1, COLOR_LINE);
    if (multi) {
        drawHints(400, 223, " Tag     hold all     Backup all     Clear");
    }
    else {
        drawHints(400, 223, " Open     Tag     Extdata    SELECT Settings");
    }

    // Live transfer status (network sends draw their own modal on the bottom).
    const TransferSnapshot& ts = mTransfer;
    if (ts.active && ts.kind == TransferKind::Network) {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
        u64 total = ts.bytesTotal, done = ts.bytesDone;
        int pct            = total > 0 ? (int)((done * 100) / total) : 0;
        std::string prefix = ts.mode.empty() ? "Transferring backup" : ts.mode;
        std::string line1  = StringUtils::format("%s... %d%%", prefix.c_str(), pct);
        std::string line2  = TransferStatus::bytesToMB(done, total);
        const float lh     = 0.7f * fontGetInfo(NULL)->lineFeed;
        float startY       = ceilf((240 - 2 * lh) / 2);
        text.drawCentered(line1, 0, 400, startY, 0.7f, COLOR_TEXT);
        text.drawCentered(line2, 0, 400, startY + lh, 0.7f, COLOR_MUTED);
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
            text.draw("Save", seg.segX + seg.pad, seg.segY + 2, 0.4f, onSave ? COLOR_WHITE : COLOR_MUTED);
            text.draw("Extdata", seg.segX + seg.cellSaveW + seg.pad, seg.segY + 2, 0.4f, onSave ? COLOR_MUTED : COLOR_WHITE);
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
                text.draw("·  ★ Favorite", x, y, 0.42f, COLOR_GOLD);
            }
        }

        // Backups card. Rows (entry 0 "New backup", then existing backups with
        // their async sizes) were rebuilt by refreshSelected() when they changed.
        C2D_DrawRectSolid(8, 46, 0.5f, 304, 132, COLOR_CARD);
        C2D_DrawRectSolid(8, 67, 0.5f, 304, 1, COLOR_LINE);
        text.draw("Backups", 16, 49, 0.45f, COLOR_MUTED);
        {
            // Total: shown once the worker has resolved it; until then a "…" placeholder.
            std::string meta = selected.backupCount == 0 ? std::string("No backups")
                               : selected.totalSize      ? StringUtils::format("%zu saved  ·  %s", selected.backupCount,
                                                               StringUtils::humanBytes(*selected.totalSize).c_str())
                                                         : StringUtils::format("%zu saved  ·  …", selected.backupCount);
            float w          = text.width(meta, 0.42f);
            text.draw(meta, 312 - 8 - w, 50, 0.42f, COLOR_FAINT);
        }
        directoryList->draw(g_bottomScrollEnabled);

        // Actions. While multi-selecting, one full-width Backup button replaces the
        // per-title Backup/Restore pair and drives the whole tagged batch.
        if (MS::multipleSelectionEnabled()) {
            buttonBackupAll->text(StringUtils::format("Backup %zu selected ", MS::selectedEntries().size()));
            buttonBackupAll->draw(0.6f, COLOR_RING);
        }
        else if (selected.activityLog) {
            buttonBackupAL->draw(0.6f, COLOR_RING);
            buttonPlayCoins->draw(0.6f, COLOR_ACCENT);
            buttonRestoreAL->draw(0.6f, COLOR_RING);
        }
        else {
            buttonBackup->draw(0.6f, COLOR_RING);
            buttonRestore->draw(0.6f, COLOR_RING);
        }
    }

    if (transferEnabled) {
        buttonTransfer->draw(0.5f, COLOR_ACCENT);
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
        MS::multipleSelectionEnabled() ? "Backup selected      Clear selection"
        : g_bottomScrollEnabled        ? " Confirm      Delete      Back"
                                       : " Go to saves");

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

        std::string titleStr = (ts.mode.empty() ? "Copying files" : ts.mode) + " in progress...";
        text.drawCentered(titleStr, mx, mw, my + 10, 0.55f, COLOR_TEXT);

        std::string fname = StringUtils::UTF16toUTF8(ts.currentFile);
        text.drawCentered(fname, mx, mw, my + 30, 0.5f, COLOR_FAINT);

        const int barX = mx + 12, barW = mw - 24, barH = 10;
        auto drawProgressBar = [&](int y, float frac, const char* leftLabel, const char* rightLabel) {
            if (frac > 1.0f) {
                frac = 1.0f;
            }
            C2D_DrawRectSolid(barX, y, 0.5f, barW, barH, COLOR_BLACK_MEDIUM);
            int fillW = (int)(barW * frac);
            if (fillW > 0) {
                C2D_DrawRectSolid(barX, y, 0.5f, fillW, barH, COLOR_ACCENT);
            }
            text.draw(leftLabel, barX, y + barH + 3, 0.42f, COLOR_FAINT);
            text.draw(rightLabel, barX + barW - ceilf(text.width(rightLabel, 0.42f)), y + barH + 3, 0.42f, COLOR_TEXT);
        };

        int barY = my + 52;
        if (multiSelect) {
            float overallProgress = (float)ts.saveCount / (float)ts.saveTotal;
            char overallCountStr[24];
            snprintf(overallCountStr, sizeof(overallCountStr), "Save %zu / %zu", ts.saveCount + 1, ts.saveTotal);
            char overallPctStr[8];
            snprintf(overallPctStr, sizeof(overallPctStr), "%d%%", (int)(overallProgress * 100));
            drawProgressBar(barY, overallProgress, overallCountStr, overallPctStr);
            barY += 30;
        }
        float progress = (ts.copyTotal > 0) ? (float)ts.copyCount / (float)ts.copyTotal : 0.0f;
        char countStr[24];
        snprintf(countStr, sizeof(countStr), "File %zu / %zu", ts.copyCount, ts.copyTotal);
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%", (int)((progress > 1.0f ? 1.0f : progress) * 100));
        drawProgressBar(barY, progress, countStr, pctStr);
        barY += 30;
        float fileProgress = (ts.currentFileSize > 0) ? (float)ts.currentFileOffset / (float)ts.currentFileSize : 0.0f;
        char kbStr[32];
        snprintf(kbStr, sizeof(kbStr), "%.1f / %.1f KB", ts.currentFileOffset / 1024.0f, ts.currentFileSize / 1024.0f);
        char filePctStr[8];
        snprintf(filePctStr, sizeof(filePctStr), "%d%%", (int)((fileProgress > 1.0f ? 1.0f : fileProgress) * 100));
        drawProgressBar(barY, fileProgress, kbStr, filePctStr);
    }
}

void MainScreen::update(const InputState& input)
{
    // Snapshot the live transfer state once for this frame; both draw*() read the
    // member instead of re-locking TransferStatus. Done before any early return
    // so the modal keeps updating even while a TransferJob is active.
    mTransfer = TransferStatus::snapshot();

    // Re-read in case the Settings page toggled it while this screen was parked.
    transferEnabled = Configuration::getInstance().transferEnabled();

    if (auto result = TransferJob::get().takeResult()) {
        // A backup/restore changed one or more folders; drop every cached total so
        // they get re-walked off-thread (a batch may have touched many titles).
        BackupSizeCache::get().invalidateAll();
        if (result->ok) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, result->successMsg);
        }
        else if (result->send) {
            if (result->send->stage == Transfer::SendStage::EmptyBackup) {
                currentOverlay = std::make_shared<InfoOverlay>(*this, "Selected backup is empty.");
            }
            else {
                currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, sendErrorMessage(*result->send));
            }
        }
        else {
            std::string message = result->isRestore ? restoreErrorMessage(result->stage, result->dataType.c_str())
                                                    : backupErrorMessage(result->stage, result->dataType.c_str());
            currentOverlay      = std::make_shared<ErrorOverlay>(*this, result->res, message);
        }
        return;
    }

    if (TransferJob::get().active()) {
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
        selected.sizeGen == sizeGen) {
        return; // snapshot still describes what is on screen
    }

    if (!selected.valid || selected.kind != backupKind || selected.catalogGen != catalogGen) {
        gridFavorites.assign(count, 0);
        for (size_t i = 0; i < count; i++) {
            gridFavorites[i] = catalog.favorite(i, backupKind) ? 1 : 0;
        }
    }

    selected.valid      = count > 0;
    selected.fullIndex  = hid.fullIndex();
    selected.kind       = backupKind;
    selected.catalogGen = catalogGen;
    selected.sizeGen    = sizeGen;

    if (!selected.valid) {
        directoryList->clear();
        return;
    }

    Title title;
    catalog.getTitle(title, selected.fullIndex, backupKind);
    BackupTarget target = title.backup(backupKind);

    selected.id          = title.id();
    selected.rootPath    = target.rootPath();
    selected.name        = title.shortDescription();
    selected.cartId      = title.productCode[0] != '\0' ? std::string(title.productCode) : std::string("System title");
    selected.mediaType   = title.mediaTypeString();
    selected.favorite    = catalog.favorite(selected.fullIndex, backupKind);
    selected.activityLog = title.isActivityLog();
    selected.totalSize   = BackupSizeCache::get().total(selected.id, backupKind);

    // Rows: entry 0 is the "New backup" affordance, the rest are existing
    // backups labelled with their (async) size.
    std::vector<std::u16string> dirs = target.backups();
    directoryList->clear();
    for (size_t i = 0; i < dirs.size(); i++) {
        if (i == 0) {
            directoryList->push_back("New backup", backupKind == BackupKind::Save ? "From current save" : "From current extdata", true);
        }
        else {
            std::optional<u64> bs = BackupSizeCache::get().backupSize(target.fullPath(i));
            directoryList->push_back(StringUtils::UTF16toUTF8(dirs.at(i)), bs.has_value() ? StringUtils::humanBytes(*bs) : std::string("…"), false);
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
    std::string successMsg = nameFromCell(cellIndex) + "\nhas been restored successfully.";
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
        currentOverlay = std::make_shared<YesNoOverlay>(*this, "Restore selected save?", run, [this]() { this->removeOverlay(); });
    }
    else {
        run();
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
                    *this, "Backup selected title?",
                    [this]() {
                        this->doBackup(hid.fullIndex(), 0);
                        TransferJob::get().start();
                    },
                    [this]() { this->removeOverlay(); });
            }
            else {
                requestRestore(directoryList->index());
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
            if (index > 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Delete selected backup?",
                    [this, index]() {
                        Title title;
                        TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);
                        std::u16string path = title.backup(backupKind).fullPath(index);
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

    if (transferEnabled && buttonTransfer->released()) {
        currentOverlay = std::make_shared<TransferMenuOverlay>(
            *this,
            [this]() {
                this->removeOverlay();
                this->startTransferSend();
            },
            [this]() {
                std::string error;
                if (!Transfer::startReceiver(error)) {
                    this->currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, error.empty() ? "Failed to start receiver." : error);
                    return;
                }
                this->currentOverlay = std::make_shared<ReceiveOverlay>(*this);
            });
    }

    // From the snapshot the current frame was drawn from, so input maps to the
    // buttons the user actually sees.
    const bool activityLog = selected.valid && selected.activityLog;

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
        if ((activityLog ? buttonBackupAL : buttonBackup)->released() || (kDown & KEY_L)) {
            if (g_bottomScrollEnabled) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Backup selected save?",
                    [this]() {
                        this->doBackup(hid.fullIndex(), directoryList->index());
                        TransferJob::get().start();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }

        if ((activityLog ? buttonRestoreAL : buttonRestore)->released() || (kDown & KEY_R)) {
            size_t cellIndex = directoryList->index();
            if (g_bottomScrollEnabled && cellIndex > 0) {
                requestRestore(cellIndex);
            }
        }
    }

    if (activityLog && buttonPlayCoins->released()) {
        if (!Archive::setPlayCoins()) {
            currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, "Failed to set play coins.");
        }
    }
}

void MainScreen::updateButtons(void)
{
    if (MS::multipleSelectionEnabled()) {
        buttonBackup->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_RAISED, COLOR_MUTED);
        buttonPlayCoins->setColors(COLOR_RAISED, COLOR_MUTED);
        buttonBackupAL->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_RAISED, COLOR_MUTED);
    }
    else if (g_bottomScrollEnabled) {
        buttonBackup->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_RAISED, COLOR_TEXT);
        buttonPlayCoins->setColors(COLOR_RAISED, COLOR_TEXT);
        buttonBackupAL->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_RAISED, COLOR_TEXT);
    }
    else {
        buttonBackup->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_RAISED, COLOR_TEXT);
        buttonPlayCoins->setColors(COLOR_RAISED, COLOR_TEXT);
        buttonBackupAL->setColors(COLOR_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_RAISED, COLOR_TEXT);
    }
}

std::string MainScreen::nameFromCell(size_t index) const
{
    return directoryList->name(index);
}

void MainScreen::startTransferSend(void)
{
    if (TitleCatalog::get().getTitleCount(backupKind) <= 0) {
        currentOverlay = std::make_shared<InfoOverlay>(*this, "No titles available.");
        return;
    }

    size_t cellIndex = directoryList->index();
    if (!g_bottomScrollEnabled || cellIndex == 0) {
        currentOverlay = std::make_shared<InfoOverlay>(*this, "Select a backup to send.");
        return;
    }

    Title title;
    TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);
    BackupTarget target = title.backup(backupKind);

    std::string backupName    = nameFromCell(cellIndex);
    std::u16string backupPath = target.fullPath(cellIndex);

    std::string ipPort = rawKeyboard("192.168.1.10:8000", "Receiver IP:PORT", 32);
    if (ipPort.empty()) {
        return;
    }

    size_t colon = ipPort.find(':');
    if (colon == std::string::npos) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, "Invalid IP:PORT.");
        return;
    }
    std::string ip = ipPort.substr(0, colon);
    int port       = atoi(ipPort.substr(colon + 1).c_str());
    if (ip.empty() || port <= 0 || port > 65535) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, "Invalid IP:PORT.");
        return;
    }

    std::string pin = rawKeyboard("1234", "PIN (4 digits)", 5);
    if (pin.empty()) {
        return;
    }
    bool pinOk = pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (!pinOk) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, "PIN must be 4 digits.");
        return;
    }

    // Keyboard + validation happened above on the UI thread; the blocking IO
    // (zip + socket) runs on the TransferJob worker. Title and params go by
    // value so nothing here needs to outlive this frame.
    std::string dataType = target.dataTypeName();
    TransferJob::get().enqueueSend(
        std::move(title), std::move(backupPath), std::move(backupName), std::move(dataType), std::move(ip), (u16)port, std::move(pin));
    TransferJob::get().start();
}
