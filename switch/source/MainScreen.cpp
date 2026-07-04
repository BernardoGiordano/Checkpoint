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
#include "SettingsScreen.hpp"
#include "backupsize.hpp"
#include "savedatasource.hpp"
#include "savekind.hpp"
#include "shapes.hpp"
#include "titlecatalog.hpp"
#include "transferjob.hpp"
#include "uikit.hpp"
#include <algorithm>
#include <optional>

// Main browser layout. All coordinates are absolute pixels on the fixed
// 1280x720 canvas.
namespace {
    constexpr int TOPBAR_H = 56;

    // Left rail: 56x56 save-kind buttons at x12, y72 stepping by 66; the
    // gear button and the account avatar anchored to the bottom.
    constexpr int RAIL_W          = 80;
    constexpr int RAIL_ITEM       = 56;
    constexpr int RAIL_ITEM_X     = 12;
    constexpr int RAIL_ITEM_Y0    = 72;
    constexpr int RAIL_ITEM_PITCH = 66;
    constexpr int SETTINGS_BTN_Y  = 544;
    constexpr int AVATAR_SIZE     = 44;
    constexpr int AVATAR_X        = 18;
    constexpr int AVATAR_Y        = 610;

    // Title grid: origin 100,76; 5 columns of 142px tiles, 12px gap →
    // 154px pitch; 3 rows visible per page (15 tiles).
    constexpr int GRID_COLS    = 5;
    constexpr int GRID_ROWS    = 3;
    constexpr int GRID_VISIBLE = GRID_COLS * GRID_ROWS;
    constexpr int TILE         = 142;
    constexpr int TILE_PITCH   = 154;
    constexpr int GRID_X0      = 100;
    constexpr int GRID_Y0      = 76;
    constexpr int GRID_AREA_X  = RAIL_W;
    constexpr int GRID_AREA_W  = 800;

    // Right panel: 400px column with an inner 360px content column.
    constexpr int PANEL_X       = 880;
    constexpr int COL_X         = 900;
    constexpr int COL_W         = 360;
    constexpr int HEADER_ICON   = 88;
    constexpr int BTN_W         = 360;
    constexpr int BTN_H         = 56;
    constexpr int BTN_BACKUP_Y  = 532;
    constexpr int BTN_RESTORE_Y = 598;
    constexpr int LIST_Y        = 214;
    constexpr int LIST_ROWS     = 5;

    std::string backupErrorMessage(io::BackupStage stage)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return "Failed to mount save.";
            case io::BackupStage::DeleteDst:
                return "Failed to delete the existing backup\ndirectory recursively.";
            case io::BackupStage::CreateDst:
                return "Failed to create destination directory.";
            default:
                return "Failed to backup save.";
        }
    }

    std::string restoreErrorMessage(io::BackupStage stage)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return "Failed to mount save.";
            case io::BackupStage::DeleteDst:
                return "Failed to delete save.";
            case io::BackupStage::Commit:
                return "Failed to commit to save device.";
            default:
                return "Failed to restore save.";
        }
    }

    // A save-kind rail button: square (radius 0), filled accent when active,
    // faint fill otherwise, with the single kind letter over the small kind label.
    void drawRailItem(int y, const SaveKind& kind, bool active)
    {
        Shapes::fillRound(RAIL_ITEM_X, y, RAIL_ITEM, RAIL_ITEM, 0, active ? COLOR_ACCENT : COLOR_FILL1);
        SDL_Color fg = active ? COLOR_WHITE : COLOR_TEXT2;

        u32 gw, gh, lw, lh;
        SDLH_GetTextDimensions(16, kind.buttonLabel, &gw, &gh);
        SDLH_GetTextDimensions(9, kind.railLabel, &lw, &lh);
        const int stackH = (int)gh + 2 + (int)lh;
        const int top    = y + (RAIL_ITEM - stackH) / 2;
        SDLH_DrawText(16, RAIL_ITEM_X + (RAIL_ITEM - (int)gw) / 2, top, fg, kind.buttonLabel);
        SDLH_DrawText(9, RAIL_ITEM_X + (RAIL_ITEM - (int)lw) / 2, top + (int)gh + 2, fg, kind.railLabel);
    }

    // Size of the shoulder-key system glyph drawn inside an action button.
    constexpr int ACTION_GLYPH_SIZE = 26;

    // An action button: filled accent (Backup/Send) or outlined
    // (Restore/Receive), label + the shoulder-key system glyph centered as a group.
    void drawActionButton(int x, int y, const std::string& label, const std::string& key, bool filled)
    {
        // Square buttons (radius 0), matching the 3DS action buttons and the
        // squared rail / rows.
        if (filled) {
            Shapes::fillRound(x, y, BTN_W, BTN_H, 0, COLOR_ACCENT);
        }
        else {
            Shapes::strokeRound(x, y, BTN_W, BTN_H, 0, 2, COLOR_STROKE3);
        }

        const std::string glyph = UiKit::buttonGlyph(key);
        const SDL_Color fg      = filled ? COLOR_WHITE : COLOR_TEXT;

        u32 lw, lh, gw, gh;
        SDLH_GetTextDimensions(16, label.c_str(), &lw, &lh);
        SDLH_GetTextDimensions(ACTION_GLYPH_SIZE, glyph.c_str(), &gw, &gh);
        const int gap   = 10;
        const int group = (int)lw + gap + (int)gw;
        const int sx    = x + (BTN_W - group) / 2;
        SDLH_DrawText(16, sx, y + (BTN_H - (int)lh) / 2, fg, label.c_str());
        SDLH_DrawText(ACTION_GLYPH_SIZE, sx + (int)lw + gap, y + (BTN_H - (int)gh) / 2, fg, glyph.c_str());
    }
}

