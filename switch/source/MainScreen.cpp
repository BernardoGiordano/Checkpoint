/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

static constexpr size_t rowlen = 5, collen = 4, rows = 10, SIDEBAR_w = 96;

MainScreen::MainScreen() : hid(rowlen * collen, collen)
{
    pksmBridge     = false;
    selectionTimer = 0;
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    backupList    = std::make_unique<Scrollable>(538, 276, 414, 380, rows);
    buttonBackup  = std::make_unique<Clickable>(956, 276, 220, 80, theme().c2, theme().c6, "Backup \ue004", true);
    buttonRestore = std::make_unique<Clickable>(956, 360, 220, 80, theme().c2, theme().c6, "Restore \ue005", true);
    buttonCheats  = std::make_unique<Clickable>(956, 444, 220, 80, theme().c2, theme().c6, "Cheats \ue0c5", true);
    buttonBackup->canChangeColorWhenSelected(true);
    buttonRestore->canChangeColorWhenSelected(true);
    buttonCheats->canChangeColorWhenSelected(true);
}

int MainScreen::selectorX(size_t i) const
{
    return 128 * ((i % (rowlen * collen)) % collen) + 4 * (((i % (rowlen * collen)) % collen) + 1);
}

int MainScreen::selectorY(size_t i) const
{
    return 128 * ((i % (rowlen * collen)) / collen) + 4 * (((i % (rowlen * collen)) / collen) + 1);
}

