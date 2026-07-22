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
#include "MenuOverlay.hpp"
#include "ReceiveOverlay.hpp"
#include "ScriptPickerOverlay.hpp"
#include "ScriptRequestOverlays.hpp"
#include "SettingsScreen.hpp"
#include "backupsize.hpp"
#include "configuration.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include "paths.hpp"
#include "savedatasource.hpp"
#include "savekind.hpp"
#include "scriptcatalog.hpp"
#include "scriptrunner.hpp"
#include "shapes.hpp"
#include "titlecatalog.hpp"
#include "transfer.hpp"
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
    constexpr int SCRIPT_BTN_Y    = SETTINGS_BTN_Y - RAIL_ITEM_PITCH;
    constexpr int AVATAR_SIZE     = 44;
    constexpr int AVATAR_X        = 18;
    constexpr int AVATAR_Y        = 610;

    // Title grid: origin 100,76; 5 columns of 142px tiles, 12px gap →
    // 154px pitch. The grid scrolls vertically: 3 rows are fully visible and
    // a 4th row peeks past the viewport bottom (674, the hint bar top), faded
    // out by a gradient as a scroll affordance — 4 full rows would not fit
    // (76 + 3*154 + 142 = 680 > 674).
    constexpr int GRID_COLS        = 5;
    constexpr int GRID_FULL_ROWS   = 3; // rows the cursor can occupy
    constexpr int GRID_DRAW_ROWS   = 4; // fully visible rows + faded partial row
    constexpr int TILE             = 142;
    constexpr int TILE_PITCH       = 154;
    constexpr int BADGE_GLYPH_SIZE = 14; // ★/✓ badge marks, centered in the 24px badge
    constexpr int GRID_X0          = 100;
    constexpr int GRID_Y0          = 76;
    constexpr int GRID_AREA_X      = RAIL_W;
    constexpr int GRID_AREA_W      = 800;
    constexpr int GRID_BOTTOM      = UiKit::HINTBAR_Y; // grid viewport bottom edge
    // Bottom fade-out: transparent at the top of the partial row, opaque
    // background at the viewport bottom, so the cut-off row reads as "more
    // below" instead of a glitch.
    constexpr int GRID_FADE_Y = GRID_Y0 + GRID_FULL_ROWS * TILE_PITCH; // 538
    constexpr int GRID_FADE_H = GRID_BOTTOM - GRID_FADE_Y;             // 136

    // Held-D-pad repeat interval for the grid cursor (~156 ms, the same feel
    // as the old IHid-based selector).
    constexpr u64 GRID_REPEAT_TICKS = 3000000;

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
    // Wireless Send button: a full-width button stacked above Backup, in the slot
    // the list gives up (it shows one row fewer) when transfer is enabled. Receive
    // is a row inside the backup list, not a button here.
    constexpr int BTN_TRANSFER_Y = 466;

    std::string backupErrorMessage(io::BackupStage stage)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return i18n::t("outcome.mount");
            case io::BackupStage::DeleteDst:
                return i18n::t("outcome.delete_dst");
            case io::BackupStage::CreateDst:
                return i18n::t("outcome.create_dst");
            default:
                return i18n::t("outcome.backup_failed");
        }
    }

    std::string restoreErrorMessage(io::BackupStage stage)
    {
        switch (stage) {
            case io::BackupStage::OpenArchive:
                return i18n::t("outcome.mount");
            case io::BackupStage::DeleteDst:
                return i18n::t("outcome.delete_save");
            case io::BackupStage::Commit:
                return i18n::t("outcome.commit");
            default:
                return i18n::t("outcome.restore_failed");
        }
    }

    // A save-kind rail button: square (radius 0), filled accent when active,
    // faint fill otherwise, with the single kind letter over the small kind label.
    void drawRailItem(int y, const SaveKind& kind, bool active)
    {
        Shapes::fillRound(RAIL_ITEM_X, y, RAIL_ITEM, RAIL_ITEM, 0, active ? COLOR_ACCENT : COLOR_FILL1);
        Color fg = active ? COLOR_WHITE : COLOR_TEXT2;

        u32 gw, gh, lw, lh;
        Gfx::GetTextDimensions(16, kind.buttonLabel, &gw, &gh);
        Gfx::GetTextDimensions(9, kind.railLabel, &lw, &lh);
        const int stackH = (int)gh + 2 + (int)lh;
        const int top    = y + (RAIL_ITEM - stackH) / 2;
        Gfx::DrawText(16, RAIL_ITEM_X + (RAIL_ITEM - (int)gw) / 2, top, fg, kind.buttonLabel);
        Gfx::DrawText(9, RAIL_ITEM_X + (RAIL_ITEM - (int)lw) / 2, top + (int)gh + 2, fg, kind.railLabel);
    }

    // Size of the shoulder-key system glyph drawn inside an action button.
    constexpr int ACTION_GLYPH_SIZE = 26;

    // An action button: filled accent (Backup/Send) or outlined
    // (Restore/Receive), label + the shoulder-key system glyph centered as a
    // group. An empty `key` draws the label alone (the wireless buttons are
    // touch-only — every face/shoulder button is already bound). `enabled` false
    // greys the button out (faint fill, muted text) for a shown-but-inactive
    // action, e.g. Send while a non-sendable row is highlighted.
    void drawActionButton(int x, int y, const std::string& label, const std::string& key, bool filled, int w = BTN_W, bool enabled = true)
    {
        // Square buttons (radius 0), matching the 3DS action buttons and the
        // squared rail / rows.
        if (!enabled) {
            Shapes::fillRound(x, y, w, BTN_H, 0, COLOR_FILL1);
        }
        else if (filled) {
            Shapes::fillRound(x, y, w, BTN_H, 0, COLOR_ACCENT);
        }
        else {
            Shapes::strokeRound(x, y, w, BTN_H, 0, 2, COLOR_STROKE3);
        }

        const Color fg = !enabled ? COLOR_TEXT3 : (filled ? COLOR_WHITE : COLOR_TEXT);

        u32 lw, lh;
        Gfx::GetTextDimensions(16, label.c_str(), &lw, &lh);
        if (key.empty()) {
            Gfx::DrawText(16, x + (w - (int)lw) / 2, y + (BTN_H - (int)lh) / 2, fg, label.c_str());
            return;
        }

        const std::string glyph = UiKit::buttonGlyph(key);
        u32 gw, gh;
        Gfx::GetTextDimensions(ACTION_GLYPH_SIZE, glyph.c_str(), &gw, &gh);
        const int gap   = 10;
        const int group = (int)lw + gap + (int)gw;
        const int sx    = x + (w - group) / 2;
        Gfx::DrawText(16, sx, y + (BTN_H - (int)lh) / 2, fg, label.c_str());
        Gfx::DrawText(ACTION_GLYPH_SIZE, sx + (int)lw + gap, y + (BTN_H - (int)gh) / 2, fg, glyph.c_str());
    }

    // Maps a failed network send to a user-facing message (mirrors the 3DS
    // OutcomeMessages::sendError). EmptyBackup and Cancelled are handled by the
    // caller as neutral info, so they only appear here as a fallback.
    std::string sendErrorMessage(const Transfer::SendOutcome& outcome)
    {
        switch (outcome.stage) {
            case Transfer::SendStage::PayloadTooLarge:
                return i18n::t("outcome.send_too_large");
            case Transfer::SendStage::Zip:
                return i18n::t("outcome.send_zip");
            case Transfer::SendStage::Socket:
                return i18n::t("outcome.send_socket");
            case Transfer::SendStage::Resolve:
                return i18n::t("outcome.send_resolve");
            case Transfer::SendStage::Connect:
                return i18n::t("outcome.send_connect");
            case Transfer::SendStage::Response:
                return outcome.detail.empty() ? i18n::t("outcome.send_no_response") : i18n::t("outcome.send_receiver_error", {outcome.detail});
            case Transfer::SendStage::Send:
                return i18n::t("outcome.send_interrupted");
            default:
                return i18n::t("outcome.send_failed");
        }
    }
}