MainScreen::MainScreen(const InputState& input) : hid(GRID_VISIBLE, GRID_COLS, input)
{
    pksmBridge     = false;
    selectionTimer = 0;
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    backupList    = std::make_unique<BackupList>(COL_X, LIST_Y, COL_W, LIST_ROWS * BackupList::ROW_PITCH, LIST_ROWS);
    buttonBackup  = std::make_unique<Clickable>(COL_X, BTN_BACKUP_Y, BTN_W, BTN_H, COLOR_ACCENT, COLOR_WHITE, "Backup", true);
    buttonRestore = std::make_unique<Clickable>(COL_X, BTN_RESTORE_Y, BTN_W, BTN_H, COLOR_SURFACE, COLOR_TEXT, "Restore", true);

    // The rail Clickables exist only so released() can hit-test touches; the
    // rail is drawn by drawRailItem, not by Clickable::draw.
    for (int k = 0; k < 4; k++) {
        int y            = RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * k;
        filterButtons[k] = std::make_unique<Clickable>(RAIL_ITEM_X, y, RAIL_ITEM, RAIL_ITEM, COLOR_FILL1, COLOR_TEXT2, "", true);
    }
    settingsButton = std::make_unique<Clickable>(RAIL_ITEM_X, SETTINGS_BTN_Y, RAIL_ITEM, RAIL_ITEM, COLOR_FILL1, COLOR_TEXT2, "", true);
    avatarButton   = std::make_unique<Clickable>(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, COLOR_TILE, COLOR_TEXT2, "", true);
}

int MainScreen::selectorX(size_t i) const
{
    const int col = (int)((i % GRID_VISIBLE) % GRID_COLS);
    return GRID_X0 + col * TILE_PITCH;
}

int MainScreen::selectorY(size_t i) const
{
    const int row = (int)((i % GRID_VISIBLE) / GRID_COLS);
    return GRID_Y0 + row * TILE_PITCH;
}

void MainScreen::setSaveTypeFilter(saveTypeFilter_t filter)
{
    if (mSaveTypeFilter == filter)
        return;
    mSaveTypeFilter = filter;
    this->index(TITLES, 0);
    this->index(CELLS, 0);
    g_backupScrollEnabled = false;
    MS::clearSelectedEntries();
    setPKSMBridgeFlag(false);
}

