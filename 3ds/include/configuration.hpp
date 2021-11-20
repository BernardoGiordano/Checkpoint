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

#ifndef CONFIGHANDLER_HPP
#define CONFIGHANDLER_HPP

#include "io.hpp"
#include "json.hpp"
#include "util.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    bool dsiwareSaves(void);
    bool nandSaves(void);
    bool shouldScanCard(void);
    void disableDSiWareSaves();
    std::vector<std::u16string> additionalSaveFolders(u64 id);
    std::vector<std::u16string> additionalExtdataFolders(u64 id);

private:
    Configuration(void);
    ~Configuration() = default;

    void editJsonElement(const char* element, nlohmann::json value);

    void store(void);
    nlohmann::json loadJson(const std::string& path);
    void storeJson(nlohmann::json& json, const std::string& path);

    Configuration(Configuration const&) = delete;
    void operator=(Configuration const&) = delete;

    nlohmann::json mJson;
    std::unordered_set<u64> mFilterIds, mFavoriteIds;
    std::unordered_map<u64, std::vector<std::u16string>> mAdditionalSaveFolders, mAdditionalExtdataFolders;
    bool mDSiWareSaves, mNandSaves, mScanCard;
    std::string BASEPATH = "/3ds/Checkpoint/config.json";
};

#endif