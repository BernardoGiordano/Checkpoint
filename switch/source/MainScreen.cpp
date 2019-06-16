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
    SDL_Color colorBar = Gui::getPKSMBridgeFlag() ? COLOR_HIGHBLUE : FC_MakeColor(theme().c1.r - 15, theme().c1.g - 15, theme().c1.b - 15, 255);
    SDLH_DrawRect(0, 0, 532, 662, FC_MakeColor(theme().c1.r + 5, theme().c1.g + 5, theme().c1.b + 5, 255));
    SDLH_DrawRect(1280 - SIDEBAR_w, 0, SIDEBAR_w, 720, colorBar);

    u32 nick_w, nick_h;
    SDLH_GetTextDimensions(13, Account::username(g_currentUId).c_str(), &nick_w, &nick_h);
    drawPulsingOutline(
        1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE, 2, COLOR_GREEN);
    SDLH_DrawImageScale(
        Account::icon(g_currentUId), 1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE);
    SDLH_DrawTextBox(13, 1280 - SIDEBAR_w + (SIDEBAR_w - nick_w) / 2, 720 - 28 + (28 - nick_h) / 2, theme().c6, SIDEBAR_w,
        Account::username(g_currentUId).c_str());

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
        SDLH_DrawRect(x, y, 124, 124, FC_MakeColor(255, 255, 255, 80));
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
        u32 title_w, title_h, h, titleid_w, producer_w, user_w, subtitle_w;
        auto displayName = title.displayName();
        SDLH_GetTextDimensions(28, displayName.first.c_str(), &title_w, &title_h);
        SDLH_GetTextDimensions(23, "Title: ", &subtitle_w, NULL);
        SDLH_GetTextDimensions(23, "Title ID: ", &titleid_w, &h);
        SDLH_GetTextDimensions(23, "Author: ", &producer_w, NULL);
        SDLH_GetTextDimensions(23, "User: ", &user_w, NULL);

        h += 6;
        u32 offset = 10 + title_h + h / 2;
        int i      = 0;

        SDLH_DrawRect(534, 2, 482, 16 + title_h, theme().c3);
        SDLH_DrawRect(534, offset - h / 2 - 2, 480, h * (displayName.second.length() > 0 ? 4 : 3) + h / 2, theme().c2);

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

        drawOutline(538, 276, 414, 380, 4, theme().c3);
        drawOutline(956, 276, 220, 80, 4, theme().c3);
        drawOutline(956, 360, 220, 80, 4, theme().c3);
        drawOutline(956, 444, 220, 80, 4, theme().c3);
        backupList->draw(g_backupScrollEnabled);
        buttonBackup->draw(30, FC_MakeColor(0, 0, 0, 0));
        buttonRestore->draw(30, FC_MakeColor(0, 0, 0, 0));
        buttonCheats->draw(30, FC_MakeColor(0, 0, 0, 0));
    }

    SDL_Color lightBlack = FC_MakeColor(theme().c0.r + 20, theme().c0.g + 20, theme().c0.b + 20, 255);
    u32 ver_w, ver_h, checkpoint_h, checkpoint_w, inst_w, inst_h;
    SDLH_GetTextDimensions(20, ver, &ver_w, &ver_h);
    SDLH_GetTextDimensions(26, "checkpoint", &checkpoint_w, &checkpoint_h);
    SDLH_GetTextDimensions(24, "\ue046 Instructions", &inst_w, &inst_h);

    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_MINUS) {
        SDLH_DrawRect(0, 0, 1280, 720, FC_MakeColor(0, 0, 0, 190));
        SDLH_DrawText(27, 1205, 646, theme().c6, "\ue085\ue086");
        SDLH_DrawText(24, 58, 69, theme().c6, "\ue058 Tap to select title");
        SDLH_DrawText(24, 100, 330, theme().c6, "\ue006 \ue080 to scroll between titles");
        SDLH_DrawText(24, 100, 360, theme().c6, "\ue000 to enter the selected title");
        SDLH_DrawText(24, 100, 390, theme().c6, "\ue001 to exit the selected title");
        if (Configuration::getInstance().isPKSMBridgeEnabled()) {
            SDLH_DrawText(24, 100, 420, theme().c6, "\ue004 + \ue005 to enable PKSM bridge");
        }
        SDLH_DrawText(24, 616, 450, theme().c6, "\ue002 to delete a backup");
        if (gethostid() != INADDR_LOOPBACK) {
            SDLH_DrawText(24, 16 * 6 + checkpoint_w + 8 + ver_w + inst_w, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, COLOR_GOLD,
                StringUtils::format("Configuration server running on %s:8000", getConsoleIP()).c_str());
        }
    }

    SDLH_DrawRect(0, 672, checkpoint_w + ver_w + 2 * 16 + 8, 40, lightBlack);
    SDLH_DrawText(26, 16, 672 + (40 - checkpoint_h) / 2 + 2, theme().c6, "checkpoint");
    SDLH_DrawText(20, 16 + checkpoint_w + 8, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - ver_h, theme().c6, ver);
    SDLH_DrawText(24, 16 * 3 + checkpoint_w + 8 + ver_w, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, theme().c6, "\ue046 Instructions");
}

