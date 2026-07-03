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

#include "MainScreenV4.hpp"
#include "KeyboardManager.hpp"
#include "SettingsScreenV4.hpp"
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
// Top grid (400x240): 8 columns x 4 rows of 44px tiles, 4px gap, centered.
static constexpr size_t rowlen = 4, collen = 8;
static constexpr int HEADER_H = 24, FOOTER_H = 20;
static constexpr int TILE = 44, GAP = 4;
static constexpr int GRID_LEFT = (400 - (8 * TILE + 7 * GAP)) / 2; // = 10
static constexpr int GRID_TOP  = HEADER_H + 6;                     // = 30

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

    // Human-readable byte count for the backups card header.
    std::string humanSize(u64 bytes)
    {
        if (bytes >= 1024ull * 1024ull) {
            return StringUtils::format("%.1f MB", bytes / (1024.0 * 1024.0));
        }
        if (bytes >= 1024ull) {
            return StringUtils::format("%.1f KB", bytes / 1024.0);
        }
        return StringUtils::format("%llu B", bytes);
    }

    // Draws a single button-glyph footer hint line, centered on `screenW`.
    void drawHints(int screenW, int y, const std::string& text)
    {
        TextPool::get().drawCentered(text, 0, screenW, y, 0.47f, COLOR_V4_MUTED);
    }
}

MainScreenV4::MainScreenV4(void) : hid(rowlen * collen, collen)
{
    selectionTimer  = 0;
    refreshTimer    = 0;
    transferEnabled = Configuration::getInstance().transferEnabled();

    // Detail action buttons. Backup is the primary (accent), Restore secondary.
    buttonBackup  = std::make_unique<Clickable>(8, 182, 148, 30, COLOR_V4_ACCENT, COLOR_WHITE, "Backup ", true);
    buttonRestore = std::make_unique<Clickable>(164, 182, 148, 30, COLOR_V4_RAISED, COLOR_V4_TEXT, "Restore ", true);
    // Narrower Backup/Restore + Coins trio shown side by side on the Activity Log title.
    buttonBackupAL  = std::make_unique<Clickable>(8, 182, 96, 30, COLOR_V4_ACCENT, COLOR_WHITE, "Backup", true);
    buttonPlayCoins = std::make_unique<Clickable>(112, 182, 96, 30, COLOR_V4_RAISED, COLOR_V4_TEXT, "Coins", true);
    buttonRestoreAL = std::make_unique<Clickable>(216, 182, 96, 30, COLOR_V4_RAISED, COLOR_V4_TEXT, "Restore", true);
    // Full-width batch-backup button shown only while multi-selecting.
    buttonBackupAll = std::make_unique<Clickable>(8, 182, 304, 30, COLOR_V4_ACCENT, COLOR_WHITE, "Backup selected", true);
    buttonTransfer  = std::make_unique<Clickable>(4, 223, 96, 16, COLOR_V4_RAISED, COLOR_V4_MUTED, "Transfer", true);
    directoryList   = std::make_unique<BackupList>(12, 70, 296, 106, 5);
    buttonBackup->canChangeColorWhenSelected(true);
    buttonBackupAll->canChangeColorWhenSelected(true);
    buttonRestore->canChangeColorWhenSelected(true);
    buttonPlayCoins->canChangeColorWhenSelected(true);
    buttonBackupAL->canChangeColorWhenSelected(true);
    buttonRestoreAL->canChangeColorWhenSelected(true);
    buttonTransfer->canChangeColorWhenSelected(true);

    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    C2D_PlainImageTint(&flagTint, COLOR_V4_TEAL, 1.0f);
    C2D_PlainImageTint(&checkboxTint, COLOR_V4_BASE, 1.0f);
    C2D_PlainImageTint(&starTint, COLOR_V4_BASE, 1.0f);
}

int MainScreenV4::cellX(size_t i) const
{
    return GRID_LEFT + (int)((i % (rowlen * collen)) % collen) * (TILE + GAP);
}

int MainScreenV4::cellY(size_t i) const
{
    return GRID_TOP + (int)((i % (rowlen * collen)) / collen) * (TILE + GAP);
}

void MainScreenV4::drawSelector(void) const
{
    const int x = cellX(hid.index());
    const int y = cellY(hid.index());
    C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, COLOR_WHITEMASK);
    Gui::drawPulsingOutline(x, y, TILE, TILE, 2, COLOR_V4_RING);
}

