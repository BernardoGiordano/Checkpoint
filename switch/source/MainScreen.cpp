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

static constexpr size_t rowlen = 5, collen = 4, rows = 10, TOPBAR_h = 48;
static constexpr size_t LEFT_SIDEBAR_w  = 72;
static constexpr int SIDEBAR_PAD        = 4;
static constexpr int FILTER_BTN_SIZE    = 56;
static constexpr int FILTER_BTN_X       = 8;
static constexpr int FILTER_BTN_SPACING = 64;
static constexpr int ACCT_ICON_SIZE     = 56;

MainScreen::MainScreen(const InputState& input) : hid(rowlen * collen, collen, input)
{
    pksmBridge       = false;
    wantInstructions = false;
    selectionTimer   = 0;
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    backupList    = std::make_unique<Scrollable>(608, 316, 400, 408, rows);
    buttonBackup  = std::make_unique<Clickable>(1012, 316, 260, 64, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "Backup \ue004", true);
    buttonRestore = std::make_unique<Clickable>(1012, 384, 260, 64, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "Restore \ue005", true);
    buttonCheats  = std::make_unique<Clickable>(1012, 452, 260, 64, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "Cheats \ue0c5", true);
    buttonBackup->canChangeColorWhenSelected(true);
    buttonRestore->canChangeColorWhenSelected(true);
    buttonCheats->canChangeColorWhenSelected(true);

    int filterY = TOPBAR_h + 12;
    buttonSaves = std::make_unique<Clickable>(FILTER_BTN_X, filterY, FILTER_BTN_SIZE, FILTER_BTN_SIZE, COLOR_PURPLE_DARK, COLOR_WHITE, "A", true);
    buttonBCAT  = std::make_unique<Clickable>(
        FILTER_BTN_X, filterY + FILTER_BTN_SPACING, FILTER_BTN_SIZE, FILTER_BTN_SIZE, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "B", true);
    buttonDevice = std::make_unique<Clickable>(
        FILTER_BTN_X, filterY + FILTER_BTN_SPACING * 2, FILTER_BTN_SIZE, FILTER_BTN_SIZE, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "D", true);
    buttonSystem = std::make_unique<Clickable>(
        FILTER_BTN_X, filterY + FILTER_BTN_SPACING * 3, FILTER_BTN_SIZE, FILTER_BTN_SIZE, COLOR_BLACK_DARKER, COLOR_GREY_LIGHT, "S", true);
    buttonSaves->canChangeColorWhenSelected(true);
    buttonBCAT->canChangeColorWhenSelected(true);
    buttonDevice->canChangeColorWhenSelected(true);
    buttonSystem->canChangeColorWhenSelected(true);
}

int MainScreen::selectorX(size_t i) const
{
    return LEFT_SIDEBAR_w + 4 + 128 * ((i % (rowlen * collen)) % collen) + 4 * (((i % (rowlen * collen)) % collen) + 1);
}

int MainScreen::selectorY(size_t i) const
{
    const int row = (i % (rowlen * collen)) / collen;
    return 4 + 128 * row + 4 * (row + 1) + TOPBAR_h;
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

    buttonSaves->setColors(filter == FILTER_SAVES ? COLOR_PURPLE_DARK : COLOR_BLACK_DARKER, filter == FILTER_SAVES ? COLOR_WHITE : COLOR_GREY_LIGHT);
    buttonBCAT->setColors(filter == FILTER_BCAT ? COLOR_PURPLE_DARK : COLOR_BLACK_DARKER, filter == FILTER_BCAT ? COLOR_WHITE : COLOR_GREY_LIGHT);
    buttonDevice->setColors(
        filter == FILTER_DEVICE ? COLOR_PURPLE_DARK : COLOR_BLACK_DARKER, filter == FILTER_DEVICE ? COLOR_WHITE : COLOR_GREY_LIGHT);
    buttonSystem->setColors(
        filter == FILTER_SYSTEM ? COLOR_PURPLE_DARK : COLOR_BLACK_DARKER, filter == FILTER_SYSTEM ? COLOR_WHITE : COLOR_GREY_LIGHT);
}

