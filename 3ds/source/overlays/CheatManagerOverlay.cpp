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
#include "CheatManager.hpp"
#include "stringutils.hpp"
#include "fileptr.hpp"
#include "platform.hpp"
#include "YesNoOverlay.hpp"

CheatManagerOverlay::CheatManagerOverlay(Screen& screen, DataHolder& data, const std::string& mkey)
:
DualScreenOverlay(screen), scrollable(2, 2, 396, 220, 11)
{
    key           = mkey;
    multiSelected = false;

    {
        const std::string outPath = StringUtils::format(Platform::Formats::CheatPath, key.c_str());
        FilePtr f = openFile(outPath.c_str(), "rt");
        if (f) {
            fseek(f.get(), 0, SEEK_END);
            u32 size = ftell(f.get());
            auto s  = std::make_unique<char[]>(size);
            rewind(f.get());
            fread(s.get(), 1, size, f.get());
            existingCheat = std::string(s.get(), s.get() + size);
        }
    }

    size_t i     = 0;
    currentIndex = i;
    auto cheatsPtr  = data.getCheats().cheats();
    const auto& cheats = *cheatsPtr;
    for (const auto& [k, v] : cheats[key].items()) {
        std::string value = k;
        if (existingCheat.find(value) != std::string::npos) {
            value.insert(value.begin(), CheatManager::SELECTED_MAGIC.begin(), CheatManager::SELECTED_MAGIC.end());
        }
        scrollable.push_back(COLOR_GREY_DARKER, COLOR_WHITE, value, i == 0);
        i++;
    }
}

void CheatManagerOverlay::drawTop(DrawDataHolder& d) const
{
    C2D_Text multiInfoText;
    d.citro.dynamicText(&multiInfoText, multiSelected ? "\uE003 to deselect all cheats" : "\uE003 to select all cheats");

    C2D_Text page;
    d.citro.dynamicText(&page, StringUtils::format("%d/%d", scrollable.index() + 1, scrollable.size()).c_str());

    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_GREY_DARK);
    scrollable.draw(d, true);
    C2D_DrawText(&page, C2D_WithColor, ceilf(396 - page.width * scale), 224, 0.5f, scale, scale, COLOR_WHITE);
    C2D_DrawText(&multiInfoText, C2D_WithColor, 4, 224, 0.5f, scale, scale, COLOR_WHITE);
}

void CheatManagerOverlay::drawBottom(DrawDataHolder& d) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
}

void CheatManagerOverlay::update(InputDataHolder& input)
{
    if (input.kDown & KEY_A) {
        std::string cellName = scrollable.cellName(scrollable.index());
        if (cellName.compare(0, CheatManager::SELECTED_MAGIC.size(), CheatManager::SELECTED_MAGIC) == 0) {
            // cheat was already selected
            cellName = cellName.substr(CheatManager::SELECTED_MAGIC.size());
        }
        else {
            cellName.insert(cellName.begin(), CheatManager::SELECTED_MAGIC.begin(), CheatManager::SELECTED_MAGIC.end());
        }
        scrollable.c2dText(scrollable.index(), cellName);
    }

    if (input.kDown & KEY_Y) {
        if (multiSelected) {
            for (size_t j = 0; j < scrollable.size(); j++) {
                std::string cellName = scrollable.cellName(j);
                if (cellName.compare(0, CheatManager::SELECTED_MAGIC.size(), CheatManager::SELECTED_MAGIC) == 0) {
                    cellName = cellName.substr(CheatManager::SELECTED_MAGIC.size());
                    scrollable.c2dText(j, cellName);
                }
            }
            multiSelected = false;
        }
        else {
            for (size_t j = 0; j < scrollable.size(); j++) {
                std::string cellName = scrollable.cellName(j);
                if (cellName.compare(0, CheatManager::SELECTED_MAGIC.size(), CheatManager::SELECTED_MAGIC) != 0) {
                    cellName.insert(cellName.begin(), CheatManager::SELECTED_MAGIC.begin(), CheatManager::SELECTED_MAGIC.end());
                    scrollable.c2dText(j, cellName);
                }
            }
            multiSelected = true;
        }
    }

    scrollable.updateSelection(input);
    scrollable.selectRow(currentIndex, false);
    scrollable.selectRow(scrollable.index(), true);
    currentIndex = scrollable.index();

    if (input.kDown & KEY_B) {
        std::vector<std::string> selectedCheatCodes;
        for (size_t i = 0; i < scrollable.size(); i++) {
            selectedCheatCodes.push_back(scrollable.cellName(i));
        }
        me = std::make_shared<YesNoOverlay>(
            screen, "Do you want to store\nthe cheat file?",
            [=](InputDataHolder& i) {
                i.parent.getCheats().save(key, selectedCheatCodes);
                i.currentScreen->removeOverlay();
            },
            [](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
    }
}
