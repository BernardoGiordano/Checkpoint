/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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
#include "datetime.hpp"
#include "stringutils.hpp"
#include "archive.hpp"
#include "util.hpp"
#include "keyboard.hpp"
#include "YesNoOverlay.hpp"
#include "InfoOverlay.hpp"
#include "ErrorOverlay.hpp"
#include "CheatManagerOverlay.hpp"

#include "sprites.h"

static constexpr size_t IconRows = 4, IconsPerRow = 8;

MainScreen::MainScreen(DrawDataHolder& d) : hid(IconsPerRow, IconRows),
buttonBackup(204, 102, 110, 35, COLOR_GREY_DARKER, COLOR_WHITE, "Backup \uE004", true),
buttonRestore(204, 139, 110, 35, COLOR_GREY_DARKER, COLOR_WHITE, "Restore \uE005", true),
buttonExtra(204, 176, 110, 36, COLOR_GREY_DARKER, COLOR_WHITE, "Extra", true),
directoryList(6, 102, 196, 110, 5)
{
    buttonBackup.canChangeColorWhenSelected(true);
    buttonRestore.canChangeColorWhenSelected(true);
    buttonExtra.canChangeColorWhenSelected(true);

    previousIndex = 1;
    holdStartTime = 0;

    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    inInstructions = false;

    d.citro.staticText(&ins1, "Hold SELECT to see commands. Press \uE002 for ");
    d.citro.staticText(&ins3, ".");
    d.citro.staticText(&ins4, "Press \uE073 or START to exit.");
    d.citro.staticText(&version, ver);
    d.citro.staticText(&checkpoint, "checkpoint");

    d.citro.staticText(&top_move, "\uE006 to move between targets");
    d.citro.staticText(&top_a, "\uE000 to enter target");
    d.citro.staticText(&top_y, "\uE003 to multiselect a targets");
    d.citro.staticText(&top_my, "\uE003 hold to multiselect all targets");
    d.citro.staticText(&top_b, "\uE001 to exit target or deselect all targets");
    d.citro.staticText(&bot_ts, "\uE01D \uE006 to move\nbetween backups");
    d.citro.staticText(&bot_x, "\uE002 to delete backups");
    d.citro.staticText(&coins, "\uE075");

    C2D_PlainImageTint(&checkboxTint, COLOR_GREY_DARKER, 1.0f);
}