void MainScreen::draw() const
{
    auto selEnt              = MS::selectedEntries();
    const size_t entries     = hid.maxVisibleEntries();
    const size_t filteredCnt = getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
    const size_t max         = filteredCnt > 0 ? hid.maxEntries(filteredCnt) + 1 : 0;

    SDLH_ClearScreen(COLOR_BLACK_DARKERR);

    // left sidebar background (with padding for elevated look)
    SDLH_DrawRect(SIDEBAR_PAD, TOPBAR_h + SIDEBAR_PAD, LEFT_SIDEBAR_w - SIDEBAR_PAD * 2, 720 - TOPBAR_h - SIDEBAR_PAD * 2, COLOR_BLACK_DARKER);

    // title grid background
    SDLH_DrawRect(LEFT_SIDEBAR_w, TOPBAR_h + 4, 532, 664, COLOR_BLACK_DARKER);

    // top bar
    SDLH_DrawRect(0, 0, 1280, TOPBAR_h, COLOR_BLACK);

    // filter buttons
    buttonSaves->draw(24, COLOR_PURPLE_LIGHT);
    buttonBCAT->draw(24, COLOR_PURPLE_LIGHT);
    buttonDevice->draw(24, COLOR_PURPLE_LIGHT);
    buttonSystem->draw(24, COLOR_PURPLE_LIGHT);

    // sidebar focus indicator
    if (sidebarFocused) {
        int filterY   = TOPBAR_h + 12;
        int focusBtnY = filterY + FILTER_BTN_SPACING * sidebarCursor;
        drawPulsingOutline(FILTER_BTN_X, focusBtnY, FILTER_BTN_SIZE, FILTER_BTN_SIZE, 3, COLOR_PURPLE_LIGHT);
    }

    // account icon at bottom of left sidebar
    int acctIconX = (LEFT_SIDEBAR_w - ACCT_ICON_SIZE) / 2;
    int acctIconY = 720 - ACCT_ICON_SIZE - 30;
    if (mSaveTypeFilter == FILTER_SAVES) {
        drawPulsingOutline(acctIconX, acctIconY, ACCT_ICON_SIZE, ACCT_ICON_SIZE, 2, COLOR_GREEN);
    }
    if (Account::icon(g_currentUId) != NULL) {
        SDLH_DrawImageScale(Account::icon(g_currentUId), acctIconX, acctIconY, ACCT_ICON_SIZE, ACCT_ICON_SIZE);
        if (mSaveTypeFilter != FILTER_SAVES) {
            SDLH_DrawRect(acctIconX, acctIconY, ACCT_ICON_SIZE, ACCT_ICON_SIZE, FC_MakeColor(0, 0, 0, 160));
        }
    }

    u32 username_w, username_h;
    std::string username = Account::shortName(g_currentUId);
    SDLH_GetTextDimensions(11, username.c_str(), &username_w, &username_h);
    SDL_Color usernameColor = mSaveTypeFilter == FILTER_SAVES ? COLOR_WHITE : COLOR_GREY_LIGHT;
    SDLH_DrawTextBox(11, (LEFT_SIDEBAR_w - username_w) / 2, 720 - 28 + (28 - username_h) / 2, usernameColor, LEFT_SIDEBAR_w, username.c_str());

    // title icons
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        int selectorx = selectorX(k);
        int selectory = selectorY(k);
        if (filteredSmallIcon(g_currentUId, mSaveTypeFilter, k) != NULL) {
            SDLH_DrawImageScale(filteredSmallIcon(g_currentUId, mSaveTypeFilter, k), selectorx, selectory, 128, 128);
        }
        else {
            SDLH_DrawRect(selectorx, selectory, 128, 128, COLOR_BLACK);
        }

        if (!selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end()) {
            SDLH_DrawIcon("checkbox", selectorx + 86, selectory + 86);
        }

        if (filteredFavorite(g_currentUId, mSaveTypeFilter, k)) {
            SDLH_DrawRect(selectorx + 94, selectory + 8, 24, 24, COLOR_GOLD);
            SDLH_DrawIcon("star", selectorx + 86, selectory);
        }
    }

    // title selector (hidden when sidebar is focused)
    if (filteredCnt > 0 && !sidebarFocused) {
        const int x = selectorX(hid.index()) + 4 / 2;
        const int y = selectorY(hid.index()) + 4 / 2;
        drawPulsingOutline(x, y, 124, 124, 4, COLOR_PURPLE_DARK);
        SDLH_DrawRect(x, y, 124, 124, COLOR_WHITEMASK);
    }

    u32 ver_w, ver_h, checkpoint_h, checkpoint_w;
    SDLH_GetTextDimensions(20, ver, &ver_w, &ver_h);
    SDLH_GetTextDimensions(26, "checkpoint", &checkpoint_w, &checkpoint_h);

    SDLH_DrawText(26, 8, (TOPBAR_h - checkpoint_h) / 2 + 4, COLOR_WHITE, "checkpoint");
    SDLH_DrawText(20, 8 + checkpoint_w + 8, (TOPBAR_h - checkpoint_h) / 2 + checkpoint_h - ver_h + 2, COLOR_GREY_LIGHT, ver);
    SDLH_DrawText(
        20, 8 + checkpoint_w + 8 + ver_w + 32, (TOPBAR_h - checkpoint_h) / 2 + checkpoint_h - ver_h + 2, COLOR_GREY_LIGHT, "\ue046 Instructions");

    backupList->flush();
    if (filteredCnt > 0) {
        Title title;
        getFilteredTitle(title, g_currentUId, mSaveTypeFilter, hid.fullIndex());

        std::vector<std::string> dirs = title.saves();

        for (size_t i = 0; i < dirs.size(); i++) {
            backupList->push_back(COLOR_BLACK_DARKER, COLOR_WHITE, dirs.at(i), i == backupList->index());
        }

        if (title.icon() != NULL) {
            drawOutline(1012, 52, 256, 256, 4, COLOR_BLACK_DARK);
            SDLH_DrawImage(title.icon(), 1012, 52);
        }

        u32 h = 29, offset = 56, i = 0, title_w;
        auto gameName = title.displayName();
        SDLH_GetTextDimensions(26, gameName.c_str(), &title_w, NULL);

        if (title_w >= 680) {
            gameName = gameName.substr(0, 38) + "...";
            SDLH_GetTextDimensions(26, gameName.c_str(), &title_w, NULL);
        }

        SDLH_DrawText(26, 1280 - 8 - title_w, (TOPBAR_h - checkpoint_h) / 2 + 4, COLOR_WHITE, gameName.c_str());
        static constexpr u32 DESC_MAX_W = 360;
        SDLH_DrawText(
            23, 610, offset + h * (i++), COLOR_GREY_LIGHT, trimToFit(StringUtils::format("Title ID: %016llX", title.id()), DESC_MAX_W, 23).c_str());
        SDLH_DrawText(23, 610, offset + h * (i++), COLOR_GREY_LIGHT, trimToFit("Author: " + title.author(), DESC_MAX_W, 23).c_str());
        if (title.saveDataType() == FsSaveDataType_Bcat) {
            SDLH_DrawText(23, 610, offset + h * (i++), COLOR_GREY_LIGHT, "Type: BCAT");
        }
        else if (title.saveDataType() == FsSaveDataType_Device) {
            SDLH_DrawText(23, 610, offset + h * (i++), COLOR_GREY_LIGHT, "Type: Device");
        }
        else {
            SDLH_DrawText(23, 610, offset + h * (i++), COLOR_GREY_LIGHT, trimToFit("User: " + title.userName(), DESC_MAX_W, 23).c_str());
            if (!title.playTime().empty()) {
                SDLH_DrawText(23, 610, offset + h * i, COLOR_GREY_LIGHT, trimToFit("Play Time: " + title.playTime(), DESC_MAX_W, 23).c_str());
            }
        }

        backupList->draw(g_backupScrollEnabled);
        buttonBackup->draw(30, COLOR_PURPLE_LIGHT);
        buttonRestore->draw(30, COLOR_PURPLE_LIGHT);
        buttonCheats->draw(30, COLOR_PURPLE_LIGHT);
    }
    else {
        const char* emptyMsg = mSaveTypeFilter == FILTER_BCAT     ? "No BCAT saves"
                               : mSaveTypeFilter == FILTER_DEVICE ? "No Device saves"
                               : mSaveTypeFilter == FILTER_SYSTEM ? "No System saves"
                                                                  : "No saves";
        u32 emptyW;
        SDLH_GetTextDimensions(26, emptyMsg, &emptyW, NULL);
        SDLH_DrawText(26, LEFT_SIDEBAR_w + (532 - emptyW) / 2, 360, COLOR_GREY_LIGHT, emptyMsg);
    }

    if (wantInstructions && currentOverlay == nullptr) {
        SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);
        SDLH_DrawText(28, (LEFT_SIDEBAR_w - ACCT_ICON_SIZE) / 2, 720 - ACCT_ICON_SIZE - 17, COLOR_WHITE, "\ue085\ue086");
        SDLH_DrawText(24, 58 + LEFT_SIDEBAR_w, 69, COLOR_WHITE, "\ue058 Tap to select title");
        SDLH_DrawText(24, 58 + LEFT_SIDEBAR_w, 109, COLOR_WHITE, ("\ue026 Sort: " + sortMode()).c_str());
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 270, COLOR_WHITE, "\ue006 \ue080 to scroll between titles");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 300, COLOR_WHITE, "\ue004 \ue005 to scroll between pages");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 330, COLOR_WHITE, "\ue000 to enter the selected title");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 360, COLOR_WHITE, "\ue001 to exit the selected title");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 390, COLOR_WHITE, "\ue002 to change sort mode");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 420, COLOR_WHITE, "\ue003 to select multiple titles");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 450, COLOR_WHITE, "Hold \ue003 to select all titles");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 480, COLOR_WHITE, "\ue0a4 to cycle save type");
        SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 510, COLOR_WHITE, "\ue006 left to navigate save type filter");
        SDLH_DrawText(24, 680, 510, COLOR_WHITE, "\ue002 to delete a backup");
        if (Configuration::getInstance().isPKSMBridgeEnabled()) {
            SDLH_DrawText(24, 100 + LEFT_SIDEBAR_w, 540, COLOR_WHITE, "\ue004 + \ue005 to enable PKSM bridge");
        }
        if (gethostid() != INADDR_LOOPBACK) {
            if (g_ftpAvailable && Configuration::getInstance().isFTPEnabled()) {
                SDLH_DrawText(24, 600, 642, COLOR_GOLD, StringUtils::format("FTP server running on %s:50000", getConsoleIP()).c_str());
            }
            SDLH_DrawText(24, 600, 672, COLOR_GOLD, StringUtils::format("Configuration server running on %s:8000", getConsoleIP()).c_str());
        }
    }

    if (g_isTransferringFile) {
        SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);

        // Modal box centered on screen
        const int mx = 370, my = 260, mw = 540, mh = 230;
        SDLH_DrawRect(mx, my, mw, mh, COLOR_BLACK_DARKERR);
        drawOutline(mx, my, mw, mh, 3, COLOR_PURPLE_LIGHT);

        // Title
        std::string titleStr = (g_transferMode.empty() ? "Copying files" : g_transferMode) + " in progress...";
        u32 title_w, title_h;
        SDLH_GetTextDimensions(26, titleStr.c_str(), &title_w, &title_h);
        SDLH_DrawText(26, mx + (mw - (int)title_w) / 2, my + 14, COLOR_WHITE, titleStr.c_str());

        // Current filename
        u32 fname_w, fname_h;
        std::string fname = trimToFit(g_currentFile, mw - 40, 22);
        SDLH_GetTextDimensions(22, fname.c_str(), &fname_w, &fname_h);
        SDLH_DrawText(22, mx + (mw - (int)fname_w) / 2, my + 14 + (int)title_h + 8, COLOR_GREY_LIGHT, fname.c_str());

        const int barX = mx + 20, barW = mw - 40, barH = 18;

        // Per-save progress bar
        const int saveBarY = my + 108;
        SDLH_DrawRect(barX, saveBarY, barW, barH, COLOR_BLACK_MEDIUM);

        float progress = (g_copyTotal > 0) ? (float)g_copyCount / (float)g_copyTotal : 0.0f;
        if (progress > 1.0f)
            progress = 1.0f;
        int saveFillW = (int)(barW * progress);
        if (saveFillW > 0) {
            SDLH_DrawRect(barX, saveBarY, saveFillW, barH, COLOR_PURPLE_LIGHT);
        }
        drawOutline(barX, saveBarY, barW, barH, 2, COLOR_GREY_LIGHT);

        // Count (left) and percentage (right) below per-save bar
        char countStr[24];
        snprintf(countStr, sizeof(countStr), "%zu / %zu", g_copyCount, g_copyTotal);
        char pctStr[8];
        snprintf(pctStr, sizeof(pctStr), "%d%%%%", (int)(progress * 100));

        u32 pct_w, pct_h;
        SDLH_GetTextDimensions(20, pctStr, &pct_w, &pct_h);
        SDLH_DrawText(20, barX, saveBarY + barH + 6, COLOR_GREY_LIGHT, countStr);
        SDLH_DrawText(20, barX + barW - (int)pct_w, saveBarY + barH + 6, COLOR_WHITE, pctStr);

        // Per-file progress bar
        const int fileBarY = my + 160;
        SDLH_DrawRect(barX, fileBarY, barW, barH, COLOR_BLACK_MEDIUM);

        float fileProgress = (g_currentFileSize > 0) ? (float)g_currentFileOffset / (float)g_currentFileSize : 0.0f;
        if (fileProgress > 1.0f)
            fileProgress = 1.0f;
        int fileFillW = (int)(barW * fileProgress);
        if (fileFillW > 0) {
            SDLH_DrawRect(barX, fileBarY, fileFillW, barH, COLOR_PURPLE_LIGHT);
        }
        drawOutline(barX, fileBarY, barW, barH, 2, COLOR_GREY_LIGHT);

        // KB transferred (left) and percentage (right) below per-file bar
        char kbStr[40];
        snprintf(kbStr, sizeof(kbStr), "%.1f / %.1f KB", g_currentFileOffset / 1024.0f, g_currentFileSize / 1024.0f);
        char filePctStr[8];
        snprintf(filePctStr, sizeof(filePctStr), "%d%%%%", (int)(fileProgress * 100));

        u32 filePct_w;
        SDLH_GetTextDimensions(20, filePctStr, &filePct_w, NULL);
        SDLH_DrawText(20, barX, fileBarY + barH + 6, COLOR_GREY_LIGHT, kbStr);
        SDLH_DrawText(20, barX + barW - (int)filePct_w, fileBarY + barH + 6, COLOR_WHITE, filePctStr);
    }
}

