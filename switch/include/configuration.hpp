/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef CONFIGHANDLER_HPP
#define CONFIGHANDLER_HPP

#include "io.hpp"
#include "nlohmann/json.hpp"
#include "util.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
extern "C" {
#include "mongoose.h"
}

#define CONFIG_VERSION 4

class Configuration {
public:
    static Configuration& getInstance(void)
    {
        static Configuration mConfiguration;
        return mConfiguration;
    }

    bool filter(u64 id);
    bool favorite(u64 id);
    bool isPKSMBridgeEnabled(void);
    bool isFTPEnabled(void);
    std::vector<std::string> additionalSaveFolders(u64 id);
    void pollServer(void);
    void save(void);
    void load(void);
    void parse(void);
    const char* c_str(void);
    nlohmann::json getJson(void);

    const std::string BASEPATH = "/switch/Checkpoint/config.json";

private:
    Configuration(void);
    ~Configuration(void);

    void store(void);

    Configuration(Configuration const&)  = delete;
    void operator=(Configuration const&) = delete;

    nlohmann::json mJson;
    bool PKSMBridgeEnabled;
    bool FTPEnabled;
    std::unordered_set<u64> mFilterIds, mFavoriteIds;
    std::unordered_map<u64, std::vector<std::string>> mAdditionalSaveFolders;
};

#endif