void MainScreen::update(InputDataHolder& input)
{
    if (input.parent.actionOngoing.load()) {
        return;
    }
    else if (!std::get<2>(input.parent.resultInfo).empty()) {
        if (std::get<0>(input.parent.resultInfo) == io::ActionResult::Success) {
            currentOverlay = std::make_shared<InfoOverlay>(*this, input.parent.resultInfo);
        }
        else {
            currentOverlay = std::make_shared<ErrorOverlay>(*this, input.parent.resultInfo);
        }
        input.parent.resultInfo = std::make_tuple(io::ActionResult::Success, 0, "");

        // reset directoryList on any failure/success
        directoryList.resetIndex();
        directoryList.flush();
        directoryList.push_back(COLOR_GREY_DARKER, COLOR_WHITE, "New...", true);

        LightLock_Lock(&input.parent.backupableVectorLock);
        auto& info = input.parent.thingsToActOn[input.parent.selectedType][previousIndex]->getInfo();
        for (const auto& [extraFolderIdx, dir] : info.getBackupsList()) {
            directoryList.push_back(COLOR_GREY_DARKER, COLOR_WHITE, dir, false);
        }
        LightLock_Unlock(&input.parent.backupableVectorLock);

        return;
    }

    if (input.kDown & KEY_START) {
        input.keepRunning = false;
        return;
    }

    bool changedModes = false;
    if (input.kDown & KEY_X) {
        if (enteredTarget) {
            const size_t idx = directoryList.index();
            if (idx > 0) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Delete selected backup?",
                    [this, idx](InputDataHolder& i) {
                        this->directoryList.setIndex(idx - 1);
                        this->directoryList.removeEntry(idx);

                        LightLock_Lock(&i.parent.backupableVectorLock);
                        auto& bak = i.parent.thingsToActOn[i.parent.selectedType][this->hid.fullIndex()];
                        bak->deleteBackup(idx - 1);
                        LightLock_Unlock(&i.parent.backupableVectorLock);

                        i.currentScreen->removeOverlay();
                    },
                    [](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
            }
        }
        else {
            input.parent.selectedType = BackupTypes::after(input.parent.selectedType);
            changedModes = true;
        }
    }
    else if (input.kDown & KEY_A) {
        // If backup list is active...
        if (enteredTarget) {
            // If the "New..." entry is selected...
            if (0 == directoryList.index()) {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Backup selected target?",
                    [this](InputDataHolder& i) {
                        this->performBackup(i);
                        i.currentScreen->removeOverlay();
                    },
                    [](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
            }
            else {
                currentOverlay = std::make_shared<YesNoOverlay>(
                    *this, "Restore selected target?",
                    [this](InputDataHolder& i) { 
                        this->performRestore(i);
                        i.currentScreen->removeOverlay();
                    },
                    [this](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
            }
        }
        else {
            // Activate backup list only if multiple selections are not enabled
            if (input.parent.multiSelectedCount[input.parent.selectedType] == 0) {
                enteredTarget = true;
                updateButtons(input);
            }
        }
    }
    else if (input.kDown & KEY_B) {
        enteredTarget = false;
        LightLock_Lock(&input.parent.backupableVectorLock);
        input.parent.clearMultiSelection();
        LightLock_Lock(&input.parent.backupableVectorLock);
        directoryList.resetIndex();
        updateButtons(input);
        holdStartTime = osGetTime();
    }
    else if (input.kDown & KEY_Y) {
        LightLock_Lock(&input.parent.backupableVectorLock);
        auto& info = input.parent.thingsToActOn[input.parent.selectedType][previousIndex]->getInfo();
        if (info.mMultiSelected) {
            input.parent.multiSelectedCount[input.parent.selectedType]--;
            info.mMultiSelected = false;
        }
        else {
            input.parent.multiSelectedCount[input.parent.selectedType]++;
            info.mMultiSelected = true;
        }
        LightLock_Unlock(&input.parent.backupableVectorLock);
        holdStartTime = osGetTime();
    }
    else if (holdStartTime) {
        if (input.kHeld & KEY_B) {
            if ((osGetTime() - holdStartTime) >= 1000) {
                holdStartTime = 0;
                if (input.parent.titleLoadingComplete.load()) {
                    previousIndex = 1;
                    hid.reset();
                    LightEvent_Signal(&input.parent.titleLoadingThreadBeginEvent);
                }
            }
        }
        else if (input.kHeld & KEY_Y) {
            if ((osGetTime() - holdStartTime) >= 1000) {
                holdStartTime = 0;
                input.parent.setAllMultiSelection();
            }
        }
        else {
            holdStartTime = 0;
        }
    }

    if (buttonBackup.released(input) || (input.kDown & KEY_L)) {
        if (input.parent.multiSelectedCount[input.parent.selectedType] != 0) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, "Backup all the selected targets?",
                [this](InputDataHolder& i) { 
                    this->directoryList.resetIndex();
                    this->performMultiBackup(i);
                    i.currentScreen->removeOverlay();
                },
                [this](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
        }
        else if (enteredTarget) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, "Backup to selected folder?",
                [this](InputDataHolder& i) {
                    this->performBackup(i);
                    i.currentScreen->removeOverlay();
                },
                [this](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
        }
    }

    if (buttonRestore.released(input) || (input.kDown & KEY_R)) {
        size_t cellIndex = directoryList.index();
        if (input.parent.multiSelectedCount[input.parent.selectedType] != 0) {
            LightLock_Lock(&input.parent.backupableVectorLock);
            input.parent.clearMultiSelection();
            LightLock_Lock(&input.parent.backupableVectorLock);
            updateButtons(input);
        }
        else if (enteredTarget && cellIndex > 0) {
            currentOverlay = std::make_shared<YesNoOverlay>(
                *this, "Restore from selected folder?",
                [this](InputDataHolder& i) {
                    this->performRestore(i);
                    i.currentScreen->removeOverlay();
                },
                [](InputDataHolder& i) { i.currentScreen->removeOverlay(); });
        }
    }

    inInstructions = hidKeysHeld() & KEY_SELECT;

    countForCurrentFrame = input.parent.thingsToActOn[input.parent.selectedType].size();
    if (!enteredTarget) {
        if (countForCurrentFrame > 0) {
            hid.update(input, countForCurrentFrame);
            if (previousIndex != hid.fullIndex() || changedModes) {
                previousIndex = hid.fullIndex();

                directoryList.resetIndex();
                directoryList.flush();
                directoryList.push_back(COLOR_GREY_DARKER, COLOR_WHITE, "New...", true);

                LightLock_Lock(&input.parent.backupableVectorLock);
                auto& info = input.parent.thingsToActOn[input.parent.selectedType][previousIndex]->getInfo();
                for (const auto& [extraFolderIdx, dir] : info.getBackupsList()) {
                    directoryList.push_back(COLOR_GREY_DARKER, COLOR_WHITE, dir, false);
                }
                LightLock_Unlock(&input.parent.backupableVectorLock);
            }
        }
    }
    else {
        directoryList.updateSelection(input);
    }

    if (countForCurrentFrame > 0) {
        LightLock_Lock(&input.parent.backupableVectorLock);
        auto& info = input.parent.thingsToActOn[input.parent.selectedType][previousIndex]->getInfo();
        if (info.getSpecialInfo(BackupInfo::SpecialInfo::TitleIsActivityLog) == BackupInfo::SpecialInfoResult::True) {
            buttonExtra.c2dText("\uE075 Coins");
            if (buttonExtra.released(input) || (input.kDown & KEY_TOUCH && input.touch.py < 20 && input.touch.px > 294)) {
                if (!Archive::setPlayCoins()) {
                    currentOverlay = std::make_shared<ErrorOverlay>(*this, std::make_tuple(io::ActionResult::Failure, -1, "Failed to set play coins."));
                }
            }
        }
        else if (info.getSpecialInfo(BackupInfo::SpecialInfo::CanCheat) == BackupInfo::SpecialInfoResult::True) {
            buttonExtra.c2dText("Cheats");
            if (input.parent.multiSelectedCount[input.parent.selectedType] != 0) {
                input.parent.clearMultiSelection();
            }
            else {
                const std::string key = info.getCheatKey();
                if (input.parent.getCheats().areCheatsAvailable(key)) {
                    currentOverlay = std::make_shared<CheatManagerOverlay>(*this, input.parent, key);
                }
                else {
                    currentOverlay = std::make_shared<InfoOverlay>(*this, "No available cheat codes for this title.");
                }
            }
        }
        else {
            buttonExtra.c2dText("Extra");
        }
        LightLock_Unlock(&input.parent.backupableVectorLock);
    }
}

void MainScreen::drawTop(DrawDataHolder& d) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 19, COLOR_GREY_DARK);
    C2D_DrawRectSolid(0, 221, 0.5f, 400, 19, COLOR_GREY_DARK);

    C2D_Text timeText;
    d.citro.dynamicText(&timeText, DateTime::timeStr().c_str());
    C2D_TextOptimize(&timeText);
    C2D_DrawText(&timeText, C2D_WithColor, 4.0f, 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);

    const size_t entriesInPage = hid.maxVisibleEntries();
    const size_t page          = hid.page();
    const size_t max           = hid.maxEntries(countForCurrentFrame);

    if (countForCurrentFrame > 0) {
        LightLock_Lock(&d.parent.backupableVectorLock);
        auto& vec = d.parent.thingsToActOn[d.parent.selectedType];
        for (size_t k = 0; k < max; k++) {
            const size_t actualIndex = k + page * entriesInPage;
            const ldiv_t result = ldiv(k, IconsPerRow);
            const int x = result.rem  * 50;
            const int y = result.quot * 50 + 20;

            auto& info = vec[k]->getInfo();
            C2D_Image titleIcon = info.getIcon();

            if (titleIcon.subtex->width == 48) {
                C2D_DrawImageAt(titleIcon, x + 1, y + 1, 0.25f);
            }
            else {
                C2D_DrawImageAt(titleIcon, x + 9, y + 9, 0.25f);
            }

            if (actualIndex == previousIndex) {
                drawSelector(d, x, y);
            }

            if (info.mMultiSelected) {
                C2D_DrawRectSolid(x + 31, y + 31, 0.75f, 16, 16, COLOR_WHITE);
                C2D_DrawImageAt(d.citro.getImage(sprites_checkbox_idx), x + 27, y + 27, 1.0f, &checkboxTint);
            }

            if (info.favorite()) {
                C2D_DrawRectSolid(x + 31, y + 3, 0.75f, 16, 16, COLOR_GOLD);
                C2D_DrawImageAt(d.citro.getImage(sprites_star_idx), x + 27, y - 1, 1.0f, &checkboxTint);
            }
        }
        LightLock_Unlock(&d.parent.backupableVectorLock);
    }

    C2D_Text ins2;
    d.citro.dynamicText(&ins2, BackupTypes::name(BackupTypes::after(d.parent.selectedType)));
    const float border = ceilf((400 - (ins1.width + ins2.width + ins3.width) * 0.47f) / 2);
    C2D_DrawText(&ins1, C2D_WithColor, border, 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    C2D_DrawText(&ins2, C2D_WithColor, border + ceilf(ins1.width * 0.47f), 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    C2D_DrawText(&ins3, C2D_WithColor, border + ceilf((ins1.width + ins2.width) * 0.47f), 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);

    if (inInstructions) {
        const u32 inst_lh = scaleInst * fontGetInfo(nullptr)->lineFeed;
        const u32 inst_h  = ceilf((240 - scaleInst * inst_lh * 6) / 2);
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
        C2D_DrawText(&top_move, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_move, scaleInst)) / 2), inst_h, 0.9f, scaleInst, scaleInst,
            COLOR_WHITE);
        C2D_DrawText(&top_a, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_a, scaleInst)) / 2), inst_h + inst_lh * 1, 0.9f, scaleInst,
            scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_b, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_b, scaleInst)) / 2), inst_h + inst_lh * 2, 0.9f, scaleInst,
            scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_y, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_y, scaleInst)) / 2), inst_h + inst_lh * 3, 0.9f, scaleInst,
            scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_my, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_my, scaleInst)) / 2), inst_h + inst_lh * 4, 0.9f, scaleInst,
            scaleInst, COLOR_WHITE);
    }

    C2D_DrawText(&version, C2D_WithColor, 400 - 4 - ceilf(0.45f * version.width), 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
    C2D_DrawImageAt(d.citro.getImage(sprites_checkpoint_idx), 400 - 24 - ceilf(version.width * 0.45f), 0.0f, 0.5f, NULL, 1.0f, 1.0f);
    C2D_DrawText(&checkpoint, C2D_WithColor, 400 - 6 - 0.45f * version.width - 0.5f * checkpoint.width - 19, 2.0f, 0.5f, 0.5f, 0.5f, COLOR_WHITE);
}