void MainScreen::draw() const
{
    const size_t entries     = hid.maxVisibleEntries();
    const size_t filteredCnt = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
    const size_t max         = filteredCnt > 0 ? hid.maxEntries(filteredCnt) + 1 : 0;
    auto selEnt              = MS::selectedEntries();

    SDLH_ClearScreen(COLOR_BG);

    // Resolve the selected title once (cached): its name feeds the top bar and
    // its detail feeds the right panel.
    std::string gameName;
    if (filteredCnt > 0) {
        backupList->refreshSelected(g_currentUId, mSaveTypeFilter, hid.fullIndex(), TitleCatalog::get().generation());
        gameName = backupList->title().displayName();
    }

    // ---- Top bar ----
    u32 logoW, logoH;
    SDLH_GetTextDimensions(22, "checkpoint", &logoW, &logoH);
    SDLH_DrawText(22, 24, (TOPBAR_H - (int)logoH) / 2, COLOR_TEXT, "checkpoint");

    // Plain version label just right of the logo (no chip), e.g. "v4.0.0".
    u32 verW, verH;
    SDLH_GetTextDimensions(11, ver, &verW, &verH);
    const int verX = 24 + (int)logoW + 12;
    SDLH_DrawText(11, verX, (TOPBAR_H - (int)verH) / 2 + 3, COLOR_TEXT2, ver);
    const int topbarLeftEnd = verX + (int)verW;

    if (!gameName.empty()) {
        const int avail = 1256 - topbarLeftEnd - 24;
        std::string t   = gameName;
        u32 tw, th;
        SDLH_GetTextDimensions(17, t.c_str(), &tw, &th);
        if ((int)tw > avail) {
            t = trimToFit(t, avail, 17);
            SDLH_GetTextDimensions(17, t.c_str(), &tw, &th);
        }
        SDLH_DrawText(17, 1256 - (int)tw, (TOPBAR_H - (int)th) / 2, COLOR_TEXT, t.c_str());
    }

    // ---- Frame hairlines ----
    SDLH_DrawRect(0, TOPBAR_H, 1280, 1, COLOR_STROKE1);
    SDLH_DrawRect(RAIL_W, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);
    SDLH_DrawRect(PANEL_X, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);

    // ---- Left rail ----
    for (int k = 0; k < 4; k++) {
        drawRailItem(RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * k, SaveKind::all()[k], mSaveTypeFilter == static_cast<saveTypeFilter_t>(k));
    }
    // Sidebar cursor spans the 4 save-kind buttons plus the settings gear
    // (index 4), so it can be reached by pressing Down past SYSTEM.
    if (sidebarFocused) {
        const int selY = sidebarCursor < 4 ? RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * sidebarCursor : SETTINGS_BTN_Y;
        Shapes::focusRing(RAIL_ITEM_X, selY, RAIL_ITEM, RAIL_ITEM, 14, COLOR_ACCENT);
    }

    Shapes::fillRound(RAIL_ITEM_X, SETTINGS_BTN_Y, RAIL_ITEM, RAIL_ITEM, 0, COLOR_FILL1);
    {
        u32 gw, gh;
        SDLH_GetTextDimensions(22, "", &gw, &gh);
        SDLH_DrawText(22, RAIL_ITEM_X + (RAIL_ITEM - (int)gw) / 2, SETTINGS_BTN_Y + (RAIL_ITEM - (int)gh) / 2, COLOR_TEXT2, "");
    }

    Shapes::fillRound(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, AVATAR_SIZE / 2, COLOR_TILE);
    if (Account::icon(g_currentUId) != NULL) {
        SDLH_DrawImageScale(Account::icon(g_currentUId), AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE);
    }
    Shapes::strokeRound(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, AVATAR_SIZE / 2, 1, COLOR_STROKE2);
    {
        std::string username = Account::shortName(g_currentUId);
        u32 uw, uh;
        SDLH_GetTextDimensions(10, username.c_str(), &uw, &uh);
        SDLH_DrawText(10, RAIL_W / 2 - (int)uw / 2, AVATAR_Y + AVATAR_SIZE + 4, COLOR_TEXT2, username.c_str());
    }

    // ---- Title grid ----
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        const int tx = selectorX(k), ty = selectorY(k);
        Shapes::cardRound(tx, ty, TILE, TILE, 14, COLOR_TILE, COLOR_STROKE2, 1);
        SDL_Texture* smallIcon = TitleCatalog::get().filteredSmallIcon(g_currentUId, mSaveTypeFilter, k);
        if (smallIcon != NULL) {
            SDLH_DrawImageScale(smallIcon, tx, ty, TILE, TILE);
        }

        const bool selected = !selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end();
        const bool favorite = TitleCatalog::get().filteredFavorite(g_currentUId, mSaveTypeFilter, k);
        if (selected || favorite) {
            const int bx = tx + TILE - 8 - 24, by = ty + 8;
            Shapes::fillRound(bx, by, 24, 24, 8, selected ? COLOR_ACCENT : FC_MakeColor(16, 16, 20, 191));
            if (selected && SDLH_CheckboxTexture() != NULL) {
                SDLH_DrawImageScale(SDLH_CheckboxTexture(), bx + 4, by + 4, 16, 16);
            }
            else if (SDLH_StarTexture() != NULL) {
                SDLH_DrawImageScale(SDLH_StarTexture(), bx + 5, by + 5, 14, 14);
            }
        }
    }

    // Focus ring on the selected tile (hidden while the rail owns the cursor).
    if (filteredCnt > 0 && !sidebarFocused) {
        Shapes::focusRing(selectorX(hid.index()), selectorY(hid.index()), TILE, TILE, 14, COLOR_ACCENT);
    }

    // ---- Right panel ----
    if (filteredCnt > 0) {
        Title& title = backupList->title();

        Shapes::cardRound(COL_X, 76, HEADER_ICON, HEADER_ICON, 16, COLOR_TILE, COLOR_STROKE2, 1);
        if (TitleCatalog::get().iconFor(title.id()) != NULL) {
            SDLH_DrawImageScale(TitleCatalog::get().iconFor(title.id()), COL_X, 76, HEADER_ICON, HEADER_ICON);
        }

        const int infoX   = COL_X + HEADER_ICON + 14;
        const int infoW   = COL_X + COL_W - infoX;
        std::string line1 = title.author();
        if (title.saveDataType() == FsSaveDataType_Account && !title.userName().empty()) {
            line1 += " · " + title.userName();
        }
        const bool hasPlay = title.saveDataType() == FsSaveDataType_Account && !title.playTime().empty();
        std::string idStr  = StringUtils::format("%016llX", title.id());

        u32 h1, h2, h3;
        SDLH_GetTextDimensions(12, "Ag", NULL, &h1);
        SDLH_GetTextDimensions(13, "Ag", NULL, &h2);
        SDLH_GetTextDimensions(11, "Ag", NULL, &h3, FontFamily::Mono);
        int stackH = (int)h1 + 4 + (int)h3 + (hasPlay ? 4 + (int)h2 : 0);
        int ty     = 76 + (HEADER_ICON - stackH) / 2;

        SDLH_DrawText(12, infoX, ty, COLOR_TEXT2, trimToFit(line1, infoW, 12).c_str());
        ty += (int)h1 + 4;
        if (hasPlay) {
            u32 lblW;
            SDLH_GetTextDimensions(13, "Play time ", &lblW, NULL);
            SDLH_DrawText(13, infoX, ty, COLOR_TEXT2, "Play time ");
            SDLH_DrawText(13, infoX + (int)lblW, ty, COLOR_TEXT, title.playTime().c_str());
            ty += (int)h2 + 4;
        }
        SDLH_DrawText(11, infoX, ty, COLOR_TEXT3, idStr.c_str(), FontFamily::Mono);

        // Backups header row.
        const int headerY      = 184;
        std::string countLabel = StringUtils::format("BACKUPS · %zu", backupList->backupCount());
        u32 clH;
        SDLH_GetTextDimensions(11, countLabel.c_str(), NULL, &clH);
        UiKit::drawSectionLabel(COL_X, headerY, countLabel.c_str());

        {
            // Total on-disk size of this title's backups, right-aligned against
            // the "BACKUPS · N" label.
            const std::string& totalSize = backupList->totalSizeString();
            if (!totalSize.empty()) {
                u32 tw, th;
                SDLH_GetTextDimensions(11, totalSize.c_str(), &tw, &th, FontFamily::Mono);
                SDLH_DrawText(11, COL_X + COL_W - (int)tw, headerY + ((int)clH - (int)th) / 2, COLOR_TEXT2, totalSize.c_str(), FontFamily::Mono);
            }
        }

        backupList->draw(g_backupScrollEnabled);

        if (MS::multipleSelectionEnabled()) {
            // Multi-select is a batch backup only (no restore): show a single
            // button counting the selected titles, wired to the same L handler.
            const size_t n        = selEnt.size();
            const std::string lbl = StringUtils::format("Backup %zu %s", n, n == 1 ? "title" : "titles");
            drawActionButton(COL_X, BTN_BACKUP_Y, lbl, "L", true);
        }
        else {
            const bool pksm = getPKSMBridgeFlag() && mSaveTypeFilter == FILTER_SAVES;
            drawActionButton(COL_X, BTN_BACKUP_Y, pksm ? "Send" : "Backup", "L", true);
            drawActionButton(COL_X, BTN_RESTORE_Y, pksm ? "Receive" : "Restore", "R", false);
        }
    }
    else {
        const char* emptyMsg = SaveKind::of(mSaveTypeFilter).emptyMsg;
        u32 emptyW, emptyH;
        SDLH_GetTextDimensions(18, emptyMsg, &emptyW, &emptyH);
        SDLH_DrawText(18, GRID_AREA_X + (GRID_AREA_W - (int)emptyW) / 2, (720 - (int)emptyH) / 2, COLOR_TEXT2, emptyMsg);
    }

    // ---- Hint bar ----
    // Minus opens Settings (see the class note); no help overlay in this build.
    UiKit::drawHintBar({
        {"A", "Select"},
        {"B", "Back"},
        {"X", "Sort"},
        {"Y", "Multi-select"},
        {"-", "Settings"},
    });

    // ---- Transfer modal ----
    const TransferSnapshot transfer = TransferStatus::snapshot();
    if (transfer.active) {
        SDLH_DrawRect(0, 0, 1280, 720, COLOR_SCRIM);

        const bool multiSelect = transfer.saveTotal > 1;
        const int mx = 370, mw = 540;
        const int mh = multiSelect ? 290 : 230;
        const int my = multiSelect ? 230 : 260;
        Shapes::cardRound(mx, my, mw, mh, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

        std::string titleStr = (transfer.mode.empty() ? "Copying files" : transfer.mode) + " in progress...";
        u32 title_w, title_h;
        SDLH_GetTextDimensions(20, titleStr.c_str(), &title_w, &title_h);
        SDLH_DrawText(20, mx + (mw - (int)title_w) / 2, my + 16, COLOR_TEXT, titleStr.c_str());

        if (transfer.cancellable) {
            const std::string hint = UiKit::buttonGlyph("B") + " to cancel";
            u32 hint_w;
            SDLH_GetTextDimensions(14, hint.c_str(), &hint_w, NULL);
            SDLH_DrawText(14, mx + mw - (int)hint_w - 16, my + mh - 26, COLOR_TEXT2, hint.c_str());
        }

        u32 fname_w, fname_h;
        std::string fname = trimToFit(transfer.currentFile, mw - 40, 15);
        SDLH_GetTextDimensions(15, fname.c_str(), &fname_w, &fname_h);
        SDLH_DrawText(15, mx + (mw - (int)fname_w) / 2, my + 16 + (int)title_h + 8, COLOR_TEXT2, fname.c_str());

        const int barX = mx + 20, barW = mw - 40, barH = 16;
        auto drawProgressBar = [&](int y, float frac, const char* leftLabel, const char* rightLabel) {
            if (frac > 1.0f)
                frac = 1.0f;
            Shapes::fillRound(barX, y, barW, barH, 0, COLOR_FILL2);
            int fillW = (int)(barW * frac);
            if (fillW > 0) {
                Shapes::fillRound(barX, y, fillW, barH, 0, COLOR_ACCENT);
            }
            u32 right_w;
            SDLH_GetTextDimensions(15, rightLabel, &right_w, NULL);
            SDLH_DrawText(15, barX, y + barH + 6, COLOR_TEXT2, leftLabel);
            SDLH_DrawText(15, barX + barW - (int)right_w, y + barH + 6, COLOR_TEXT, rightLabel);
        };

        int barY = my + 108;
        if (multiSelect) {
            float overallProgress = (float)transfer.saveCount / (float)transfer.saveTotal;
            char overallCountStr[24];
            snprintf(overallCountStr, sizeof(overallCountStr), "Save %zu / %zu", transfer.saveCount + 1, transfer.saveTotal);
            char overallPctStr[8];
            snprintf(overallPctStr, sizeof(overallPctStr), "%d%%", (int)(overallProgress * 100));
            drawProgressBar(barY, overallProgress, overallCountStr, overallPctStr);
            barY += 52;
        }

        float progress = (transfer.copyTotal > 0) ? (float)transfer.copyCount / (float)transfer.copyTotal : 0.0f;
        char countStr[24];
        snprintf(countStr, sizeof(countStr), "File %zu / %zu", transfer.copyCount, transfer.copyTotal);
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%", (int)((progress > 1.0f ? 1.0f : progress) * 100));
        drawProgressBar(barY, progress, countStr, pctStr);
        barY += 52;

        float fileProgress = (transfer.currentFileSize > 0) ? (float)transfer.currentFileOffset / (float)transfer.currentFileSize : 0.0f;
        char kbStr[40];
        snprintf(kbStr, sizeof(kbStr), "%.1f / %.1f KB", transfer.currentFileOffset / 1024.0f, transfer.currentFileSize / 1024.0f);
        char filePctStr[8];
        snprintf(filePctStr, sizeof(filePctStr), "%d%%", (int)((fileProgress > 1.0f ? 1.0f : fileProgress) * 100));
        drawProgressBar(barY, fileProgress, kbStr, filePctStr);
    }
}

