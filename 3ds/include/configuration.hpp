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

#ifndef CONFIGURATION_HPP
#define CONFIGURATION_HPP

#include "json.hpp"
#include <3ds/types.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Configuration {
public:
    static constexpr int CURRENT_VERSION = 3;

    static Configuration& getInstance(void)
    {
        static Configuration mConfiguration;
        return mConfiguration;
    }

    bool filter(u64 id);
    bool favorite(u64 id);
    bool nandSaves(void);
    bool shouldScanCard(void);
    std::vector<std::u16string> additionalSaveFolders(u64 id);
    std::vector<std::u16string> additionalExtdataFolders(u64 id);

    void save(void);

private:
    Configuration(void);
    ~Configuration();

    Configuration(Configuration const&)  = delete;
    void operator=(Configuration const&) = delete;

    void loadFromRomfs(void);

    std::unique_ptr<nlohmann::json> mJson;
    std::unordered_set<u64> mFilterIds, mFavoriteIds;
    std::unordered_map<u64, std::vector<std::u16string>> mAdditionalSaveFolders, mAdditionalExtdataFolders;
    bool mNandSaves, mScanCard;
    std::string BASEPATH = "/3ds/Checkpoint/config.json";
    size_t oldSize       = 0;
};

#endif