void MainScreen::drawBottom(DrawDataHolder& d) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 19, COLOR_GREY_DARK);
    C2D_DrawRectSolid(0, 221, 0.5f, 320, 19, COLOR_GREY_DARK);
    if (countForCurrentFrame > 0) {
        LightLock_Lock(&d.parent.backupableVectorLock);
        auto& info = d.parent.thingsToActOn[d.parent.selectedType][previousIndex]->getInfo();
        info.drawInfo(d);
        LightLock_Unlock(&d.parent.backupableVectorLock);

        buttonExtra.draw(d, 0.7, 0);

        C2D_DrawRectSolid(4, 100, 0.5f, 312, 114, COLOR_GREY_DARK);
        directoryList.draw(d, enteredTarget);
        buttonBackup.draw(d, 0.7, 0);
        buttonRestore.draw(d, 0.7, 0);
    }

    if (inInstructions) {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
        C2D_DrawText(&bot_ts, C2D_WithColor, 16, 124, 0.5f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&bot_x, C2D_WithColor, 16, 168, 0.5f, scaleInst, scaleInst, COLOR_WHITE);
        // play coins
        C2D_DrawText(&coins, C2D_WithColor, ceilf(318 - StringUtils::textWidth(coins, scaleInst)), -1, 0.5f, scaleInst, scaleInst, COLOR_WHITE);
    }
}