void MainScreen::update(const InputState& input)
{
    // Deliver a finished backup/restore: the worker ran the copy off the main
    // loop, so the result comes back here. Refresh the backup lists the worker
    // touched (it cannot, having no catalog mutex), raise the result overlay, and
    // skip input this frame (next frame routes to the overlay).
    if (auto result = TransferJob::get().takeResult()) {
        for (u64 id : result->refreshIds) {
            TitleCatalog::get().refreshDirectories(id);
            BackupSizeCache::get().invalidate(id); // folders changed → recompute sizes
        }
        if (result->cancelled) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, "Backup cancelled.");
        }
        else if (result->ok) {
            blinkLed(4);
            currentOverlay = std::make_shared<InfoOverlay>(*this, result->successMsg);
        }
        else {
            std::string message = result->isRestore ? restoreErrorMessage(result->stage) : backupErrorMessage(result->stage);
            currentOverlay      = std::make_shared<ErrorOverlay>(*this, result->res, message);
        }
        return;
    }

    // While a transfer runs on the worker, the loop keeps drawing the modal from
    // TransferStatus; ignore input so nothing mutates underneath the copy, except a
    // B press on a backup in flight, which requests a cancel (a restore can't be
    // cancelled: aborting mid-write could leave a truncated save on the cartridge).
    if (TransferJob::get().active()) {
        if ((input.kDown & HidNpadButton_B) && TransferStatus::snapshot().cancellable) {
            TransferJob::get().requestCancel();
        }
        return;
    }

    updateSelector(input);
    handleEvents(input);
}

