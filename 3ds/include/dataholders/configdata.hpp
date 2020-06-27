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

#ifndef CONFIGDATA_HPP
#define CONFIGDATA_HPP

#define CONFIG_VERSION 4

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdint>
#include "json.hpp"

struct ConfigDataHolder {
    ConfigDataHolder(nlohmann::json* jp, bool isAlreadyValid=false);

    bool anyChange;

    bool filter(uint64_t id);
    bool favorite(uint64_t id);
    bool nandSaves();
    bool shouldScanCard();
    std::vector<std::string> additionalWifiSlotsFolders();
    std::vector<std::string> additionalSaveFolders(uint64_t id);
    std::vector<std::string> additionalExtdataFolders(uint64_t id);

private:
    struct ExtraPathsView {
        size_t start, duration;

        std::vector<std::string>::const_iterator begin(const std::vector<std::string>& v) {
            return v.cbegin() + (start);
        }
        std::vector<std::string>::const_iterator end(const std::vector<std::string>& v) {
            return v.cbegin() + (start + duration);
        }

        std::vector<std::string> getRange(const std::vector<std::string>& v) {
            return std::vector<std::string>(begin(v), end(v));
        }
    };

    bool mNandSaves, mScanCard;
    nlohmann::json* jsonPtr;
    std::vector<std::string> mExtraPaths;
    std::unordered_set<uint64_t> mFilterIds, mFavoriteIds;
    ExtraPathsView mAdditionalWifiSlotsFolders;
    std::unordered_map<uint64_t, ExtraPathsView> mAdditionalSaveFolders, mAdditionalExtdataFolders;
};

#endif