void MainScreen::drawSelector(DrawDataHolder& d, int x, int y) const
{
    constexpr int w            = 2;
    float highlight_multiplier = fmax(0.0, fabs(fmod(d.citro.getTimer(), 1.0) - 0.5) / 0.5);
    u8 r                       = COLOR_SELECTOR & 0xFF;
    u8 g                       = (COLOR_SELECTOR >> 8) & 0xFF;
    u8 b                       = (COLOR_SELECTOR >> 16) & 0xFF;
    u32 color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);

    C2D_DrawRectSolid(x, y, 0.5f, 50, 50, COLOR_WHITEMASK);
    C2D_DrawRectSolid(x, y, 0.5f, 50, w, color);                      // top
    C2D_DrawRectSolid(x, y + w, 0.5f, w, 50 - 2 * w, color);          // left
    C2D_DrawRectSolid(x + 50 - w, y + w, 0.5f, w, 50 - 2 * w, color); // right
    C2D_DrawRectSolid(x, y + 50 - w, 0.5f, 50, w, color);             // bottom
}

void MainScreen::updateButtons(InputDataHolder& input)
{
    if (input.parent.multiSelectedCount[input.parent.selectedType] != 0) {
        buttonRestore.canChangeColorWhenSelected(true);
        buttonRestore.canChangeColorWhenSelected(false);
        buttonExtra.canChangeColorWhenSelected(false);
        buttonBackup.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore.setColors(COLOR_GREY_DARKER, COLOR_GREY_LIGHT);
        buttonExtra.setColors(COLOR_GREY_DARKER, COLOR_GREY_LIGHT);
    }
    else if (enteredTarget) {
        buttonBackup.canChangeColorWhenSelected(true);
        buttonRestore.canChangeColorWhenSelected(true);
        buttonExtra.canChangeColorWhenSelected(true);
        buttonBackup.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonExtra.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
    }
    else {
        buttonBackup.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonExtra.setColors(COLOR_GREY_DARKER, COLOR_WHITE);
    }
}

