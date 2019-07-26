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

CheatManager::CheatManager(void)
{
    mCheats = nullptr;
    if (io::fileExists("/switch/Checkpoint/cheats.json")) {
        const std::string path = "/switch/Checkpoint/cheats.json";
        FILE* in               = fopen(path.c_str(), "rt");
        if (in != NULL) {
            mCheats = std::make_shared<nlohmann::json>(nlohmann::json::parse(in, nullptr, false));
            fclose(in);
        }
        else {
            Logger::getInstance().log(Logger::WARN, "Failed to open " + path + " with errno %d.", errno);
        }
    }
    else {
        const std::string path = "romfs:/cheats/cheats.json.bz2";
        // load compressed archive in memory
        FILE* f = fopen(path.c_str(), "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            u32 size             = ftell(f);
            unsigned int destLen = CHEAT_SIZE_DECOMPRESSED;
            char* s              = new char[size];
            char* d              = new char[destLen]();
            rewind(f);
            fread(s, 1, size, f);

            int r = BZ2_bzBuffToBuffDecompress(d, &destLen, s, size, 0, 0);
            if (r == BZ_OK) {
                mCheats = std::make_shared<nlohmann::json>(nlohmann::json::parse(d));
            }

            delete[] s;
            delete[] d;
            fclose(f);
        }
        else {
            Logger::getInstance().log(Logger::WARN, "Failed to open " + path + " with errno %d.", errno);
        }
    }
}

bool CheatManager::areCheatsAvailable(const std::string& key)
{
    return mCheats->find(key) != mCheats->end();
}

void CheatManager::save(const std::string& key, const std::vector<std::string>& s)
{
    static size_t MAGIC_LEN = strlen(SELECTED_MAGIC);

    std::string idfolder   = StringUtils::format("/atmosphere/titles/%s", key.c_str());
    std::string rootfolder = idfolder + "/cheats";
    mkdir(idfolder.c_str(), 777);
    mkdir(rootfolder.c_str(), 777);

    auto cheats = *CheatManager::getInstance().cheats().get();
    for (auto it = cheats[key].begin(); it != cheats[key].end(); ++it) {
        std::string buildid   = it.key();
        std::string cheatFile = "";
        for (size_t i = 0; i < s.size(); i++) {
            std::string cellName = s.at(i);
            if (cellName.compare(0, MAGIC_LEN, SELECTED_MAGIC) == 0) {
                cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                if (cheats[key][buildid].find(cellName) != cheats[key][buildid].end()) {
                    cheatFile += "[" + cellName + "]\n";
                    for (auto& it2 : cheats[key][buildid][cellName]) {
                        cheatFile += it2.get<std::string>() + "\n";
                    }
                    cheatFile += "\n";
                }
            }
        }

        std::string outPath = rootfolder + "/" + buildid + ".txt";
        FILE* f             = fopen(outPath.c_str(), "w");
        if (f != NULL) {
            fwrite(cheatFile.c_str(), 1, cheatFile.length(), f);
            fclose(f);
        }
        else {
            Logger::getInstance().log(Logger::ERROR, "Failed to write " + outPath + " with errno %d.", errno);
        }
    }
}