void MainScreen::updateSelector(const InputState& input)
{
    // Hiding/showing titles from Settings bumps the catalog generation and can
    // shrink the filtered list under the cursor. Clamp the index and drop the
    // selection before anything reads them, so backup/restore can't fall back to
    // targeting the first (wrong) title.
    const u32 gen = TitleCatalog::get().generation();
    if (gen != mLastGeneration) {
        mLastGeneration    = gen;
        const size_t count = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
        if (hid.fullIndex() >= count) {
            this->index(TITLES, count > 0 ? count - 1 : 0);
        }
        MS::clearSelectedEntries();
    }

    if (!g_backupScrollEnabled) {
        size_t count    = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
        size_t oldindex = hid.index();
        if (sidebarFocused && (input.kDown & (HidNpadButton_Right | HidNpadButton_B))) {
            sidebarFocused   = false;
            sidebarExitFrame = true;
        }
        else if (sidebarFocused) {
            // don't update title grid while sidebar is focused
        }
        else if (sidebarExitFrame) {
            // skip hid.update() until Right/B is released so held key doesn't move the title cursor
            if (!(input.kHeld & (HidNpadButton_AnyRight | HidNpadButton_B))) {
                sidebarExitFrame = false;
            }
        }
        else if ((input.kDown & HidNpadButton_Left) && hid.index() % GRID_COLS == 0) {
            sidebarFocused = true;
            sidebarCursor  = static_cast<int>(mSaveTypeFilter);
        }
        else {
            hid.update(count);
        }

        // loop through every rendered title
        for (u8 row = 0; row < GRID_ROWS; row++) {
            for (u8 col = 0; col < GRID_COLS; col++) {
                u8 index = row * GRID_COLS + col;
                if (index > hid.maxEntries(count))
                    break;

                u32 x = selectorX(index);
                u32 y = selectorY(index);
                if (input.touch.count > 0 && input.touch.touches[0].x >= x && input.touch.touches[0].x <= x + TILE && input.touch.touches[0].y >= y &&
                    input.touch.touches[0].y <= y + TILE) {
                    hid.index(index);
                }
            }
        }

        backupList->resetIndex();
        if (hid.index() != oldindex) {
            setPKSMBridgeFlag(false);
        }
    }
    else {
        backupList->updateSelection();
    }
}