void MainScreenV4::drawTile(size_t k) const
{
    const int x = cellX(k);
    const int y = cellY(k);
    C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, COLOR_V4_CARD);
    C2D_Image icon = TitleCatalog::get().icon(k, backupKind);
    if (icon.subtex->width == 48) {
        // Scale the native 48px SMDH icon down to the 44px tile.
        C2D_DrawImageAt(icon, x, y, 0.5f, nullptr, (float)TILE / 48.0f, (float)TILE / 48.0f);
    }
    else {
        // Smaller icons (DS/other) sit centered, unscaled.
        const int off = (TILE - icon.subtex->width) / 2;
        C2D_DrawImageAt(icon, x + off, y + off, 0.5f, nullptr, 1.0f, 1.0f);
    }
}

void MainScreenV4::drawTop(void) const
{
    auto selEnt          = MS::selectedEntries();
    const bool multi     = MS::multipleSelectionEnabled();
    const size_t entries = hid.maxVisibleEntries();
    const size_t count   = TitleCatalog::get().getTitleCount(backupKind);
    const size_t max     = hid.maxEntries(count) + 1;

    TextPool& text = TextPool::get();
    C2D_TargetClear(g_top, COLOR_V4_BASE);
    C2D_TargetClear(g_bottom, COLOR_V4_BASE);
    C2D_SceneBegin(g_top);

    // Header bar.
    C2D_DrawRectSolid(0, 0, 0.5f, 400, HEADER_H, COLOR_V4_SURFACE);
    C2D_DrawRectSolid(0, HEADER_H, 0.5f, 400, 1, COLOR_V4_LINE);
    // Teal brand mark + wordmark.
    C2D_DrawImageAt(flag, 6, 3, 0.5f, &flagTint, 1.0f, 1.0f);
    float nameX = 6 + ceilf(flag.subtex->width * 1.0f) + 6;
    nameX += text.draw("Checkpoint", nameX, 4, 0.5f, COLOR_V4_TEXT) + 6;
    text.draw(ver, nameX, 6, 0.4f, COLOR_V4_FAINT);

    // Right cluster: time, count / multi-select badge.
    std::string timeStr = DateTime::timeStr();
    {
        float w = text.width(timeStr, 0.42f);
        text.draw(timeStr, 400 - 6 - w, 6, 0.42f, COLOR_V4_FAINT);

        if (multi) {
            std::string badge = StringUtils::format("%zu selected", selEnt.size());
            float bw          = text.width(badge, 0.42f);
            float bx          = 400 - 6 - w - 8 - bw - 12;
            C2D_DrawRectSolid(bx - 6, 4, 0.5f, bw + 12, 16, COLOR_V4_ACCENT);
            text.draw(badge, bx, 6, 0.42f, COLOR_WHITE);
        }
        else {
            std::string cnt = StringUtils::format("%zu titles", count);
            float cw        = text.width(cnt, 0.42f);
            text.draw(cnt, 400 - 6 - w - 10 - cw, 6, 0.42f, COLOR_V4_MUTED);
        }
    }

    LoadProgress loadProgress = TitleCatalog::get().progress();
    if (loadProgress.active) {
        int percentage = loadProgress.percent();
        if (percentage >= 100) {
            percentage = 99;
        }
        std::string msg = StringUtils::format("Loading titles... %d%%", percentage);
        text.drawCentered(msg, 0, 400, ceilf((240 - 0.6f * fontGetInfo(NULL)->lineFeed) / 2), 0.6f, COLOR_V4_TEXT, 0.9f);
        return;
    }

    // Tiles.
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        drawTile(k);
    }

    if (count > 0) {
        drawSelector();
    }

    // Multi-select veil + badges, favorite pips.
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        const int x = cellX(k), y = cellY(k);
        const bool checked = !selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end();

        if (multi && !checked && k != hid.fullIndex()) {
            C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, COLOR_V4_DIM);
        }
        if (checked) {
            C2D_DrawRectSolid(x, y, 0.5f, TILE, TILE, C2D_Color32(122, 66, 196, 90));
            C2D_DrawRectSolid(x + TILE - 17, y + 2, 0.5f, 15, 15, COLOR_V4_ACCENT);
            C2D_SpriteSetPos(&checkbox, x + TILE - 18, y + 1);
            C2D_DrawSpriteTinted(&checkbox, &checkboxTint);
        }
        if (TitleCatalog::get().favorite(k, backupKind)) {
            C2D_DrawRectSolid(x + TILE - 14, y - 3, 0.5f, 15, 15, COLOR_V4_GOLD);
            C2D_SpriteSetPos(&star, x + TILE - 15, y - 4);
            C2D_DrawSpriteTinted(&star, &starTint);
        }
    }

    // Footer hint bar.
    C2D_DrawRectSolid(0, 220, 0.5f, 400, FOOTER_H, COLOR_V4_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 400, 1, COLOR_V4_LINE);
    if (multi) {
        drawHints(400, 223, " Tag     hold all     Backup all     Clear");
    }
    else {
        drawHints(400, 223, " Open     Select     Extdata    Hold SELECT: help");
    }

    // Hold-SELECT command help overlay.
    if (hidKeysHeld() & KEY_SELECT) {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
        const char* cmds[] = {
            " Move between titles",
            " Enter title",
            " Back  -  hold to refresh titles",
            " Tag title  -  hold to select all",
            " Switch Save / Extdata",
            "\xEE\x80\x80 Open Settings",
        };
        const float scale = 0.6f;
        const float lh    = scale * fontGetInfo(NULL)->lineFeed;
        const int n       = sizeof(cmds) / sizeof(cmds[0]);
        float top         = ceilf((240 - n * lh) / 2);
        for (int i = 0; i < n; i++) {
            text.drawCentered(cmds[i], 0, 400, top + i * lh, scale, COLOR_V4_TEXT, 0.9f);
        }
        if (Server::isRunning() && Server::getAddress().length() > 0) {
            text.draw("Logs at " + Server::getAddress() + "/logs/memory", 6, 223, 0.42f, COLOR_V4_FAINT);
        }
    }

    // Live transfer status (network sends draw their own modal on the bottom).
    TransferSnapshot ts = TransferStatus::snapshot();
    if (ts.active && ts.kind == TransferKind::Network) {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
        u64 total = ts.bytesTotal, done = ts.bytesDone;
        int pct            = total > 0 ? (int)((done * 100) / total) : 0;
        std::string prefix = ts.mode.empty() ? "Transferring backup" : ts.mode;
        std::string line1  = StringUtils::format("%s... %d%%", prefix.c_str(), pct);
        std::string line2  = TransferStatus::bytesToMB(done, total);
        const float lh     = 0.7f * fontGetInfo(NULL)->lineFeed;
        float startY       = ceilf((240 - 2 * lh) / 2);
        text.drawCentered(line1, 0, 400, startY, 0.7f, COLOR_V4_TEXT);
        text.drawCentered(line2, 0, 400, startY + lh, 0.7f, COLOR_V4_MUTED);
    }
}