MainScreen::MainScreen(const InputState&)
{
    selectionTimer = 0;
    snprintf(ver, sizeof(ver), "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    backupList    = std::make_unique<BackupList>(COL_X, LIST_Y, COL_W, LIST_ROWS * BackupList::ROW_PITCH, LIST_ROWS);
    buttonBackup  = std::make_unique<Clickable>(COL_X, BTN_BACKUP_Y, BTN_W, BTN_H, COLOR_ACCENT, COLOR_WHITE, i18n::t("main.backup"), true);
    buttonRestore = std::make_unique<Clickable>(COL_X, BTN_RESTORE_Y, BTN_W, BTN_H, COLOR_SURFACE, COLOR_TEXT, i18n::t("main.restore"), true);
    // Contextual slot stacked above Backup (Receive lives inside the backup
    // list, mirroring the 3DS layout): Send when an existing backup is
    // highlighted, Scripts otherwise. The Clickable only hit-tests touches;
    // the label drawn comes from drawActionButton.
    buttonSend = std::make_unique<Clickable>(COL_X, BTN_TRANSFER_Y, BTN_W, BTN_H, COLOR_ACCENT, COLOR_WHITE, i18n::t("transfer.send"), true);

    // The rail Clickables exist only so released() can hit-test touches; the
    // rail is drawn by drawRailItem, not by Clickable::draw.
    for (int k = 0; k < 4; k++) {
        int y            = RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * k;
        filterButtons[k] = std::make_unique<Clickable>(RAIL_ITEM_X, y, RAIL_ITEM, RAIL_ITEM, COLOR_FILL1, COLOR_TEXT2, "", true);
    }
    scriptButton   = std::make_unique<Clickable>(RAIL_ITEM_X, SCRIPT_BTN_Y, RAIL_ITEM, RAIL_ITEM, COLOR_FILL1, COLOR_TEXT2, "", true);
    settingsButton = std::make_unique<Clickable>(RAIL_ITEM_X, SETTINGS_BTN_Y, RAIL_ITEM, RAIL_ITEM, COLOR_FILL1, COLOR_TEXT2, "", true);
    avatarButton   = std::make_unique<Clickable>(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, COLOR_TILE, COLOR_TEXT2, "", true);
}

int MainScreen::selectorX(size_t i) const
{
    const int col = (int)(i % GRID_COLS);
    return GRID_X0 + col * TILE_PITCH;
}

int MainScreen::selectorY(size_t i) const
{
    const int row = (int)(i / GRID_COLS);
    return GRID_Y0 + (row - mScrollRow) * TILE_PITCH;
}

void MainScreen::scrollToCursor(size_t count)
{
    const int totalRows = count == 0 ? 0 : (int)((count - 1) / GRID_COLS) + 1;
    const int maxScroll = std::max(0, totalRows - GRID_FULL_ROWS);
    const int row       = count == 0 ? 0 : (int)(mCursor / GRID_COLS);
    if (row < mScrollRow) {
        mScrollRow = row;
    }
    else if (row > mScrollRow + GRID_FULL_ROWS - 1) {
        mScrollRow = row - (GRID_FULL_ROWS - 1);
    }
    mScrollRow = std::clamp(mScrollRow, 0, maxScroll);
}

void MainScreen::updateGridCursor(const InputState& input, size_t count)
{
    if (count == 0) {
        mCursor    = 0;
        mScrollRow = 0;
        return;
    }
    if (mCursor >= count) {
        mCursor = count - 1;
    }

    const u64 now = armGetSystemTick();
    // A fresh press moves immediately; a held direction repeats every
    // GRID_REPEAT_TICKS (which also serves as the initial delay).
    auto pressed = [&](u64 mask) { return (input.kDown & mask) || ((input.kHeld & mask) && now > mLastMoveTick + GRID_REPEAT_TICKS); };

    const size_t lastRow = (count - 1) / GRID_COLS;
    const size_t row     = mCursor / GRID_COLS;
    bool moved           = false;

    if (pressed(HidNpadButton_AnyUp)) {
        if (row > 0) {
            mCursor -= GRID_COLS;
            moved = true;
        }
    }
    else if (pressed(HidNpadButton_AnyDown)) {
        if (row < lastRow) {
            mCursor = std::min(mCursor + GRID_COLS, count - 1);
            moved   = true;
        }
    }
    else if (pressed(HidNpadButton_AnyLeft)) {
        // Left at column 0 is the sidebar-entry gesture, handled by the
        // caller before this runs.
        if (mCursor % GRID_COLS != 0) {
            mCursor--;
            moved = true;
        }
    }
    else if (pressed(HidNpadButton_AnyRight)) {
        // Row-major continuation: right on the last column flows to the next
        // row's first tile.
        if (mCursor + 1 < count) {
            mCursor++;
            moved = true;
        }
    }

    if (moved) {
        mLastMoveTick = now;
    }
    scrollToCursor(count);
}

void MainScreen::setSaveTypeFilter(saveTypeFilter_t filter)
{
    if (mSaveTypeFilter == filter)
        return;
    mSaveTypeFilter = filter;
    this->index(TITLES, 0);
    this->index(CELLS, 0);
    backupScrollEnabled = false;
    MS::clearSelectedEntries();
}

void MainScreen::draw() const
{
    const size_t filteredCnt = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
    auto selEnt              = MS::selectedEntries();

    Gfx::ClearScreen(COLOR_BG);

    // Resolve the selected title once (cached): its name feeds the top bar and
    // its detail feeds the right panel.
    std::string gameName;
    if (filteredCnt > 0) {
        backupList->refreshSelected(g_currentUId, mSaveTypeFilter, mCursor, TitleCatalog::get().generation());
        gameName = backupList->title().displayName();
    }

    // ---- Top bar ----
    u32 logoW, logoH;
    Gfx::GetTextDimensions(22, "checkpoint", &logoW, &logoH);
    Gfx::DrawText(22, 24, (TOPBAR_H - (int)logoH) / 2, COLOR_TEXT, "checkpoint");

    // Plain version label just right of the logo (no chip), e.g. "v4.0.0".
    u32 verW, verH;
    Gfx::GetTextDimensions(11, ver, &verW, &verH);
    const int verX = 24 + (int)logoW + 12;
    Gfx::DrawText(11, verX, (TOPBAR_H - (int)verH) / 2 + 3, COLOR_TEXT2, ver);
    const int topbarLeftEnd = verX + (int)verW;

    if (!gameName.empty()) {
        const int avail = 1256 - topbarLeftEnd - 24;
        std::string t   = gameName;
        u32 tw, th;
        Gfx::GetTextDimensions(17, t.c_str(), &tw, &th);
        if ((int)tw > avail) {
            t = trimToFit(t, avail, 17);
            Gfx::GetTextDimensions(17, t.c_str(), &tw, &th);
        }
        Gfx::DrawText(17, 1256 - (int)tw, (TOPBAR_H - (int)th) / 2, COLOR_TEXT, t.c_str());
    }

    // ---- Frame hairlines ----
    Gfx::DrawRect(0, TOPBAR_H, 1280, 1, COLOR_STROKE1);
    Gfx::DrawRect(RAIL_W, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);
    Gfx::DrawRect(PANEL_X, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);

    // ---- Left rail ----
    for (int k = 0; k < 4; k++) {
        drawRailItem(RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * k, SaveKind::all()[k], mSaveTypeFilter == static_cast<saveTypeFilter_t>(k));
    }
    // Sidebar cursor spans the 4 save-kind buttons plus the scripts glyph
    // (index 4) and settings gear (index 5), so both can be reached by pressing
    // Down past SYSTEM.
    if (sidebarFocused) {
        const int selY = sidebarCursor < 4 ? RAIL_ITEM_Y0 + RAIL_ITEM_PITCH * sidebarCursor : (sidebarCursor == 4 ? SCRIPT_BTN_Y : SETTINGS_BTN_Y);
        Shapes::focusRing(RAIL_ITEM_X, selY, RAIL_ITEM, RAIL_ITEM, 14, COLOR_ACCENT);
    }

    Shapes::fillRound(RAIL_ITEM_X, SCRIPT_BTN_Y, RAIL_ITEM, RAIL_ITEM, 0, COLOR_FILL1);
    {
        u32 sw, sh;
        Gfx::GetTextDimensions(22, "", &sw, &sh);
        Gfx::DrawText(22, RAIL_ITEM_X + (RAIL_ITEM - (int)sw) / 2, SCRIPT_BTN_Y + (RAIL_ITEM - (int)sh) / 2 + 3, COLOR_TEXT2, "");
    }

    Shapes::fillRound(RAIL_ITEM_X, SETTINGS_BTN_Y, RAIL_ITEM, RAIL_ITEM, 0, COLOR_FILL1);
    {
        u32 gw, gh;
        Gfx::GetTextDimensions(22, "", &gw, &gh);
        Gfx::DrawText(22, RAIL_ITEM_X + (RAIL_ITEM - (int)gw) / 2, SETTINGS_BTN_Y + (RAIL_ITEM - (int)gh) / 2 + 3, COLOR_TEXT2, "");
    }

    Shapes::fillRound(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, AVATAR_SIZE / 2, COLOR_TILE);
    if (Account::icon(g_currentUId) != NULL) {
        Gfx::DrawImageScale(Account::icon(g_currentUId), AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE);
    }
    Shapes::strokeRound(AVATAR_X, AVATAR_Y, AVATAR_SIZE, AVATAR_SIZE, AVATAR_SIZE / 2, 1, COLOR_STROKE2);
    {
        std::string username = Account::shortName(g_currentUId);
        u32 uw, uh;
        Gfx::GetTextDimensions(10, username.c_str(), &uw, &uh);
        Gfx::DrawText(10, RAIL_W / 2 - (int)uw / 2, AVATAR_Y + AVATAR_SIZE + 4, COLOR_TEXT2, username.c_str());
    }

    // ---- Title grid ----
    // Rows mScrollRow..mScrollRow+3: three fully visible plus the faded
    // partial row peeking past the viewport bottom.
    const size_t drawBegin = (size_t)mScrollRow * GRID_COLS;
    const size_t drawEnd   = std::min(filteredCnt, drawBegin + (size_t)(GRID_COLS * GRID_DRAW_ROWS));
    for (size_t k = drawBegin; k < drawEnd; k++) {
        const int tx = selectorX(k), ty = selectorY(k);
        Shapes::cardRound(tx, ty, TILE, TILE, 14, COLOR_TILE, COLOR_STROKE2, 1);
        Texture* smallIcon = TitleCatalog::get().filteredSmallIcon(g_currentUId, mSaveTypeFilter, k);
        if (smallIcon != NULL) {
            Gfx::DrawImageScale(smallIcon, tx, ty, TILE, TILE);
        }

        const bool selected = !selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end();
        const bool favorite = TitleCatalog::get().filteredFavorite(g_currentUId, mSaveTypeFilter, k);
        if (selected || favorite) {
            const int bx = tx + TILE - 8 - 24, by = ty + 8;
            Shapes::fillRound(bx, by, 24, 24, 8, selected ? COLOR_ACCENT : makeColor(16, 16, 20, 191));
            const char* glyph = selected ? "\uE14B" : "★"; // ✓ multi-select · ★ favorite
            const Color gcol  = selected ? COLOR_WHITE : COLOR_GOLD;
            u32 gw = 0, gh = 0;
            Gfx::GetTextDimensions(BADGE_GLYPH_SIZE, glyph, &gw, &gh);
            Gfx::DrawText(BADGE_GLYPH_SIZE, bx + (24 - (int)gw) / 2, by + (24 - (int)gh) / 2 + 1, gcol, glyph);
        }
    }

    // Focus ring on the selected tile (hidden while the rail owns the cursor).
    if (filteredCnt > 0 && !sidebarFocused) {
        Shapes::focusRing(selectorX(mCursor), selectorY(mCursor), TILE, TILE, 14, COLOR_ACCENT);
    }

    // Fade the partial fourth row into the background (only when it holds
    // tiles); the opaque hint bar hides the few pixels that spill past 674.
    if (drawEnd > drawBegin + (size_t)(GRID_COLS * GRID_FULL_ROWS)) {
        const Color fadeTop = makeColor(COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 0);
        Gfx::DrawRectGradientV(GRID_AREA_X + 1, GRID_FADE_Y, GRID_AREA_W - 1, GRID_FADE_H, fadeTop, COLOR_BG);
    }

    // ---- Right panel ----
    if (filteredCnt > 0) {
        Title& title = backupList->title();

        Shapes::cardRound(COL_X, 76, HEADER_ICON, HEADER_ICON, 16, COLOR_TILE, COLOR_STROKE2, 1);
        if (TitleCatalog::get().iconFor(title.id()) != NULL) {
            Gfx::DrawImageScale(TitleCatalog::get().iconFor(title.id()), COL_X, 76, HEADER_ICON, HEADER_ICON);
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
        Gfx::GetTextDimensions(12, "Ag", NULL, &h1);
        Gfx::GetTextDimensions(13, "Ag", NULL, &h2);
        Gfx::GetTextDimensions(11, "Ag", NULL, &h3, FontFamily::Mono);
        int stackH = (int)h1 + 4 + (int)h3 + (hasPlay ? 4 + (int)h2 : 0);
        int ty     = 76 + (HEADER_ICON - stackH) / 2;

        Gfx::DrawText(12, infoX, ty, COLOR_TEXT2, trimToFit(line1, infoW, 12).c_str());
        ty += (int)h1 + 4;
        if (hasPlay) {
            u32 lblW;
            std::string playLbl = i18n::t("main.play_time") + " ";
            Gfx::GetTextDimensions(13, playLbl.c_str(), &lblW, NULL);
            Gfx::DrawText(13, infoX, ty, COLOR_TEXT2, playLbl.c_str());
            Gfx::DrawText(13, infoX + (int)lblW, ty, COLOR_TEXT, title.playTime().c_str());
            ty += (int)h2 + 4;
        }
        Gfx::DrawText(11, infoX, ty, COLOR_TEXT3, idStr.c_str(), FontFamily::Mono);

        // Backups header row.
        const int headerY      = 184;
        std::string countLabel = i18n::t("main.backups_count", {std::to_string(backupList->backupCount())});
        u32 clH;
        Gfx::GetTextDimensions(11, countLabel.c_str(), NULL, &clH);
        UiKit::drawSectionLabel(COL_X, headerY, countLabel.c_str());

        {
            // Total on-disk size of this title's backups, right-aligned against
            // the "BACKUPS · N" label.
            const std::string& totalSize = backupList->totalSizeString();
            if (!totalSize.empty()) {
                u32 tw, th;
                Gfx::GetTextDimensions(11, totalSize.c_str(), &tw, &th, FontFamily::Mono);
                Gfx::DrawText(11, COL_X + COL_W - (int)tw, headerY + ((int)clH - (int)th) / 2, COLOR_TEXT2, totalSize.c_str(), FontFamily::Mono);
            }
        }

        backupList->draw(backupScrollEnabled);

        if (MS::multipleSelectionEnabled()) {
            // Multi-select is a batch backup only (no restore): show a single
            // button counting the selected titles, wired to the same L handler.
            const size_t n = selEnt.size();
            const std::string lbl =
                n == 1 ? i18n::t("main.backup_n_title", {std::to_string(n)}) : i18n::t("main.backup_n_titles", {std::to_string(n)});
            drawActionButton(COL_X, BTN_BACKUP_Y, lbl, "L", true);
        }
        else {
            // The slot above Backup/Restore is always Send on ZR (greyed until a
            // highlighted existing backup can actually be sent) so it never swaps
            // identity; Scripts now lives in the Minus menu.
            const bool sendCtx = Configuration::getInstance().isTransferEnabled() && backupScrollEnabled && backupList->index() != 0 &&
                                 !backupList->isReceiveRow(backupList->index());
            drawActionButton(COL_X, BTN_TRANSFER_Y, i18n::t("transfer.send"), "ZR", true, BTN_W, sendCtx);
            drawActionButton(COL_X, BTN_BACKUP_Y, i18n::t("main.backup"), "L", true);
            drawActionButton(COL_X, BTN_RESTORE_Y, i18n::t("main.restore"), "R", false);
        }
    }
    else {
        std::string emptyMsg = i18n::t(SaveKind::of(mSaveTypeFilter).emptyMsg);
        u32 emptyW, emptyH;
        Gfx::GetTextDimensions(18, emptyMsg.c_str(), &emptyW, &emptyH);
        Gfx::DrawText(18, GRID_AREA_X + (GRID_AREA_W - (int)emptyW) / 2, (720 - (int)emptyH) / 2, COLOR_TEXT2, emptyMsg.c_str());
    }

    // ---- Hint bar ----
    // Minus opens the tools menu (see the class note); no help overlay in this build.
    UiKit::drawHintBar({
        {"A", i18n::t("hint.select")},
        {"B", i18n::t("hint.back")},
        {"X", i18n::t("hint.sort")},
        {"Y", i18n::t("hint.multiselect")},
        {"-", i18n::t("hint.menu")},
    });

    // ---- Script status ----
    // While a script runs, a small modal shows its name and last gui_status
    // line; the script's own requests raise full overlays on top of this.
    if (ScriptRunner::get().active()) {
        Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);

        const int mw = 540, mh = 160;
        const int mx = (1280 - mw) / 2, my = (720 - mh) / 2;
        Shapes::cardRound(mx, my, mw, mh, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

        std::string titleStr = i18n::t("scripts.running", {ScriptRunner::get().scriptName()});
        u32 tw, th;
        Gfx::GetTextDimensions(20, titleStr.c_str(), &tw, &th);
        Gfx::DrawText(20, mx + (mw - (int)tw) / 2, my + 36, COLOR_TEXT, titleStr.c_str());

        std::string status = ScriptRunner::get().bridge().statusText();
        if (!status.empty()) {
            status = trimToFit(status, mw - 48, 15);
            u32 sw;
            Gfx::GetTextDimensions(15, status.c_str(), &sw, NULL);
            Gfx::DrawText(15, mx + (mw - (int)sw) / 2, my + 36 + (int)th + 18, COLOR_TEXT2, status.c_str());
        }

        // Hold-B abort hint (same wording as the transfer modal).
        std::string hint = ScriptRunner::get().cancelRequested() ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint");
        u32 hw;
        Gfx::GetTextDimensions(15, hint.c_str(), &hw, NULL);
        Gfx::DrawText(15, mx + (mw - (int)hw) / 2, my + mh - 34, COLOR_TEXT2, hint.c_str());
    }

    // ---- Transfer modal ----
    const TransferSnapshot transfer = TransferStatus::snapshot();
    if (transfer.active) {
        Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);

        // Network send/receive: a single byte-progress bar plus a hold-B-to-cancel
        // hint. (During a receive the ReceiveOverlay draws its own card on top of
        // this; this is what shows for a send, which has no overlay.)
        if (transfer.kind == TransferKind::Network) {
            const int mw = 540, mh = 200;
            const int mx = (1280 - mw) / 2, my = (720 - mh) / 2;
            Shapes::cardRound(mx, my, mw, mh, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

            std::string titleStr = i18n::t("main.in_progress", {transfer.mode.empty() ? i18n::t("main.transferring") : transfer.mode});
            u32 tw, th;
            Gfx::GetTextDimensions(20, titleStr.c_str(), &tw, &th);
            Gfx::DrawText(20, mx + (mw - (int)tw) / 2, my + 24, COLOR_TEXT, titleStr.c_str());

            const int barX = mx + 24, barW = mw - 48, barH = 18;
            const int barY = my + 84;
            float frac     = transfer.bytesTotal > 0 ? (float)transfer.bytesDone / (float)transfer.bytesTotal : 0.0f;
            if (frac > 1.0f) {
                frac = 1.0f;
            }
            Shapes::fillRound(barX, barY, barW, barH, 0, COLOR_FILL2);
            if (frac > 0.0f) {
                Shapes::fillRound(barX, barY, (int)(barW * frac), barH, 0, COLOR_ACCENT);
            }
            std::string mb = TransferStatus::bytesToMB(transfer.bytesDone, transfer.bytesTotal);
            char pctStr[8];
            snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(frac * 100));
            u32 pw;
            Gfx::GetTextDimensions(15, pctStr, &pw, NULL);
            Gfx::DrawText(15, barX, barY + barH + 6, COLOR_TEXT2, mb.c_str());
            Gfx::DrawText(15, barX + barW - (int)pw, barY + barH + 6, COLOR_TEXT, pctStr);

            std::string hint = UiKit::buttonGlyph("B") + " " +
                               (TransferStatus::cancelRequested() ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint"));
            u32 hw;
            Gfx::GetTextDimensions(14, hint.c_str(), &hw, NULL);
            Gfx::DrawText(14, mx + mw - (int)hw - 16, my + mh - 30, COLOR_TEXT2, hint.c_str());
            return;
        }

        const bool multiSelect = transfer.saveTotal > 1;
        const int mx = 370, mw = 540;
        const int mh = multiSelect ? 290 : 230;
        const int my = multiSelect ? 230 : 260;
        Shapes::cardRound(mx, my, mw, mh, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

        std::string titleStr = i18n::t("main.in_progress", {transfer.mode.empty() ? i18n::t("main.copying") : transfer.mode});
        u32 title_w, title_h;
        Gfx::GetTextDimensions(20, titleStr.c_str(), &title_w, &title_h);
        Gfx::DrawText(20, mx + (mw - (int)title_w) / 2, my + 16, COLOR_TEXT, titleStr.c_str());

        if (transfer.cancellable) {
            const std::string hint = UiKit::buttonGlyph("B") + " " + i18n::t("main.to_cancel");
            u32 hint_w;
            Gfx::GetTextDimensions(14, hint.c_str(), &hint_w, NULL);
            Gfx::DrawText(14, mx + mw - (int)hint_w - 16, my + mh - 26, COLOR_TEXT2, hint.c_str());
        }

        u32 fname_w, fname_h;
        std::string fname = trimToFit(transfer.currentFile, mw - 40, 15);
        Gfx::GetTextDimensions(15, fname.c_str(), &fname_w, &fname_h);
        Gfx::DrawText(15, mx + (mw - (int)fname_w) / 2, my + 16 + (int)title_h + 8, COLOR_TEXT2, fname.c_str());

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
            Gfx::GetTextDimensions(15, rightLabel, &right_w, NULL);
            Gfx::DrawText(15, barX, y + barH + 6, COLOR_TEXT2, leftLabel);
            Gfx::DrawText(15, barX + barW - (int)right_w, y + barH + 6, COLOR_TEXT, rightLabel);
        };

        int barY = my + 108;
        if (multiSelect) {
            float overallProgress       = (float)transfer.saveCount / (float)transfer.saveTotal;
            std::string overallCountStr = i18n::t("main.save_n", {std::to_string(transfer.saveCount + 1), std::to_string(transfer.saveTotal)});
            char overallPctStr[8];
            snprintf(overallPctStr, sizeof(overallPctStr), "%d%%", (int)(overallProgress * 100));
            drawProgressBar(barY, overallProgress, overallCountStr.c_str(), overallPctStr);
            barY += 52;
        }

        float progress       = (transfer.copyTotal > 0) ? (float)transfer.copyCount / (float)transfer.copyTotal : 0.0f;
        std::string countStr = i18n::t("main.file_n", {std::to_string(transfer.copyCount), std::to_string(transfer.copyTotal)});
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%", (int)((progress > 1.0f ? 1.0f : progress) * 100));
        drawProgressBar(barY, progress, countStr.c_str(), pctStr);
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
        if (result->send) {
            // A network send: map the outcome to a message. EmptyBackup/Cancelled
            // are neutral info; every other stage is an error.
            if (result->send->stage == Transfer::SendStage::EmptyBackup) {
                currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.backup_empty"));
            }
            else if (result->send->stage == Transfer::SendStage::Cancelled) {
                currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("transfer.cancelled"));
            }
            else if (result->send->ok) {
                blinkLed(4);
                currentOverlay = std::make_shared<InfoOverlay>(*this, result->successMsg);
            }
            else {
                currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, sendErrorMessage(*result->send));
            }
            return;
        }
        if (result->cancelled) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.backup_cancelled"));
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
        TransferSnapshot ts = TransferStatus::snapshot();
        if (ts.active && ts.kind == TransferKind::Network) {
            // A network send is cancelled by holding B (parity with the receive
            // overlay); a stray tap won't kill a long transfer.
            if (input.kHeld & HidNpadButton_B) {
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
            if ((input.kDown & HidNpadButton_B) && ts.cancellable) {
                TransferJob::get().requestCancel();
            }
        }
        return;
    }

    // Script outcome / activity. While a script runs this screen only pumps its
    // UI-bridge requests; normal input is ignored (same policy as TransferJob).
    if (auto script = ScriptRunner::get().takeResult()) {
        appletEndBlockingHomeButton();
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

    updateSelector(input);
    handleEvents(input);
}

void MainScreen::startScriptPicker(void)
{
    if (TransferJob::get().active() || ScriptRunner::get().active()) {
        return;
    }

    // The highlighted grid title (if any) picks the specific-script folder and
    // becomes the script's selected title.
    Title title;
    const bool hasTitle = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter) > 0;
    if (hasTitle) {
        TitleCatalog::get().getFilteredTitle(title, g_currentUId, mSaveTypeFilter, mCursor);
    }
    const u64 titleId = hasTitle ? title.id() : 0;

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
        *this, std::move(entries), hasTitle ? title.displayName() : "", [this, hasTitle, titleId](const ScriptCatalog::Entry& entry) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, i18n::t("scripts.confirm_run", {entry.name}),
                [this, entry, hasTitle, titleId]() {
                    this->removeOverlay();
                    std::string idHex = hasTitle ? StringUtils::format("%016llX", (unsigned long long)titleId) : "";
                    // HOME stays blocked for the whole run (PKSM precedent): picoc
                    // cannot be preempted, so a buggy script requires a reboot.
                    appletBeginBlockingHomeButton(0);
                    if (!ScriptRunner::get().start(entry.path, entry.name, std::move(idHex))) {
                        appletEndBlockingHomeButton();
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
        if (mCursor >= count) {
            mCursor = count > 0 ? count - 1 : 0;
        }
        scrollToCursor(count); // fewer rows may remain than the window showed
        MS::clearSelectedEntries();
    }

    if (!backupScrollEnabled) {
        size_t count = TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
        if (sidebarFocused && (input.kDown & (HidNpadButton_Right | HidNpadButton_B))) {
            sidebarFocused   = false;
            sidebarExitFrame = true;
        }
        else if (sidebarFocused) {
            // don't update title grid while sidebar is focused
        }
        else if (sidebarExitFrame) {
            // skip grid-cursor updates until Right/B is released so the held key doesn't move the title cursor
            if (!(input.kHeld & (HidNpadButton_AnyRight | HidNpadButton_B))) {
                sidebarExitFrame = false;
            }
        }
        else if ((input.kDown & HidNpadButton_Left) && mCursor % GRID_COLS == 0) {
            sidebarFocused = true;
            sidebarCursor  = static_cast<int>(mSaveTypeFilter);
        }
        else {
            updateGridCursor(input, count);
        }

        // Touch selection over the visible tiles (including the faded partial
        // row — tapping it selects the tile and scrolls it into full view).
        if (input.touch.count > 0) {
            const size_t visBegin = (size_t)mScrollRow * GRID_COLS;
            const size_t visEnd   = std::min(count, visBegin + (size_t)(GRID_COLS * GRID_DRAW_ROWS));
            for (size_t k = visBegin; k < visEnd; k++) {
                const int x = selectorX(k);
                const int y = selectorY(k);
                if (input.touch.touches[0].x >= x && input.touch.touches[0].x <= x + TILE && input.touch.touches[0].y >= y &&
                    input.touch.touches[0].y <= y + TILE && input.touch.touches[0].y < GRID_BOTTOM) {
                    mCursor = k;
                    scrollToCursor(count);
                    break;
                }
            }
        }

        backupList->resetIndex();
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

    std::string successMsg = usedKeyboardFallback ? i18n::t("main.backup_success_fallback") : i18n::t("main.backup_success");
    TransferJob::get().enqueueBackup(std::move(title), *dst, std::move(successMsg));
}

void MainScreen::doRestore(size_t rawIdx, size_t cellIndex)
{
    Title title;
    TitleCatalog::get().getTitle(title, g_currentUId, rawIdx);
    std::string name = nameFromCell(cellIndex);
    std::string src  = title.fullPath(cellIndex) + "/";
    removeOverlay();

    TransferJob::get().enqueueRestore(std::move(title), std::move(src), i18n::t("main.restore_success", {name}));
}

void MainScreen::requestRestoreSelected(void)
{
    // A restore overwrites the on-console save, so confirm by default; the
    // confirm-restore toggle (Settings > General) can skip the prompt.
    if (Configuration::getInstance().isConfirmRestoreEnabled()) {
        currentOverlay = std::make_shared<YesNoOverlay>(
            *this, i18n::t("main.confirm_restore"),
            [this]() {
                doRestore(rawIndex(), backupList->rowToCell(this->index(CELLS)));
                TransferJob::get().start();
            },
            [this]() { this->removeOverlay(); });
    }
    else {
        doRestore(rawIndex(), backupList->rowToCell(this->index(CELLS)));
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

    // Minus opens the tools menu (Scripts / Settings). Guarded so you can't
    // navigate away mid-copy.
    if ((kdown & HidNpadButton_Minus) && !TransferJob::get().active()) {
        std::vector<MenuOverlay::Item> items = {
            {i18n::t("main.scripts"), [this]() { startScriptPicker(); }},
            {i18n::t("settings.title"), []() { g_pendingScreen = std::make_shared<SettingsScreen>(g_screen); }},
        };
        currentOverlay = std::make_shared<MenuOverlay>(*this, i18n::t("menu.title"), std::move(items));
        return;
    }

    // The rail scripts glyph opens the picker straight away, mirroring the gear.
    if (scriptButton->released() && !TransferJob::get().active()) {
        startScriptPicker();
        return;
    }

    // The rail gear is an unambiguous Settings icon, so it still jumps straight
    // there; the swap itself is deferred to main() via g_pendingScreen.
    if (settingsButton->released() && !TransferJob::get().active()) {
        g_pendingScreen = std::make_shared<SettingsScreen>(g_screen);
        return;
    }

    // handle sidebar D-pad navigation. Cursor 0..3 = save-kind buttons, 4 =
    // scripts glyph, 5 = settings gear (so Down from SYSTEM reaches them, Up from
    // USER wraps to the gear).
    if (sidebarFocused) {
        if (kdown & HidNpadButton_Up) {
            sidebarCursor = sidebarCursor > 0 ? sidebarCursor - 1 : 5;
        }
        else if (kdown & HidNpadButton_Down) {
            sidebarCursor = sidebarCursor < 5 ? sidebarCursor + 1 : 0;
        }
        if (kdown & HidNpadButton_A) {
            if (sidebarCursor == 5) {
                if (!TransferJob::get().active()) {
                    g_pendingScreen = std::make_shared<SettingsScreen>(g_screen);
                }
            }
            else if (sidebarCursor == 4) {
                if (!TransferJob::get().active()) {
                    startScriptPicker();
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

    // The account picker rebuilds the title list, so it stays out of the backup
    // list: switching users there would leave the open list pointing at a title of
    // the previous account (same as the avatar button, which is title-grid only).
    if (mSaveTypeFilter == FILTER_SAVES && !backupScrollEnabled) {
        // ZR is the contextual Send/Scripts button, so the account picker
        // lives on ZL alone.
        if (kdown & HidNpadButton_ZL) {
            while ((g_currentUId = Account::selectAccount()) == 0)
                ;
            this->index(TITLES, 0);
            this->index(CELLS, 0);
            MS::clearSelectedEntries(); // filtered indices belong to the old account
        }
    }

    // Tapping the avatar opens the account picker. Release-triggered (like the
    // rail buttons) so the applet fires once on lift instead of relaunching every
    // frame the finger is held down on it.
    if (!backupScrollEnabled && avatarButton->released()) {
        while ((g_currentUId = Account::selectAccount()) == 0)
            ;
        this->index(TITLES, 0);
        this->index(CELLS, 0);
        MS::clearSelectedEntries(); // filtered indices belong to the old account
    }

    // Handle touching the backup list / panel region
    if (input.touch.count > 0 && input.touch.touches[0].x > COL_X && input.touch.touches[0].x < COL_X + COL_W && input.touch.touches[0].y > LIST_Y &&
        input.touch.touches[0].y < BTN_BACKUP_Y) {
        // Activate backup list only if multiple selections are not enabled
        if (!MS::multipleSelectionEnabled()) {
            backupScrollEnabled = true;
            entryType(CELLS);
        }
    }

    // Handle pressing A
    // Backup list active:   Backup/Restore
    // Backup list inactive: Activate backup list only if multiple
    //                       selections are enabled
    if ((kdown & HidNpadButton_A) && TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter) > 0) {
        // If backup list is active...
        if (backupScrollEnabled) {
            const size_t row = this->index(CELLS);
            // "New..." row → backup; the wireless "Receive" row → start receiver;
            // any existing backup row → restore.
            if (0 == row) {
                doBackup(rawIndex(), 0);
                TransferJob::get().start();
            }
            else if (backupList->isReceiveRow(row)) {
                startTransferReceive();
            }
            else {
                requestRestoreSelected();
            }
        }
        else {
            // Activate backup list only if multiple selections are not enabled
            if (!MS::multipleSelectionEnabled()) {
                backupScrollEnabled = true;
                entryType(CELLS);
            }
        }
    }

    // Handle pressing B
    if ((kdown & HidNpadButton_B) || (input.touch.count > 0 && input.touch.touches[0].x >= GRID_AREA_X &&
                                         input.touch.touches[0].x <= (GRID_AREA_X + GRID_AREA_W) && input.touch.touches[0].y <= 674)) {
        this->index(CELLS, 0);
        backupScrollEnabled = false;
        entryType(TITLES);
        MS::clearSelectedEntries();
    }

    // Handle pressing X
    if (kdown & HidNpadButton_X) {
        if (backupScrollEnabled) {
            size_t index = this->index(CELLS); // display row
            if (index > 0 && !backupList->isReceiveRow(index)) {
                const size_t cell = backupList->rowToCell(index);
                currentOverlay    = std::make_shared<YesNoOverlay>(
                    *this, i18n::t("main.confirm_delete"),
                    [this, index, cell]() {
                        Title title;
                        TitleCatalog::get().getTitle(title, g_currentUId, rawIndex());
                        std::string path = title.fullPath(cell);
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
        if (backupScrollEnabled) {
            this->index(CELLS, 0);
            backupScrollEnabled = false;
        }
        entryType(TITLES);
        MS::addSelectedEntry(this->index(TITLES));
    }

    // Handle holding Y
    if (kheld & HidNpadButton_Y && !(backupScrollEnabled)) {
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
        else if (backupScrollEnabled && !backupList->isReceiveRow(this->index(CELLS))) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, i18n::t("main.confirm_backup_save"),
                [this]() {
                    doBackup(rawIndex(), backupList->rowToCell(this->index(CELLS)));
                    TransferJob::get().start();
                },
                [this]() { this->removeOverlay(); });
        }
    }

    // Handle pressing/touching R
    if (buttonRestore->released() || (kdown & HidNpadButton_R)) {
        if (backupScrollEnabled) {
            if (this->index(CELLS) != 0 && !backupList->isReceiveRow(this->index(CELLS))) {
                requestRestoreSelected();
            }
        }
    }

    // ZR / its touch button is contextual (3DS parity): wireless Send when an
    // existing backup is highlighted (and transfer is on) — so the "select a
    // backup first" info box never appears — the script picker otherwise.
    // Receive is a row inside the backup list (handled by the A/touch path
    // above).
    if (!MS::multipleSelectionEnabled() && (buttonSend->released() || (kdown & HidNpadButton_ZR))) {
        // startTransferSend self-guards: it no-ops unless a highlighted existing
        // backup is sendable, matching the greyed-out ZR button.
        startTransferSend();
        return;
    }
}

void MainScreen::startTransferReceive(void)
{
    // Receiver lifetime == overlay lifetime: start it here, the overlay stops it
    // on close.
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
    const size_t row = this->index(CELLS);
    if (!backupScrollEnabled || row == 0 || backupList->isReceiveRow(row)) {
        return; // contextual: only a highlighted existing backup can be sent
    }
    if (!KeyboardManager::get().isSystemKeyboardAvailable().first) {
        currentOverlay = std::make_shared<InfoOverlay>(*this, i18n::t("main.receiver_failed"));
        return;
    }

    const size_t cellIndex = backupList->rowToCell(row);
    Title title;
    TitleCatalog::get().getTitle(title, g_currentUId, rawIndex());
    std::string backupName = nameFromCell(cellIndex);
    std::string backupPath = title.fullPath(cellIndex);

    // Keyboard + validation on the UI thread; the blocking zip + socket IO runs
    // on the TransferJob worker.
    std::string lastAddress             = Configuration::getInstance().lastTransferAddress();
    std::pair<bool, std::string> ipResp = KeyboardManager::get().keyboard(lastAddress.empty() ? "192.168.0.10:8000" : lastAddress);
    if (!ipResp.first || ipResp.second.empty()) {
        return;
    }
    auto dst = Transfer::parseTarget(ipResp.second);
    if (!dst) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, i18n::t("main.invalid_ip_port"));
        return;
    }
    // Remember a valid address so the next send prefills the keyboard with it.
    Configuration::getInstance().setLastTransferAddress(ipResp.second);

    std::pair<bool, std::string> pinResp = KeyboardManager::get().keyboard("1234");
    if (!pinResp.first || pinResp.second.empty()) {
        return;
    }
    if (!Transfer::validPin(pinResp.second)) {
        currentOverlay = std::make_shared<ErrorOverlay>(*this, -1, i18n::t("main.pin_invalid"));
        return;
    }

    TransferJob::get().enqueueSend(std::move(title), std::move(backupPath), std::move(backupName), "save", std::move(dst->ip), dst->port,
        std::move(pinResp.second), i18n::t("transfer.completed"));
    TransferJob::get().start();
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
        mCursor    = 0;
        mScrollRow = 0;
    }
    else {
        backupList->resetIndex();
    }
}

size_t MainScreen::index(entryType_t type) const
{
    return type == TITLES ? mCursor : backupList->index();
}

void MainScreen::index(entryType_t type, size_t i)
{
    if (type == TITLES) {
        mCursor = i;
        scrollToCursor(TitleCatalog::get().getFilteredTitleCount(g_currentUId, mSaveTypeFilter));
    }
    else {
        backupList->setIndex(i);
    }
}
