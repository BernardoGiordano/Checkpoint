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

#include "cheatmanager.hpp"

#define SELECTED_MAGIC "\uE071 "

static bool mLoaded = false;
static nlohmann::json mCheats;
static bool multiSelected = false;
static size_t MAGIC_LEN   = strlen(SELECTED_MAGIC);

void CheatManager::init(void)
{
    Gui::updateButtons();

    if (io::fileExists("/switch/Checkpoint/cheats.json")) {
        const std::string path = "/switch/Checkpoint/cheats.json";
        FILE* in               = fopen(path.c_str(), "rt");
        if (in != NULL) {
            mCheats = nlohmann::json::parse(in, nullptr, false);
            fclose(in);
            mLoaded = true;
        }
    }
    else {
        const std::string path = "romfs:/cheats/cheats.json.bz2";
        // load compressed archive in memory
        FILE* f = fopen(path.c_str(), "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            u32 size             = ftell(f);
            unsigned int destLen = 1024 * 1024;
            char* s              = new char[size];
            char* d              = new char[destLen]();
            rewind(f);
            fread(s, 1, size, f);

            int r = BZ2_bzBuffToBuffDecompress(d, &destLen, s, size, 0, 0);
            if (r == BZ_OK) {
                mCheats = nlohmann::json::parse(d);
                mLoaded = true;
            }

            delete[] s;
            delete[] d;
            fclose(f);
        }
    }

    Gui::updateButtons();
}

void CheatManager::exit(void) {}

bool CheatManager::loaded(void)
{
    return mLoaded;
}

bool CheatManager::availableCodes(const std::string& key)
{
    return mCheats.find(key) != mCheats.end();
}

void CheatManager::manageCheats(const std::string& key)
{
    std::string existingCheat = "";

    std::string root = StringUtils::format("/atmosphere/titles/%s/cheats", key.c_str());
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

    size_t i            = 0;
    size_t currentIndex = i;
    Scrollable* s       = new Scrollable(90, 20, 1100, 640, 16);
    for (auto it = mCheats[key].begin(); it != mCheats[key].end(); ++it) {
        std::string buildid = it.key();
        for (auto it2 = mCheats[key][buildid].begin(); it2 != mCheats[key][buildid].end(); ++it2) {
            std::string value = it2.key();
            if (existingCheat.find(value) != std::string::npos) {
                value = SELECTED_MAGIC + value;
            }
            s->push_back(COLOR_GREY_DARKER, COLOR_WHITE, value, i == 0);
            i++;
        }
    }

    while (appletMainLoop()) {
        hidScanInput();

        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B) {
            break;
        }

        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) {
            std::string cellName = s->cellName(s->index());
            if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                // cheat was already selected
                cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
            }
            else {
                cellName = SELECTED_MAGIC + cellName;
            }
            s->text(s->index(), cellName);
        }

        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_Y) {
            if (multiSelected) {
                for (size_t j = 0; j < s->size(); j++) {
                    std::string cellName = s->cellName(j);
                    if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                        cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                        s->text(j, cellName);
                    }
                }
                multiSelected = false;
            }
            else {
                for (size_t j = 0; j < s->size(); j++) {
                    std::string cellName = s->cellName(j);
                    if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) != 0) {
                        cellName = SELECTED_MAGIC + cellName;
                        s->text(j, cellName);
                    }
                }
                multiSelected = true;
            }
        }

        s->updateSelection();
        s->selectRow(currentIndex, false);
        s->selectRow(s->index(), true);
        currentIndex = s->index();

        std::string page = StringUtils::format("%d/%d", s->index() + 1, s->size());
        u32 width, height;
        SDLH_GetTextDimensions(20, page.c_str(), &width, &height);

        SDLH_DrawRect(86, 16, 1108, 680, COLOR_GREY_DARK);
        s->draw(true);
        SDLH_DrawText(20, ceilf(1190 - width), ceilf(664 + (32 - height) / 2), COLOR_WHITE, page.c_str());
        SDLH_DrawText(
            20, 94, ceilf(664 + (32 - height) / 2), COLOR_WHITE, multiSelected ? "\ue003 to deselect all cheats" : "\ue003 to select all cheats");
        SDLH_Render();
    }

    if (Gui::askForConfirmation("Do you want to store the cheat file?")) {
        save(key, s);
    }

    delete s;
}

void CheatManager::save(const std::string& key, Scrollable* s)
{
    std::string idfolder   = StringUtils::format("/atmosphere/titles/%s", key.c_str());
    std::string rootfolder = idfolder + "/cheats";
    mkdir(idfolder.c_str(), 777);
    mkdir(rootfolder.c_str(), 777);

    for (auto it = mCheats[key].begin(); it != mCheats[key].end(); ++it) {
        std::string buildid   = it.key();
        std::string cheatFile = "";
        for (size_t i = 0; i < s->size(); i++) {
            std::string cellName = s->cellName(i);
            if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                if (mCheats[key][buildid].find(cellName) != mCheats[key][buildid].end()) {
                    cheatFile += cellName + "\n";
                    for (auto& it2 : mCheats[key][buildid][cellName]) {
                        cheatFile += it2.get<std::string>() + "\n";
                    }
                    cheatFile += "\n";
                }
            }
        }

        FILE* f = fopen((rootfolder + "/" + buildid + ".txt").c_str(), "w");
        if (f != NULL) {
            fwrite(cheatFile.c_str(), 1, cheatFile.length(), f);
            fclose(f);
        }
        else {
            Gui::showError(errno, "Failed to write cheat file\nto the sd card");
        }
    }
}