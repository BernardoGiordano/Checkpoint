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

#ifndef CHEATMANAGER_HPP
#define CHEATMANAGER_HPP

#include "io.hpp"
#include "json.hpp"
#include "main.hpp"
#include <3ds.h>
#include <bzlib.h>
#include <stdio.h>
#include <sys/stat.h>

#define SELECTED_MAGIC "\uE071 "

class CheatManager {
public:
    static CheatManager& getInstance(void)
    {
        static CheatManager mCheatManager;
        return mCheatManager;
    }

    bool areCheatsAvailable(const std::string& key);
    void save(const std::string& key, const std::vector<std::string>& s);

    std::shared_ptr<nlohmann::json> cheats(void) { return mCheats; }

private:
    CheatManager(void);
    ~CheatManager(void){};

    CheatManager(CheatManager const&) = delete;
    void operator=(CheatManager const&) = delete;

    std::shared_ptr<nlohmann::json> mCheats;
};

#endif