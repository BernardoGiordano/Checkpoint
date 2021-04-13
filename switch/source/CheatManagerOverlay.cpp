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

#include "CheatManagerOverlay.hpp"

CheatManagerOverlay::CheatManagerOverlay(Screen& screen, const std::string& mkey) : Overlay(screen)
{
    key                       = mkey;
    multiSelected             = false;
    std::string existingCheat = "";

    std::string root = StringUtils::format("/atmosphere/contents/%s/cheats", key.c_str());
    Directory dir(root);
    if (dir.good()) {
        for (size_t i = 0; i < dir.size(); i++) {
            if (!dir.folder(i)) {
                FILE* f = fopen((root + "/" + dir.entry(i)).c_str(), "r");
                if (f != NULL) {
                    fseek(f, 0, SEEK_END);
                    u32 size = ftell(f);
                    char* s  = new char[size];
                    rewind(f);
                    fread(s, 1, size, f);
                    existingCheat += "\n" + std::string(s);
                    delete[] s;
                    fclose(f);
                }
            }
        }
    }

    size_t i     = 0;
    currentIndex = i;
    scrollable   = std::make_shared<Scrollable>(90, 20, 1100, 640, 16);
    auto cheats  = *CheatManager::getInstance().cheats().get();
    for (auto it = cheats[key].begin(); it != cheats[key].end(); ++it) {
        std::string buildid = it.key();
        for (auto it2 = cheats[key][buildid].begin(); it2 != cheats[key][buildid].end(); ++it2) {
            std::string value = it2.key();
            if (existingCheat.find(value) != std::string::npos) {
                value = SELECTED_MAGIC + value;
            }
            scrollable->push_back(COLOR_GREY_DARKER, COLOR_WHITE, value, i == 0);
            i++;
        }
    }
}

void CheatManagerOverlay::draw(void) const
{
    SDLH_DrawRect(0, 0, 1280, 720, COLOR_OVERLAY);
    std::string page = StringUtils::format("%d/%d", scrollable->index() + 1, scrollable->size());
    u32 width, height;
    SDLH_GetTextDimensions(20, page.c_str(), &width, &height);

    SDLH_DrawRect(86, 16, 1108, 680, COLOR_GREY_DARK);
    scrollable->draw(true);
    SDLH_DrawText(20, ceilf(1190 - width), ceilf(664 + (32 - height) / 2), COLOR_WHITE, page.c_str());
    SDLH_DrawText(
        20, 94, ceilf(664 + (32 - height) / 2), COLOR_WHITE, multiSelected ? "\ue003 to deselect all cheats" : "\ue003 to select all cheats");
}

void CheatManagerOverlay::update(PadState* pad)
{
    u64 kDown = padGetButtonsDown(pad);
    if (kDown & HidNpadButton_A) {
        std::string cellName = scrollable->cellName(scrollable->index());
        if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
            // cheat was already selected
            cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
        }
        else {
            cellName = SELECTED_MAGIC + cellName;
        }
        scrollable->text(scrollable->index(), cellName);
    }

    if (kDown & HidNpadButton_Y) {
        if (multiSelected) {
            for (size_t j = 0; j < scrollable->size(); j++) {
                std::string cellName = scrollable->cellName(j);
                if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                    cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                    scrollable->text(j, cellName);
                }
            }
            multiSelected = false;
        }
        else {
            for (size_t j = 0; j < scrollable->size(); j++) {
                std::string cellName = scrollable->cellName(j);
                if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) != 0) {
                    cellName = SELECTED_MAGIC + cellName;
                    scrollable->text(j, cellName);
                }
            }
            multiSelected = true;
        }
    }

    scrollable->updateSelection();
    scrollable->selectRow(currentIndex, false);
    scrollable->selectRow(scrollable->index(), true);
    currentIndex = scrollable->index();

    if (kDown & HidNpadButton_B) {
        g_selectedCheatKey = key;
        g_selectedCheatCodes.clear();
        for (size_t i = 0; i < scrollable->size(); i++) {
            g_selectedCheatCodes.push_back(scrollable->cellName(i));
        }
        me = std::make_shared<YesNoOverlay>(
            screen, "Do you want to store the cheat file?",
            []() {
                CheatManager::getInstance().save(g_selectedCheatKey, g_selectedCheatCodes);
                g_screen->removeOverlay();
            },
            []() { g_screen->removeOverlay(); });
    }
}