size_t MainScreen::rawIndex() const
{
    return TitleCatalog::get().filteredToRawIndex(g_currentUId, mSaveTypeFilter, this->index(TITLES));
}

void MainScreen::doBackup(size_t rawIdx, size_t cellIndex)
{
    Title title;
    TitleCatalog::get().getTitle(title, g_currentUId, rawIdx);

    // Resolve the destination (keyboard prompt and all) on the UI thread, then
    // hand the owned Title to the worker. The caller calls TransferJob::start()
    // once all saves are enqueued. blinkLed and the result overlay are raised by
    // update() when the batch finishes.
    bool usedKeyboardFallback      = false;
    std::optional<std::string> dst = BackupList::chooseDst(title, cellIndex, usedKeyboardFallback);
    removeOverlay();
    if (!dst) { // keyboard prompt cancelled
        return;
    }

    std::string successMsg = usedKeyboardFallback ? "Progress correctly saved to disk.\nSystem keyboard applet was not\naccessible. The suggested "
                                                    "destination\nfolder was used instead."
                                                  : "Progress correctly saved to disk.";
    TransferJob::get().enqueueBackup(std::move(title), *dst, std::move(successMsg));
}

void MainScreen::doRestore(size_t rawIdx, size_t cellIndex)
{
    Title title;
    TitleCatalog::get().getTitle(title, g_currentUId, rawIdx);
    std::string name = nameFromCell(cellIndex);
    std::string src  = title.fullPath(cellIndex) + "/";
    removeOverlay();

    TransferJob::get().enqueueRestore(std::move(title), std::move(src), name + "\nhas been restored successfully.");
}

void MainScreen::requestRestoreSelected(void)
{
    // A restore overwrites the on-console save, so confirm by default; the
    // confirm-restore toggle (Settings > General) can skip the prompt.
    if (Configuration::getInstance().isConfirmRestoreEnabled()) {
        currentOverlay = std::make_shared<YesNoOverlay>(
            *this, "Restore selected save?",
            [this]() {
                doRestore(rawIndex(), this->index(CELLS));
                TransferJob::get().start();
            },
            [this]() { this->removeOverlay(); });
    }
    else {
        doRestore(rawIndex(), this->index(CELLS));
        TransferJob::get().start();
    }
}

