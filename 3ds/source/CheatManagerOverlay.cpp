/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2021 Bernardo Giordano, FlagBrew
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

    FILE* f = fopen(("/cheats/" + key + ".txt").c_str(), "r");
    if (f != NULL) {
        fseek(f, 0, SEEK_END);
        u32 size = ftell(f);
        char* s  = new char[size];
        rewind(f);
        fread(s, 1, size, f);
        existingCheat = std::string(s);
        delete[] s;
        fclose(f);
    }

    size_t i     = 0;
    currentIndex = i;
    scrollable   = std::make_unique<Scrollable>(2, 2, 396, 220, 11);
    auto cheats  = *CheatManager::getInstance().cheats().get();
    for (auto it = cheats[key].begin(); it != cheats[key].end(); ++it) {
        std::string value = it.key();
        if (existingCheat.find(value) != std::string::npos) {
            value = SELECTED_MAGIC + value;
        }
        scrollable->push_back(COLOR_GREY_DARKER, COLOR_WHITE, value, i == 0);
        i++;
    }

    staticBuf  = C2D_TextBufNew(48);
    dynamicBuf = C2D_TextBufNew(16);
    C2D_TextParse(&multiSelectText, staticBuf, "\uE003 to select all cheats");
    C2D_TextParse(&multiDeselectText, staticBuf, "\uE003 to deselect all cheats");
    C2D_TextOptimize(&multiSelectText);
    C2D_TextOptimize(&multiDeselectText);
}

CheatManagerOverlay::~CheatManagerOverlay(void)
{
    C2D_TextBufDelete(staticBuf);
    C2D_TextBufDelete(dynamicBuf);
}

void CheatManagerOverlay::drawTop(void) const
{
    C2D_TextBufClear(dynamicBuf);

    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
    C2D_Text page;
    C2D_TextParse(&page, dynamicBuf, StringUtils::format("%d/%d", scrollable->index() + 1, scrollable->size()).c_str());
    C2D_TextOptimize(&page);
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_GREY_DARK);
    scrollable->draw(true);
    C2D_DrawText(&page, C2D_WithColor, ceilf(396 - page.width * scale), 224, 0.5f, scale, scale, COLOR_WHITE);
    C2D_DrawText(multiSelected ? &multiDeselectText : &multiSelectText, C2D_WithColor, 4, 224, 0.5f, scale, scale, COLOR_WHITE);
}

void CheatManagerOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
}

void CheatManagerOverlay::update(const InputState& input)
{
    (void)input;
    if (hidKeysDown() & KEY_A) {
        std::string cellName = scrollable->cellName(scrollable->index());
        if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
            // cheat was already selected
            cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
        }
        else {
            cellName = SELECTED_MAGIC + cellName;
        }
        scrollable->c2dText(scrollable->index(), cellName);
    }

    if (hidKeysDown() & KEY_Y) {
        if (multiSelected) {
            for (size_t j = 0; j < scrollable->size(); j++) {
                std::string cellName = scrollable->cellName(j);
                if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                    cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                    scrollable->c2dText(j, cellName);
                }
            }
            multiSelected = false;
        }
        else {
            for (size_t j = 0; j < scrollable->size(); j++) {
                std::string cellName = scrollable->cellName(j);
                if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) != 0) {
                    cellName = SELECTED_MAGIC + cellName;
                    scrollable->c2dText(j, cellName);
                }
            }
            multiSelected = true;
        }
    }

    scrollable->updateSelection();
    scrollable->selectRow(currentIndex, false);
    scrollable->selectRow(scrollable->index(), true);
    currentIndex = scrollable->index();

    if (hidKeysDown() & KEY_B) {
        g_selectedCheatKey = key;
        g_selectedCheatCodes.clear();
        for (size_t i = 0; i < scrollable->size(); i++) {
            g_selectedCheatCodes.push_back(scrollable->cellName(i));
        }
        me = std::make_shared<YesNoOverlay>(
            screen, "Do you want to store\nthe cheat file?",
            []() {
                CheatManager::getInstance().save(g_selectedCheatKey, g_selectedCheatCodes);
                g_screen->removeOverlay();
            },
            []() { g_screen->removeOverlay(); });
    }
}