void MainScreen::update(touchPosition* touch)
{
    Screen::update();
    updateSelector(touch);
    handleEvents(touch);
}

void MainScreen::updateSelector(touchPosition* touch)
{
    if (!g_backupScrollEnabled) {
        size_t oldindex = hid.index();
        hid.update(getTitleCount(g_currentUId));

        // loop through every rendered title
        for (u8 row = 0; row < rowlen; row++) {
            for (u8 col = 0; col < collen; col++) {
                u8 index = row * collen + col;
                if (index > hid.maxEntries(getTitleCount(g_currentUId)))
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
            Gui::setPKSMBridgeFlag(false);
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
        Gui::setPKSMBridgeFlag(false);
    }
    // handle PKSM bridge
    if (Configuration::getInstance().isPKSMBridgeEnabled()) {
        Title title;
        getTitle(title, g_currentUId, this->index(TITLES));
        if (!Gui::getPKSMBridgeFlag()) {
            if ((kheld & KEY_L) && (kheld & KEY_R) && isPKSMBridgeTitle(title.id())) {
                Gui::setPKSMBridgeFlag(true);
                Gui::updateButtons();
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
        Gui::setPKSMBridgeFlag(false);
    }

    // Handle touching the backup list
    if ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH && (int)touch->px > 538 && (int)touch->px < 952 && (int)touch->py > 276 &&
            (int)touch->py < 656)) {
        // Activate backup list only if multiple selections are enabled
        if (!MS::multipleSelectionEnabled()) {
            g_backupScrollEnabled = true;
            Gui::updateButtons();
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
                if (!Gui::getPKSMBridgeFlag()) {
                    io::backup(this->index(TITLES), g_currentUId, this->index(CELLS));
                }
            }
            else {
                if (Gui::getPKSMBridgeFlag()) {
                    recvFromPKSMBridge(this->index(TITLES), g_currentUId, this->index(CELLS));
                }
                else {
                    io::restore(this->index(TITLES), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
                }
            }
        }
        else {
            // Activate backup list only if multiple selections are not enabled
            if (!MS::multipleSelectionEnabled()) {
                g_backupScrollEnabled = true;
                Gui::updateButtons();
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
        Gui::setPKSMBridgeFlag(false);
        Gui::updateButtons(); // Do this last
    }

    // Handle pressing X
    if (kdown & KEY_X) {
        if (g_backupScrollEnabled) {
            size_t index = this->index(CELLS);
            if (index > 0 && Gui::askForConfirmation("Delete selected backup?")) {
                Title title;
                getTitle(title, g_currentUId, this->index(TITLES));
                std::string path = title.fullPath(index);
                io::deleteFolderRecursively((path + "/").c_str());
                refreshDirectories(title.id());
                this->index(CELLS, index - 1);
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
        Gui::setPKSMBridgeFlag(false);
        Gui::updateButtons(); // Do this last
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
    if (isBackupReleased() || (kdown & KEY_L)) {
        if (MS::multipleSelectionEnabled()) {
            resetIndex(CELLS);
            std::vector<size_t> list = MS::selectedEntries();
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                io::backup(list.at(i), g_currentUId, this->index(CELLS));
            }
            MS::clearSelectedEntries();
            Gui::updateButtons();
            blinkLed(4);
            Gui::showInfo("Progress correctly saved to disk.");
        }
        else if (g_backupScrollEnabled) {
            if (Gui::getPKSMBridgeFlag()) {
                sendToPKSMBrigde(this->index(TITLES), g_currentUId, this->index(CELLS));
            }
            else {
                io::backup(this->index(TITLES), g_currentUId, this->index(CELLS));
            }
        }
    }

    // Handle pressing/touching R
    if (isRestoreReleased() || (kdown & KEY_R)) {
        if (g_backupScrollEnabled) {
            if (Gui::getPKSMBridgeFlag()) {
                recvFromPKSMBridge(this->index(TITLES), g_currentUId, this->index(CELLS));
            }
            else {
                io::restore(this->index(TITLES), g_currentUId, this->index(CELLS), nameFromCell(this->index(CELLS)));
            }
        }
    }

    if ((isCheatReleased() || (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_RSTICK)) && CheatManager::loaded()) {
        if (MS::multipleSelectionEnabled()) {
            MS::clearSelectedEntries();
            Gui::updateButtons();
        }
        else {
            Title title;
            getTitle(title, g_currentUId, this->index(TITLES));
            std::string key = StringUtils::format("%016llX", title.id());
            if (CheatManager::availableCodes(key)) {
                CheatManager::manageCheats(key);
            }
            else {
                Gui::showInfo("No available cheat codes for this title.");
            }
        }
    }
}

bool MainScreen::isBackupReleased(void) const
{
    return buttonBackup->released();
}

bool MainScreen::isRestoreReleased(void) const
{
    return buttonRestore->released();
}

bool MainScreen::isCheatReleased(void) const
{
    return buttonCheats->released();
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