void MainScreen::draw() const
{
    auto selEnt          = MS::selectedEntries();
    const size_t entries = hid.maxVisibleEntries();
    const size_t max     = hid.maxEntries(getTitleCount(g_currentUId)) + 1;

    SDLH_ClearScreen(theme().c1);
    SDL_Color colorBar = getPKSMBridgeFlag() ? COLOR_HIGHBLUE : FC_MakeColor(theme().c1.r - 15, theme().c1.g - 15, theme().c1.b - 15, 255);
    SDLH_DrawRect(0, 0, 532, 662, FC_MakeColor(theme().c1.r + 5, theme().c1.g + 5, theme().c1.b + 5, 255));
    SDLH_DrawRect(1280 - SIDEBAR_w, 0, SIDEBAR_w, 720, colorBar);

    drawPulsingOutline(
        1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE, 2, COLOR_GREEN);
    SDLH_DrawImageScale(
        Account::icon(g_currentUId), 1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE);

    u32 username_w, username_h;
    std::string username = Account::shortName(g_currentUId);
    SDLH_GetTextDimensions(13, username.c_str(), &username_w, &username_h);
    SDLH_DrawTextBox(13, 1280 - SIDEBAR_w + (SIDEBAR_w - username_w) / 2, 720 - 28 + (28 - username_h) / 2, theme().c6, SIDEBAR_w, username.c_str());

    // title icons
    for (size_t k = hid.page() * entries; k < hid.page() * entries + max; k++) {
        int selectorx = selectorX(k);
        int selectory = selectorY(k);
        if (smallIcon(g_currentUId, k) != NULL) {
            SDLH_DrawImageScale(smallIcon(g_currentUId, k), selectorx, selectory, 128, 128);
        }
        else {
            SDLH_DrawRect(selectorx, selectory, 128, 128, theme().c0);
        }

        if (!selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end()) {
            SDLH_DrawIcon("checkbox", selectorx + 86, selectory + 86);
        }

        if (favorite(g_currentUId, k)) {
            SDLH_DrawRect(selectorx + 94, selectory + 8, 24, 24, COLOR_GOLD);
            SDLH_DrawIcon("star", selectorx + 86, selectory);
        }
    }

    // title selector
    if (getTitleCount(g_currentUId) > 0) {
        const int x = selectorX(hid.index()) + 4 / 2;
        const int y = selectorY(hid.index()) + 4 / 2;
        drawPulsingOutline(x, y, 124, 124, 4, COLOR_BLUE);
        SDLH_DrawRect(x, y, 124, 124, COLOR_WHITEMASK);
    }

    if (getTitleCount(g_currentUId) > 0) {
        Title title;
        getTitle(title, g_currentUId, hid.fullIndex());

        backupList->flush();
        std::vector<std::string> dirs = title.saves();

        for (size_t i = 0; i < dirs.size(); i++) {
            backupList->push_back(theme().c2, theme().c6, dirs.at(i), i == backupList->index());
        }

        if (title.icon() != NULL) {
            drawOutline(1018, 6, 256, 256, 4, theme().c3);
            SDLH_DrawImage(title.icon(), 1018, 6);
        }

        // draw infos
        u32 title_w, title_h, h, titleid_w, producer_w, user_w, subtitle_w, playtime_w;
        auto displayName = title.displayName();
        SDLH_GetTextDimensions(28, displayName.first.c_str(), &title_w, &title_h);
        SDLH_GetTextDimensions(23, "Title: ", &subtitle_w, NULL);
        SDLH_GetTextDimensions(23, "Title ID: ", &titleid_w, &h);
        SDLH_GetTextDimensions(23, "Author: ", &producer_w, NULL);
        SDLH_GetTextDimensions(23, "User: ", &user_w, NULL);

        u8 boxRows = (displayName.second.length() > 0 ? 4 : 3);

        h += 6;
        if (!title.playTime().empty()) {
            boxRows++;
            SDLH_GetTextDimensions(23, "Play Time: ", &playtime_w, NULL);
        }

        u32 offset = 10 + title_h + h / 2;
        int i      = 0;

        SDLH_DrawRect(534, 2, 482, 16 + title_h, theme().c3);
        SDLH_DrawRect(534, offset - h / 2 - 2, 480, h * boxRows + h / 2, theme().c2);

        SDLH_DrawText(28, 538 - 8 + 482 - title_w, 8, theme().c5, displayName.first.c_str());
        if (displayName.second.length() > 0) {
            SDLH_DrawText(23, 538, offset + h * i, theme().c5, "Title:");
            SDLH_DrawTextBox(23, 538 + subtitle_w, offset + h * (i++), theme().c6, 478 - 4 * 2 - subtitle_w, displayName.second.c_str());
        }

        SDLH_DrawText(23, 538, offset + h * i, theme().c5, "Title ID:");
        SDLH_DrawTextBox(
            23, 538 + titleid_w, offset + h * (i++), theme().c6, 478 - 4 * 2 - titleid_w, StringUtils::format("%016llX", title.id()).c_str());

        SDLH_DrawText(23, 538, offset + h * i, theme().c5, "Author:");
        SDLH_DrawTextBox(23, 538 + producer_w, offset + h * (i++), theme().c6, 478 - 4 * 2 - producer_w, title.author().c_str());

        SDLH_DrawText(23, 538, offset + h * i, theme().c5, "User:");
        SDLH_DrawTextBox(23, 538 + user_w, offset + h * (i++), theme().c6, 478 - 4 * 2 - user_w, title.userName().c_str());

        if (!title.playTime().empty()) {
            SDLH_DrawText(23, 538, offset + h * i, theme().c5, "Play Time:");
            SDLH_DrawTextBox(23, 538 + playtime_w, offset + h * (i++), theme().c6, 478 - 4 * 2 - playtime_w, title.playTime().c_str());
        }

        drawOutline(538, 276, 414, 380, 4, theme().c3);
        drawOutline(956, 276, 220, 80, 4, theme().c3);
        drawOutline(956, 360, 220, 80, 4, theme().c3);
        drawOutline(956, 444, 220, 80, 4, theme().c3);
        backupList->draw(g_backupScrollEnabled);
        buttonBackup->draw(30, COLOR_NULL);
        buttonRestore->draw(30, COLOR_NULL);
        buttonCheats->draw(30, COLOR_NULL);
    }

    SDL_Color lightBlack = FC_MakeColor(theme().c0.r + 20, theme().c0.g + 20, theme().c0.b + 20, 255);
    u32 ver_w, ver_h, checkpoint_h, checkpoint_w, inst_w, inst_h;
    SDLH_GetTextDimensions(20, ver, &ver_w, &ver_h);
    SDLH_GetTextDimensions(26, "checkpoint", &checkpoint_w, &checkpoint_h);
    SDLH_GetTextDimensions(24, "\ue046 Instructions", &inst_w, &inst_h);

    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_MINUS && currentOverlay == nullptr) {
        SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);
        SDLH_DrawText(27, 1205, 646, theme().c6, "\ue085\ue086");
        SDLH_DrawText(24, 58, 69, theme().c6, "\ue058 Tap to select title");
        SDLH_DrawText(24, 100, 270, theme().c6, "\ue006 \ue080 to scroll between titles");
        SDLH_DrawText(24, 100, 300, theme().c6, "\ue004 \ue005 to scroll between pages");
        SDLH_DrawText(24, 100, 330, theme().c6, "\ue000 to enter the selected title");
        SDLH_DrawText(24, 100, 360, theme().c6, "\ue001 to exit the selected title");
        SDLH_DrawText(24, 100, 390, theme().c6, "\ue003 to multiselect title");
        SDLH_DrawText(24, 100, 420, theme().c6, "Hold \ue003 to select all titles");
        if (Configuration::getInstance().isPKSMBridgeEnabled()) {
            SDLH_DrawText(24, 100, 420, theme().c6, "\ue004 + \ue005 to enable PKSM bridge");
        }
        SDLH_DrawText(24, 616, 450, theme().c6, "\ue002 to delete a backup");
        if (gethostid() != INADDR_LOOPBACK) {
            if (g_ftpAvailable && Configuration::getInstance().isFTPEnabled()) {
                SDLH_DrawText(24, 16 * 6 + checkpoint_w + 8 + ver_w + inst_w, 642 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, COLOR_GOLD,
                    StringUtils::format("FTP server running on %s:50000", getConsoleIP()).c_str());
            }
            SDLH_DrawText(24, 16 * 6 + checkpoint_w + 8 + ver_w + inst_w, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, COLOR_GOLD,
                StringUtils::format("Configuration server running on %s:8000", getConsoleIP()).c_str());
        }
    }

    SDLH_DrawRect(0, 672, checkpoint_w + ver_w + 2 * 16 + 8, 40, lightBlack);
    SDLH_DrawText(26, 16, 672 + (40 - checkpoint_h) / 2 + 2, theme().c6, "checkpoint");
    SDLH_DrawText(20, 16 + checkpoint_w + 8, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - ver_h, theme().c6, ver);
    SDLH_DrawText(24, 16 * 3 + checkpoint_w + 8 + ver_w, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, theme().c6, "\ue046 Instructions");

    if (g_isTransferringFile) {
        SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);

        u32 w, h;
        SDLH_GetTextDimensions(28, g_currentFile.c_str(), &w, &h);
        SDLH_DrawText(28, (1280 - w) / 2, (720 - h) / 2, COLOR_WHITE, g_currentFile.c_str());
    }
}