void MainScreen::performBackup(InputDataHolder& input)
{
    LightLock_Lock(&input.parent.backupableVectorLock);
    auto& bak = input.parent.thingsToActOn[input.parent.selectedType][hid.fullIndex()];
    size_t selectedIndex = directoryList.index();
    if (selectedIndex == 0) {
        input.backupName.first  = -2; // -2 indicates a new backup
        input.backupName.second = Keyboard::askInput(DateTime::dateTimeStr());
    }
    else {
        input.backupName = bak->getInfo().getBackupsList()[selectedIndex - 1];
    }
    input.parent.beginBackup(bak.get());
    LightLock_Unlock(&input.parent.backupableVectorLock);
}
void MainScreen::performMultiBackup(InputDataHolder& input)
{
    // Multi backup always creates a new timestamp-named folder for every marked target
    input.backupName.first  = -2;
    input.backupName.second = DateTime::dateTimeStr();
    input.parent.beginMultiBackup();
}
void MainScreen::performRestore(InputDataHolder& input)
{
    LightLock_Lock(&input.parent.backupableVectorLock);
    auto& bak = input.parent.thingsToActOn[input.parent.selectedType][hid.fullIndex()];
    input.backupName = bak->getInfo().getBackupsList()[directoryList.index() - 1];
    input.parent.beginRestore(bak.get());
    LightLock_Unlock(&input.parent.backupableVectorLock);
}