void MainScreenV4::drawBottom(void) const
{
    TextPool& text = TextPool::get();
    C2D_SceneBegin(g_bottom);

    LoadProgress loadProgress = TitleCatalog::get().progress();
    const size_t count        = TitleCatalog::get().getTitleCount(backupKind);

    selectedValid = false; // re-armed below only when a title is actually drawn

    if (!loadProgress.active && count > 0) {
        Title title;
        TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);

        // Header: title name, Save/Extdata segmented control.
        C2D_DrawRectSolid(0, 0, 0.5f, 320, HEADER_H, COLOR_V4_SURFACE);
        C2D_DrawRectSolid(0, HEADER_H, 0.5f, 320, 1, COLOR_V4_LINE);

        // Segmented Save / Extdata toggle (right-aligned in the header) — laid out
        // first so the title name below knows how much width it can use.
        int segX = 320;
        {
            const int segH = 16, segY = 4, pad = 7;
            int cellSaveW = (int)ceilf(text.width("Save", 0.4f)) + pad * 2;
            int cellExtW  = (int)ceilf(text.width("Extdata", 0.4f)) + pad * 2;
            segX          = 320 - 6 - cellSaveW - cellExtW;
            C2D_DrawRectSolid(segX, segY, 0.5f, cellSaveW + cellExtW, segH, COLOR_V4_RAISED);
            const bool onSave = backupKind == BackupKind::Save;
            C2D_DrawRectSolid(onSave ? segX : segX + cellSaveW, segY, 0.5f, onSave ? cellSaveW : cellExtW, segH, COLOR_V4_ACCENT);
            text.draw("Save", segX + pad, segY + 2, 0.4f, onSave ? COLOR_WHITE : COLOR_V4_MUTED);
            text.draw("Extdata", segX + cellSaveW + pad, segY + 2, 0.4f, onSave ? COLOR_V4_MUTED : COLOR_WHITE);
        }

        std::string name = text.truncate(title.shortDescription(), segX - 8 - 8, 0.5f);
        text.draw(name, 8, 4, 0.5f, COLOR_V4_TEXT);

        // Thin info line: cart identifier, media type, favorite.
        {
            float x            = 8;
            const float y      = 28;
            const float sep    = 6;
            std::string cartId = title.productCode[0] != '\0' ? std::string(title.productCode) : std::string("System title");
            x += text.draw(cartId, x, y, 0.42f, COLOR_V4_MUTED) + sep;
            x += text.draw("·  " + title.mediaTypeString(), x, y, 0.42f, COLOR_V4_MUTED) + sep;
            if (TitleCatalog::get().favorite(hid.fullIndex(), backupKind)) {
                text.draw("·  ★ Favorite", x, y, 0.42f, COLOR_V4_GOLD);
            }
        }

        // Backups card. Build the rows: entry 0 is the "New backup" affordance,
        // the rest are existing backups labelled with their (async) size.
        BackupTarget target              = title.backup(backupKind);
        std::vector<std::u16string> dirs = target.backups();
        directoryList->clear();
        for (size_t i = 0; i < dirs.size(); i++) {
            if (i == 0) {
                directoryList->push_back("New backup", backupKind == BackupKind::Save ? "From current save" : "From current extdata", true);
            }
            else {
                std::optional<u64> bs = BackupSizeCache::get().backupSize(target.fullPath(i));
                directoryList->push_back(StringUtils::UTF16toUTF8(dirs.at(i)), bs.has_value() ? humanSize(*bs) : std::string("…"), false);
            }
        }
        const size_t backupCount = dirs.empty() ? 0 : dirs.size() - 1; // entry 0 is "New..."

        // Stash identity so update() can ask the async size cache without copying
        // the Title again; the worker walks the SD off the UI thread.
        selectedId    = title.id();
        selectedRoot  = target.rootPath();
        selectedValid = true;

        C2D_DrawRectSolid(8, 46, 0.5f, 304, 132, COLOR_V4_CARD);
        C2D_DrawRectSolid(8, 67, 0.5f, 304, 1, COLOR_V4_LINE);
        text.draw("Backups", 16, 49, 0.45f, COLOR_V4_MUTED);
        {
            // Total: shown once the worker has resolved it; until then a "…" placeholder.
            std::optional<u64> sz = BackupSizeCache::get().total(selectedId, backupKind);
            std::string meta      = backupCount == 0 ? std::string("No backups")
                                    : sz.has_value() ? StringUtils::format("%zu saved  ·  %s", backupCount, humanSize(*sz).c_str())
                                                     : StringUtils::format("%zu saved  ·  …", backupCount);
            float w               = text.width(meta, 0.42f);
            text.draw(meta, 312 - 8 - w, 50, 0.42f, COLOR_V4_FAINT);
        }
        directoryList->draw(g_bottomScrollEnabled);

        // Actions. While multi-selecting, one full-width Backup button replaces the
        // per-title Backup/Restore pair and drives the whole tagged batch.
        if (MS::multipleSelectionEnabled()) {
            buttonBackupAll->text(StringUtils::format("Backup %zu selected", MS::selectedEntries().size()));
            buttonBackupAll->draw(0.6f, COLOR_V4_RING);
        }
        else if (title.isActivityLog()) {
            buttonBackupAL->draw(0.6f, COLOR_V4_RING);
            buttonPlayCoins->draw(0.6f, COLOR_V4_ACCENT);
            buttonRestoreAL->draw(0.6f, COLOR_V4_RING);
        }
        else {
            buttonBackup->draw(0.6f, COLOR_V4_RING);
            buttonRestore->draw(0.6f, COLOR_V4_RING);
        }
    }

    if (transferEnabled) {
        buttonTransfer->draw(0.5f, COLOR_V4_ACCENT);
    }

    // Footer hint bar.
    C2D_DrawRectSolid(0, 220, 0.5f, 320, FOOTER_H, COLOR_V4_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 320, 1, COLOR_V4_LINE);
    drawHints(320, 223,
        MS::multipleSelectionEnabled() ? "Backup selected      Clear selection"
        : g_bottomScrollEnabled        ? " Confirm      Delete      Back"
                                       : " Backups      Backup      Restore");

    // Live local-copy progress modal (network sends draw on the top screen).
    TransferSnapshot ts = TransferStatus::snapshot();
    if (ts.active && ts.kind != TransferKind::Network) {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);

        const bool multiSelect = ts.saveTotal > 1;
        const int mx = 30, mw = 260;
        const int mh = multiSelect ? 162 : 130;
        const int my = multiSelect ? 40 : 65;
        C2D_DrawRectSolid(mx, my, 0.5f, mw, mh, COLOR_V4_CARD);
        Gui::drawOutline(mx, my, mw, mh, 2, COLOR_V4_ACCENT);

        std::string titleStr = (ts.mode.empty() ? "Copying files" : ts.mode) + " in progress...";
        text.drawCentered(titleStr, mx, mw, my + 10, 0.55f, COLOR_V4_TEXT);

        std::string fname = StringUtils::UTF16toUTF8(ts.currentFile);
        text.drawCentered(fname, mx, mw, my + 30, 0.5f, COLOR_V4_FAINT);

        const int barX = mx + 12, barW = mw - 24, barH = 10;
        auto drawProgressBar = [&](int y, float frac, const char* leftLabel, const char* rightLabel) {
            if (frac > 1.0f) {
                frac = 1.0f;
            }
            C2D_DrawRectSolid(barX, y, 0.5f, barW, barH, COLOR_BLACK_MEDIUM);
            int fillW = (int)(barW * frac);
            if (fillW > 0) {
                C2D_DrawRectSolid(barX, y, 0.5f, fillW, barH, COLOR_V4_ACCENT);
            }
            text.draw(leftLabel, barX, y + barH + 3, 0.42f, COLOR_V4_FAINT);
            text.draw(rightLabel, barX + barW - ceilf(text.width(rightLabel, 0.42f)), y + barH + 3, 0.42f, COLOR_V4_TEXT);
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

void MainScreenV4::update(const InputState& input)
{
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

    // Kick off (or no-op) an off-thread size walk for the title drawn this frame.
    // drawBottom stashed its identity + backup root; the cache coalesces repeats
    // and the draw reads the memoized total once the worker lands it.
    if (selectedValid) {
        BackupSizeCache::get().request(selectedId, backupKind, selectedRoot);
    }
}

void MainScreenV4::refreshTitlesFull(void)
{
    hid.reset();
    MS::clearSelectedEntries();
    directoryList->resetIndex();
    Threads::executeTask(TitleCatalog::loadTitlesThread);
    refreshTimer = 0;
}

void MainScreenV4::updateSelector(void)
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

void MainScreenV4::doBackup(size_t fullIndex, size_t cellIndex)
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

void MainScreenV4::doRestore(size_t fullIndex, size_t cellIndex)
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

void MainScreenV4::requestRestore(size_t cellIndex)
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

void MainScreenV4::handleEvents(const InputState& input)
{
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    // Hold SELECT (shows the command help) then press A to open Settings.
    if ((kHeld & KEY_SELECT) && (kDown & KEY_A)) {
        g_pendingScreen = std::make_shared<SettingsScreenV4>(g_screen);
        return;
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

    bool activityLog = false;
    if (TitleCatalog::get().getTitleCount(backupKind) > 0) {
        Title title;
        TitleCatalog::get().getTitle(title, hid.fullIndex(), backupKind);
        activityLog = title.isActivityLog();
    }

    if (MS::multipleSelectionEnabled()) {
        // One large Backup button (touch or A) backs up the whole tagged batch;
        // it replaces the per-title Backup/Restore pair while multi-selecting.
        if (buttonBackupAll->released() || (kDown & KEY_A)) {
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
        if ((activityLog ? buttonBackupAL : buttonBackup)->released()) {
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

        if ((activityLog ? buttonRestoreAL : buttonRestore)->released()) {
            size_t cellIndex = directoryList->index();
            if (g_bottomScrollEnabled && cellIndex > 0) {
                requestRestore(cellIndex);
            }
        }
    }

    if ((activityLog && buttonPlayCoins->released()) || ((hidKeysDown() & KEY_TOUCH) && input.py < 20 && input.px > 294)) {
        if (!Archive::setPlayCoins()) {
            currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, "Failed to set play coins.");
        }
    }
}

void MainScreenV4::updateButtons(void)
{
    if (MS::multipleSelectionEnabled()) {
        buttonBackup->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_V4_RAISED, COLOR_V4_MUTED);
        buttonPlayCoins->setColors(COLOR_V4_RAISED, COLOR_V4_MUTED);
        buttonBackupAL->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_V4_RAISED, COLOR_V4_MUTED);
    }
    else if (g_bottomScrollEnabled) {
        buttonBackup->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
        buttonPlayCoins->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
        buttonBackupAL->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
    }
    else {
        buttonBackup->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestore->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
        buttonPlayCoins->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
        buttonBackupAL->setColors(COLOR_V4_ACCENT, COLOR_WHITE);
        buttonRestoreAL->setColors(COLOR_V4_RAISED, COLOR_V4_TEXT);
    }
}

std::string MainScreenV4::nameFromCell(size_t index) const
{
    return directoryList->name(index);
}

void MainScreenV4::startTransferSend(void)
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