void MainScreen::handleEvents(const InputState& input)
{
    const u64 kheld = input.kHeld;
    const u64 kdown = input.kDown;

    // handle rail save-kind button touches
    for (int k = 0; k < 4; k++) {
        if (filterButtons[k]->released()) {
            setSaveTypeFilter(static_cast<saveTypeFilter_t>(k));
            sidebarFocused = false;
            break;
        }
    }

    // Minus (or the rail gear) opens Settings. Guarded so you can't navigate
    // away mid-copy; the swap itself is deferred to main() via g_pendingScreen.
    if (((kdown & HidNpadButton_Minus) || settingsButton->released()) && !TransferJob::get().active()) {
        g_pendingScreen = std::make_shared<SettingsScreen>(g_screen);
        return;
    }

    // handle sidebar D-pad navigation. Cursor 0..3 = save-kind buttons, 4 =
    // settings gear (so Down from SYSTEM reaches it, Up from USER wraps to it).
    if (sidebarFocused) {
        if (kdown & HidNpadButton_Up) {
            sidebarCursor = sidebarCursor > 0 ? sidebarCursor - 1 : 4;
        }
        else if (kdown & HidNpadButton_Down) {
            sidebarCursor = sidebarCursor < 4 ? sidebarCursor + 1 : 0;
        }
        if (kdown & HidNpadButton_A) {
            if (sidebarCursor == 4) {
                if (!TransferJob::get().active()) {
                    g_pendingScreen = std::make_shared<SettingsScreen>(g_screen);
                }
            }
            else {
                setSaveTypeFilter(static_cast<saveTypeFilter_t>(sidebarCursor));
            }
        }
        // Right/B exit is handled in updateSelector to prevent double cursor movement
        return;
    }

    // handle StickL press to cycle filter
    if (kdown & HidNpadButton_StickL) {
        setSaveTypeFilter(SaveKind::next(mSaveTypeFilter));
    }

    if (mSaveTypeFilter == FILTER_SAVES) {
        if (kdown & HidNpadButton_ZL || kdown & HidNpadButton_ZR) {
            while ((g_currentUId = Account::selectAccount()) == 0)
                ;
            this->index(TITLES, 0);
            this->index(CELLS, 0);
            MS::clearSelectedEntries(); // filtered indices belong to the old account
            setPKSMBridgeFlag(false);
        }
    }

    // handle PKSM bridge (only for account saves). The per-frame Title copy
    // (strings + two path vectors) is gated behind the L+R hold so nothing is
    // rebuilt on the frames no shoulder combo is held.
    if (mSaveTypeFilter == FILTER_SAVES && Configuration::getInstance().isPKSMBridgeEnabled() && !getPKSMBridgeFlag() && (kheld & HidNpadButton_L) &&
        (kheld & HidNpadButton_R)) {
        Title title;
        TitleCatalog::get().getTitle(title, g_currentUId, rawIndex());
        if (title.saveDataType() != FsSaveDataType_Bcat && title.saveDataType() != FsSaveDataType_Device && isPKSMBridgeTitle(title.id())) {
            setPKSMBridgeFlag(true);
        }
    }

    // Tapping the avatar opens the account picker. Release-triggered (like the
    // rail buttons) so the applet fires once on lift instead of relaunching every
    // frame the finger is held down on it.
    if (!g_backupScrollEnabled && avatarButton->released()) {
        while ((g_currentUId = Account::selectAccount()) == 0)
            ;
        this->index(TITLES, 0);
        this->index(CELLS, 0);
        MS::clearSelectedEntries(); // filtered indices belong to the old account
        setPKSMBridgeFlag(false);
    }

    // Handle touching the backup list / panel region
    if (input.touch.count > 0 && input.touch.touches[0].x > COL_X && input.touch.touches[0].x < COL_X + COL_W && input.touch.touches[0].y > LIST_Y &&
        input.touch.touches[0].y < BTN_BACKUP_Y) {
        // Activate backup list only if multiple selections are not enabled
        if (!MS::multipleSelectionEnabled()) {
            g_backupScrollEnabled = true;
            entryType(CELLS);
        }
    }

    // Handle pressing A
    // Backup list active:   Backup/Restore
    // Backup list inactive: Activate backup list only if multiple
    //                       selections are enabled
    if ((kdown & HidNpadButton_A) && TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter) > 0) {
        // If backup list is active...
        if (g_backupScrollEnabled) {
            // If the "New..." entry is selected...
            if (0 == this->index(CELLS)) {
                if (!getPKSMBridgeFlag()) {
                    doBackup(rawIndex(), this->index(CELLS));
                    TransferJob::get().start();
                }
            }
            else {
                if (getPKSMBridgeFlag()) {
                    // Receiving overwrites the save: confirm first, like the R-press path.
                    currentOverlay = std::make_shared<YesNoOverlay>(
                        *this, "Receive save from PKSM?",
                        [this]() {
                            auto result = recvFromPKSMBridge(rawIndex(), g_currentUId, this->index(CELLS));
                            if (std::get<0>(result)) {
                                currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                            }
                            else {
                                currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                            }
                        },
                        [this]() { this->removeOverlay(); });
                }
                else {
                    requestRestoreSelected();
                }
            }
        }
        else {
            // Activate backup list only if multiple selections are not enabled
            if (!MS::multipleSelectionEnabled()) {
                g_backupScrollEnabled = true;
                entryType(CELLS);
            }
        }
    }

    // Handle pressing B
    if ((kdown & HidNpadButton_B) || (input.touch.count > 0 && input.touch.touches[0].x >= GRID_AREA_X &&
                                         input.touch.touches[0].x <= (GRID_AREA_X + GRID_AREA_W) && input.touch.touches[0].y <= 674)) {
        this->index(CELLS, 0);
        g_backupScrollEnabled = false;
        entryType(TITLES);
        MS::clearSelectedEntries();
        setPKSMBridgeFlag(false);
    }

    // Handle pressing X
    if (kdown & HidNpadButton_X) {
        if (g_backupScrollEnabled) {
            size_t index = this->index(CELLS);
            if (index > 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Delete selected backup?",
                    [this, index]() {
                        Title title;
                        TitleCatalog::get().getTitle(title, g_currentUId, rawIndex());
                        std::string path = title.fullPath(index);
                        io::deleteFolderRecursively((path + "/").c_str());
                        TitleCatalog::get().refreshDirectories(title.id());
                        BackupSizeCache::get().invalidate(title.id()); // a backup was removed
                        this->index(CELLS, index - 1);
                        this->removeOverlay();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }
        else {
            // Re-sort reorders every filtered index; a stale selection would
            // batch the wrong titles, so drop it.
            MS::clearSelectedEntries();
            TitleCatalog::get().rotateSortMode();
        }
    }

    // Handle pressing Y
    // Backup list active:   Deactivate backup list, select title, and
    //                       enable backup button
    // Backup list inactive: Select title and enable backup button
    if (kdown & HidNpadButton_Y) {
        if (g_backupScrollEnabled) {
            this->index(CELLS, 0);
            g_backupScrollEnabled = false;
        }
        entryType(TITLES);
        MS::addSelectedEntry(this->index(TITLES));
        setPKSMBridgeFlag(false);
    }

    // Handle holding Y
    if (kheld & HidNpadButton_Y && !(g_backupScrollEnabled)) {
        selectionTimer++;
    }
    else {
        selectionTimer = 0;
    }

    if (selectionTimer > 45) {
        MS::clearSelectedEntries();
        for (size_t i = 0, sz = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter); i < sz; i++) {
            MS::addSelectedEntry(i);
        }
        selectionTimer = 0;
    }

    // Handle pressing/touching L
    if (buttonBackup->released() || (kdown & HidNpadButton_L)) {
        if (MS::multipleSelectionEnabled()) {
            resetIndex(CELLS);
            std::vector<size_t> list = MS::selectedEntries();
            // Enqueue every selected save, then start the worker once. The job
            // drains them in order; the modal's overall bar tracks the batch and
            // update() raises the result overlay when it finishes.
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                // translate filtered index to raw index for multi-selection
                size_t raw = TitleCatalog::get().filteredToRawIndex(g_currentUId, mSaveTypeFilter, list.at(i));
                doBackup(raw, this->index(CELLS));
            }
            TransferJob::get().start();
            MS::clearSelectedEntries();
        }
        else if (g_backupScrollEnabled) {
            if (getPKSMBridgeFlag()) {
                if (this->index(CELLS) != 0) {
                    currentOverlay = std::make_shared<YesNoOverlay>(
                        *this, "Send save to PKSM?",
                        [this]() {
                            auto result = sendToPKSMBridge(rawIndex(), g_currentUId, this->index(CELLS));
                            if (std::get<0>(result)) {
                                currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                            }
                            else {
                                currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                            }
                        },
                        [this]() { this->removeOverlay(); });
                }
            }
            else {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Backup selected save?",
                    [this]() {
                        doBackup(rawIndex(), this->index(CELLS));
                        TransferJob::get().start();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }
    }

    // Handle pressing/touching R
    if (buttonRestore->released() || (kdown & HidNpadButton_R)) {
        if (g_backupScrollEnabled) {
            if (getPKSMBridgeFlag() && this->index(CELLS) != 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Receive save from PKSM?",
                    [this]() {
                        auto result = recvFromPKSMBridge(rawIndex(), g_currentUId, this->index(CELLS));
                        if (std::get<0>(result)) {
                            currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                        }
                        else {
                            currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                        }
                    },
                    [this]() { this->removeOverlay(); });
            }
            else {
                if (this->index(CELLS) != 0) {
                    requestRestoreSelected();
                }
            }
        }
    }
}

std::string MainScreen::nameFromCell(size_t index) const
{
    return backupList->cellName(index);
}

void MainScreen::entryType(entryType_t type_)
{
    type = type_;
}

void MainScreen::resetIndex(entryType_t type)
{
    if (type == TITLES) {
        hid.reset();
    }
    else {
        backupList->resetIndex();
    }
}

size_t MainScreen::index(entryType_t type) const
{
    return type == TITLES ? hid.fullIndex() : backupList->index();
}

void MainScreen::index(entryType_t type, size_t i)
{
    if (type == TITLES) {
        hid.page(i / hid.maxVisibleEntries());
        hid.index(i - hid.page() * hid.maxVisibleEntries());
    }
    else {
        backupList->setIndex(i);
    }
}

bool MainScreen::getPKSMBridgeFlag(void) const
{
    return Configuration::getInstance().isPKSMBridgeEnabled() ? pksmBridge : false;
}

void MainScreen::setPKSMBridgeFlag(bool f)
{
    pksmBridge = f;
}