void MainScreen::update(touchPosition* touch)
{
    updateSelector(touch);
    handleEvents(touch);
}

void MainScreen::updateSelector(touchPosition* touch)
{
    if (!g_backupScrollEnabled) {
        size_t count    = getTitleCount(g_currentUId);
        size_t oldindex = hid.index();
        hid.update(count);
        // change page
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_L) {
            hid.pageBack(count);
        }
        else if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_R) {
            hid.pageForward(count);
        }

        // loop through every rendered title
        for (u8 row = 0; row < rowlen; row++) {
            for (u8 col = 0; col < collen; col++) {
                u8 index = row * collen + col;
                if (index > hid.maxEntries(count))
                    break;

                u32 x = selectorX(index);
                u32 y = selectorY(index);
                if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH && touch->px >= x && touch->px <= x + 128 && touch->py >= y && touch->py <= y + 128) {
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

void MainScreen::handleEvents(touchPosition* touch)
{
    u32 kdown = hidKeysDown(CONTROLLER_P1_AUTO);
    u32 kheld = hidKeysHeld(CONTROLLER_P1_AUTO);
    if (kdown & KEY_ZL || kdown & KEY_ZR) {
        while ((g_currentUId = Account::selectAccount()) == 0)
            ;
        this->index(TITLES, 0);
        this->index(CELLS, 0);
        setPKSMBridgeFlag(false);
    }
    // handle PKSM bridge
    if (Configuration::getInstance().isPKSMBridgeEnabled()) {
        Title title;
        getTitle(title, g_currentUId, this->index(TITLES));
        if (!getPKSMBridgeFlag()) {
            if ((kheld & KEY_L) && (kheld & KEY_R) && isPKSMBridgeTitle(title.id())) {
                setPKSMBridgeFlag(true);
                updateButtons();
            }
        }
    }

    // handle touchscreen
    if (!g_backupScrollEnabled && hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH && touch->px >= 1200 && touch->px <= 1200 + USER_ICON_SIZE &&
        touch->py >= 626 && touch->py <= 626 + USER_ICON_SIZE) {
        while ((g_currentUId = Account::selectAccount()) == 0)
            ;
        this->index(TITLES, 0);
        this->index(CELLS, 0);
        setPKSMBridgeFlag(false);
    }

    // Handle touching the backup list
    if ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH && (int)touch->px > 538 && (int)touch->px < 952 && (int)touch->py > 276 &&
            (int)touch->py < 656)) {
        // Activate backup list only if multiple selections are enabled
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
    if (kdown & KEY_A) {
        // If backup list is active...
        if (g_backupScrollEnabled) {
            // If the "New..." entry is selected...
            if (0 == this->index(CELLS)) {
                if (!getPKSMBridgeFlag()) {
                    auto result = io::backup(this->index(TITLES), g_currentUId, this->index(CELLS));
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
                    auto result = recvFromPKSMBridge(this->index(TITLES), g_currentUId, this->index(CELLS));
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
                            auto result = io::restore(this->index(TITLES), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
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
    if (kdown & KEY_B || (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH && (int)touch->px >= 0 && (int)touch->px <= 532 && (int)touch->py >= 0 &&
                             (int)touch->py <= 664)) {
        this->index(CELLS, 0);
        g_backupScrollEnabled = false;
        entryType(TITLES);
        MS::clearSelectedEntries();
        setPKSMBridgeFlag(false);
        updateButtons(); // Do this last
    }

    // Handle pressing X
    if (kdown & KEY_X) {
        if (g_backupScrollEnabled) {
            size_t index = this->index(CELLS);
            if (index > 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Delete selected backup?",
                    [this, index]() {
                        Title title;
                        getTitle(title, g_currentUId, this->index(TITLES));
                        std::string path = title.fullPath(index);
                        io::deleteFolderRecursively((path + "/").c_str());
                        refreshDirectories(title.id());
                        this->index(CELLS, index - 1);
                        this->removeOverlay();
                    },
                    [this]() { this->removeOverlay(); });
            }
        }
    }

    // Handle pressing Y
    // Backup list active:   Deactivate backup list, select title, and
    //                       enable backup button
    // Backup list inactive: Select title and enable backup button
    if (kdown & KEY_Y) {
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
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_Y && !(g_backupScrollEnabled)) {
        selectionTimer++;
    }
    else {
        selectionTimer = 0;
    }

    if (selectionTimer > 45) {
        MS::clearSelectedEntries();
        for (size_t i = 0, sz = getTitleCount(g_currentUId); i < sz; i++) {
            MS::addSelectedEntry(i);
        }
        selectionTimer = 0;
    }

    // Handle pressing/touching L
    if (buttonBackup->released() || (kdown & KEY_L)) {
        if (MS::multipleSelectionEnabled()) {
            resetIndex(CELLS);
            std::vector<size_t> list = MS::selectedEntries();
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                // check if multiple selection is enabled and don't ask for confirmation if that's the case
                auto result = io::backup(list.at(i), g_currentUId, this->index(CELLS));
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
                            auto result = sendToPKSMBrigde(this->index(TITLES), g_currentUId, this->index(CELLS));
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
                        auto result = io::backup(this->index(TITLES), g_currentUId, this->index(CELLS));
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
    if (buttonRestore->released() || (kdown & KEY_R)) {
        if (g_backupScrollEnabled) {
            if (getPKSMBridgeFlag() && this->index(CELLS) != 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Receive save from PKSM?",
                    [this]() {
                        auto result = recvFromPKSMBridge(this->index(TITLES), g_currentUId, this->index(CELLS));
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
                            auto result = io::restore(this->index(TITLES), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
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

    if ((buttonCheats->released() || (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_RSTICK)) && CheatManager::getInstance().cheats() != nullptr) {
        if (MS::multipleSelectionEnabled()) {
            MS::clearSelectedEntries();
            updateButtons();
        }
        else {
            Title title;
            getTitle(title, g_currentUId, this->index(TITLES));
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
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c5);
        buttonCheats->setColors(theme().c2, theme().c5);
    }
    else if (g_backupScrollEnabled) {
        buttonBackup->canChangeColorWhenSelected(true);
        buttonRestore->canChangeColorWhenSelected(true);
        buttonCheats->canChangeColorWhenSelected(true);
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c6);
        buttonCheats->setColors(theme().c2, theme().c6);
    }
    else {
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c6);
        buttonCheats->setColors(theme().c2, theme().c6);
    }

    if (getPKSMBridgeFlag()) {
        buttonBackup->text("Send \ue004");
        buttonRestore->text("Receive \ue005");
    }
    else {
        buttonBackup->text("Backup \ue004");
        buttonRestore->text("Restore \ue005");
    }
}