void MainScreen::update(const InputState& input)
{
    updateSelector(input);
    handleEvents(input);
}

void MainScreen::updateSelector(const InputState& input)
{
    if (!g_backupScrollEnabled) {
        size_t count    = getFilteredTitleCount(g_currentUId, mSaveTypeFilter);
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
        else if ((input.kDown & HidNpadButton_Left) && hid.index() % collen == 0) {
            sidebarFocused = true;
            sidebarCursor  = mSaveTypeFilter == FILTER_SAVES ? 0 : mSaveTypeFilter == FILTER_BCAT ? 1 : mSaveTypeFilter == FILTER_DEVICE ? 2 : 3;
        }
        else {
            hid.update(count);
        }

        // loop through every rendered title
        for (u8 row = 0; row < rowlen; row++) {
            for (u8 col = 0; col < collen; col++) {
                u8 index = row * collen + col;
                if (index > hid.maxEntries(count))
                    break;

                u32 x = selectorX(index);
                u32 y = selectorY(index);
                if (input.touch.count > 0 && input.touch.touches[0].x >= x && input.touch.touches[0].x <= x + 128 && input.touch.touches[0].y >= y &&
                    input.touch.touches[0].y <= y + 128) {
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
    return filteredToRawIndex(g_currentUId, mSaveTypeFilter, this->index(TITLES));
}

void MainScreen::handleEvents(const InputState& input)
{
    const u64 kheld = input.kHeld;
    const u64 kdown = input.kDown;

    wantInstructions = (kheld & HidNpadButton_Minus);

    // handle filter button touches
    if (buttonSaves->released()) {
        setSaveTypeFilter(FILTER_SAVES);
        sidebarFocused = false;
    }
    else if (buttonBCAT->released()) {
        setSaveTypeFilter(FILTER_BCAT);
        sidebarFocused = false;
    }
    else if (buttonDevice->released()) {
        setSaveTypeFilter(FILTER_DEVICE);
        sidebarFocused = false;
    }
    else if (buttonSystem->released()) {
        setSaveTypeFilter(FILTER_SYSTEM);
        sidebarFocused = false;
    }

    // handle sidebar D-pad navigation
    if (sidebarFocused) {
        if (kdown & HidNpadButton_Up) {
            sidebarCursor = sidebarCursor > 0 ? sidebarCursor - 1 : 3;
        }
        else if (kdown & HidNpadButton_Down) {
            sidebarCursor = sidebarCursor < 3 ? sidebarCursor + 1 : 0;
        }
        if (kdown & HidNpadButton_A) {
            static constexpr saveTypeFilter_t filters[] = {FILTER_SAVES, FILTER_BCAT, FILTER_DEVICE, FILTER_SYSTEM};
            setSaveTypeFilter(filters[sidebarCursor]);
        }
        // Right/B exit is handled in updateSelector to prevent double cursor movement
        return;
    }

    // handle StickL press to cycle filter
    if (kdown & HidNpadButton_StickL) {
        if (mSaveTypeFilter == FILTER_SAVES)
            setSaveTypeFilter(FILTER_BCAT);
        else if (mSaveTypeFilter == FILTER_BCAT)
            setSaveTypeFilter(FILTER_DEVICE);
        else if (mSaveTypeFilter == FILTER_DEVICE)
            setSaveTypeFilter(FILTER_SYSTEM);
        else
            setSaveTypeFilter(FILTER_SAVES);
    }

    if (mSaveTypeFilter == FILTER_SAVES) {
        if (kdown & HidNpadButton_ZL || kdown & HidNpadButton_ZR) {
            while ((g_currentUId = Account::selectAccount()) == 0)
                ;
            this->index(TITLES, 0);
            this->index(CELLS, 0);
            setPKSMBridgeFlag(false);
        }
    }

    // handle PKSM bridge (only for account saves)
    if (mSaveTypeFilter == FILTER_SAVES && Configuration::getInstance().isPKSMBridgeEnabled()) {
        Title title;
        getTitle(title, g_currentUId, rawIndex());
        if (!getPKSMBridgeFlag()) {
            if ((kheld & HidNpadButton_L) && (kheld & HidNpadButton_R) && title.saveDataType() != FsSaveDataType_Bcat &&
                title.saveDataType() != FsSaveDataType_Device && isPKSMBridgeTitle(title.id())) {
                setPKSMBridgeFlag(true);
                updateButtons();
            }
        }
    }

    // handle account icon touch (only when filter is Saves)
    if (mSaveTypeFilter == FILTER_SAVES && !g_backupScrollEnabled && input.touch.count > 0) {
        u32 acctIconX = (LEFT_SIDEBAR_w - ACCT_ICON_SIZE) / 2;
        u32 acctIconY = 720 - ACCT_ICON_SIZE - 30;
        if (input.touch.touches[0].x >= acctIconX && input.touch.touches[0].x <= acctIconX + ACCT_ICON_SIZE &&
            input.touch.touches[0].y >= acctIconY && input.touch.touches[0].y <= acctIconY + ACCT_ICON_SIZE) {
            while ((g_currentUId = Account::selectAccount()) == 0)
                ;
            this->index(TITLES, 0);
            this->index(CELLS, 0);
            setPKSMBridgeFlag(false);
        }
    }

    // Handle touching the backup list
    if (input.touch.count > 0 && input.touch.touches[0].x > 608 && input.touch.touches[0].x < 1008 && input.touch.touches[0].y > 316 &&
        input.touch.touches[0].y < 720) {
        // Activate backup list only if multiple selections are not enabled
        if (!MS::multipleSelectionEnabled()) {
            g_backupScrollEnabled = true;
            updateButtons();
            entryType(CELLS);
        }
    }

    // Handle pressing A
    // Backup list active:   Backup/Restore
    // Backup list inactive: Activate backup list only if multiple
    //                       selections are enabled
    if ((kdown & HidNpadButton_A) && getFilteredTitleCount(g_currentUId, mSaveTypeFilter) > 0) {
        // If backup list is active...
        if (g_backupScrollEnabled) {
            // If the "New..." entry is selected...
            if (0 == this->index(CELLS)) {
                if (!getPKSMBridgeFlag()) {
                    auto result = io::backup(rawIndex(), g_currentUId, this->index(CELLS));
                    if (std::get<0>(result)) {
                        currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                    }
                    else {
                        currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                    }
                }
            }
            else {
                if (getPKSMBridgeFlag()) {
                    auto result = recvFromPKSMBridge(rawIndex(), g_currentUId, this->index(CELLS));
                    if (std::get<0>(result)) {
                        currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                    }
                    else {
                        currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                    }
                }
                else {
                    currentOverlay = std::make_shared<YesNoOverlay>(
                        *this, "Restore selected save?",
                        [this]() {
                            auto result = io::restore(rawIndex(), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
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
        }
        else {
            // Activate backup list only if multiple selections are not enabled
            if (!MS::multipleSelectionEnabled()) {
                g_backupScrollEnabled = true;
                updateButtons();
                entryType(CELLS);
            }
        }
    }

    // Handle pressing B
    if ((kdown & HidNpadButton_B) || (input.touch.count > 0 && input.touch.touches[0].x >= (int)LEFT_SIDEBAR_w &&
                                         input.touch.touches[0].x <= (int)(LEFT_SIDEBAR_w + 532) && input.touch.touches[0].y <= 664)) {
        this->index(CELLS, 0);
        g_backupScrollEnabled = false;
        entryType(TITLES);
        MS::clearSelectedEntries();
        setPKSMBridgeFlag(false);
        updateButtons(); // Do this last
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
                        getTitle(title, g_currentUId, rawIndex());
                        std::string path = title.fullPath(index);
                        io::deleteFolderRecursively((path + "/").c_str());
                        refreshDirectories(title.id());
                        this->index(CELLS, index - 1);
                        this->removeOverlay();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }
        else {
            rotateSortMode();
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
        updateButtons(); // Do this last
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
        for (size_t i = 0, sz = getFilteredTitleCount(g_currentUId, mSaveTypeFilter); i < sz; i++) {
            MS::addSelectedEntry(i);
        }
        selectionTimer = 0;
    }

    // Handle pressing/touching L
    if (buttonBackup->released() || (kdown & HidNpadButton_L)) {
        if (MS::multipleSelectionEnabled()) {
            resetIndex(CELLS);
            std::vector<size_t> list = MS::selectedEntries();
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                // translate filtered index to raw index for multi-selection
                size_t raw  = filteredToRawIndex(g_currentUId, mSaveTypeFilter, list.at(i));
                auto result = io::backup(raw, g_currentUId, this->index(CELLS));
                if (std::get<0>(result)) {
                    currentOverlay = std::make_shared<InfoOverlay>(*this, std::get<2>(result));
                }
                else {
                    currentOverlay = std::make_shared<ErrorOverlay>(*this, std::get<1>(result), std::get<2>(result));
                }
            }
            MS::clearSelectedEntries();
            updateButtons();
            blinkLed(4);
            currentOverlay = std::make_shared<InfoOverlay>(*this, "Progress correctly saved to disk.");
        }
        else if (g_backupScrollEnabled) {
            if (getPKSMBridgeFlag()) {
                if (this->index(CELLS) != 0) {
                    currentOverlay = std::make_shared<YesNoOverlay>(
                        *this, "Send save to PKSM?",
                        [this]() {
                            auto result = sendToPKSMBrigde(rawIndex(), g_currentUId, this->index(CELLS));
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
                        auto result = io::backup(rawIndex(), g_currentUId, this->index(CELLS));
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
                    currentOverlay = std::make_shared<YesNoOverlay>(
                        *this, "Restore selected save?",
                        [this]() {
                            auto result = io::restore(rawIndex(), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
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
        }
    }

    if ((buttonCheats->released() || (kdown & HidNpadButton_StickR)) && CheatManager::getInstance().cheats() != nullptr) {
        if (MS::multipleSelectionEnabled()) {
            MS::clearSelectedEntries();
            updateButtons();
        }
        else {
            Title title;
            getTitle(title, g_currentUId, rawIndex());
            std::string key = StringUtils::format("%016llX", title.id());
            if (CheatManager::getInstance().areCheatsAvailable(key)) {
                currentOverlay = std::make_shared<CheatManagerOverlay>(*this, key);
            }
            else {
                currentOverlay = std::make_shared<InfoOverlay>(*this, "No available cheat codes for this title.");
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
    updateButtons();
}

void MainScreen::updateButtons(void)
{
    if (MS::multipleSelectionEnabled()) {
        buttonRestore->canChangeColorWhenSelected(true);
        buttonRestore->canChangeColorWhenSelected(false);
        buttonCheats->canChangeColorWhenSelected(false);
        buttonBackup->setColors(COLOR_BLACK_DARKER, COLOR_WHITE);
        buttonRestore->setColors(COLOR_BLACK_DARKER, COLOR_GREY_LIGHT);
        buttonCheats->setColors(COLOR_BLACK_DARKER, COLOR_GREY_LIGHT);
    }
    else if (g_backupScrollEnabled) {
        buttonBackup->canChangeColorWhenSelected(true);
        buttonRestore->canChangeColorWhenSelected(true);
        buttonCheats->canChangeColorWhenSelected(true);
        buttonBackup->setColors(COLOR_BLACK_DARKER, COLOR_WHITE);
        buttonRestore->setColors(COLOR_BLACK_DARKER, COLOR_WHITE);
        buttonCheats->setColors(COLOR_BLACK_DARKER, COLOR_WHITE);
    }
    else {
        buttonBackup->setColors(COLOR_BLACK_DARKER, COLOR_GREY_LIGHT);
        buttonRestore->setColors(COLOR_BLACK_DARKER, COLOR_GREY_LIGHT);
        buttonCheats->setColors(COLOR_BLACK_DARKER, COLOR_GREY_LIGHT);
    }

    if (getPKSMBridgeFlag() && mSaveTypeFilter == FILTER_SAVES) {
        buttonBackup->text("Send \ue004");
        buttonRestore->text("Receive \ue005");
    }
    else {
        buttonBackup->text("Backup \ue004");
        buttonRestore->text("Restore \ue005");
    }
}

std::string MainScreen::sortMode() const
{
    switch (g_sortMode) {
        case SORT_LAST_PLAYED:
            return "Last played";
        case SORT_PLAY_TIME:
            return "Play time";
        case SORT_ALPHA:
            return "Alphabetical";
        default:
            break;
    }
    return "";
}
