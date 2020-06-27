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

#include "configdata.hpp"

ConfigDataHolder::ConfigDataHolder(nlohmann::json* jp, bool isAlreadyValid) : anyChange(false)
{
    if(!jp) {
        return;
    }

    jsonPtr = jp;
    auto& json = *jp;

    if(!isAlreadyValid)
    {
        if (json["version"] < CONFIG_VERSION) {
            json["version"] = CONFIG_VERSION;
            anyChange        = true;
        }

        #define IS_INCORRECT(name, typ) (!(json.contains(name) && json[name].typ()))

        if (IS_INCORRECT("nand_saves", is_boolean)) {
            json["nand_saves"] = false;
            anyChange           = true;
        }
        if (IS_INCORRECT("scan_cart", is_boolean)) {
            json["scan_cart"] = false;
            anyChange          = true;
        }
        if (IS_INCORRECT("filter", is_array)) {
            json["filter"] = nlohmann::json::array();
            anyChange       = true;
        }
        if (IS_INCORRECT("favorites", is_array)) {
            json["favorites"] = nlohmann::json::array();
            anyChange          = true;
        }
        if (IS_INCORRECT("additional_wifi_folders", is_array)) {
            json["additional_wifi_folders"] = nlohmann::json::array();
            anyChange                        = true;
        }
        if (IS_INCORRECT("additional_save_folders", is_object)) {
            json["additional_save_folders"] = nlohmann::json::object();
            anyChange                        = true;
        }
        if (IS_INCORRECT("additional_extdata_folders", is_object)) {
            json["additional_extdata_folders"] = nlohmann::json::object();
            anyChange                           = true;
        }

        #undef IS_INCORRECT

        // check every single entry in the arrays...
        for (auto& obj : json["filter"]) {
            if (!obj.is_string()) {
                json["filter"] = nlohmann::json::array();
                anyChange       = true;
                break;
            }
        }
        for (auto& obj : json["favorites"]) {
            if (!obj.is_string()) {
                json["favorites"] = nlohmann::json::array();
                anyChange          = true;
                break;
            }
        }
        for (auto& obj : json["additional_wifi_folders"]) {
            if (!obj.is_string()) {
                json["additional_wifi_folders"] = nlohmann::json::array();
                anyChange                        = true;
                break;
            }
        }
        for (auto& obj : json["additional_save_folders"]) {
            if (!obj.is_object()) {
                json["additional_save_folders"] = nlohmann::json::object();
                anyChange                        = true;
                break;
            }
        }
        for (auto& obj : json["additional_extdata_folders"]) {
            if (!obj.is_object()) {
                json["additional_extdata_folders"] = nlohmann::json::object();
                anyChange                           = true;
                break;
            }
        }
    }

    // parse filters
    for (auto& id : json["filter"]) {
        mFilterIds.emplace(std::stoull(id.get<std::string>(), nullptr, 16));
    }

    // parse favorites
    for (auto& id : json["favorites"]) {
        mFavoriteIds.emplace(std::stoull(id.get<std::string>(), nullptr, 16));
    }

    mNandSaves = json["nand_saves"];
    mScanCard  = json["scan_cart"];

    // parse additional wifi slot folders
    {
        const auto wifiFolders = json["additional_wifi_folders"].get<std::vector<std::string>>();
        mAdditionalWifiSlotsFolders.start = mExtraPaths.size();
        mAdditionalWifiSlotsFolders.duration = wifiFolders.size();
        mExtraPaths.insert(mExtraPaths.end(), wifiFolders.begin(), wifiFolders.end());
    } // destroy the vector that is now useless

    // parse additional save folders
    auto& js = json["additional_save_folders"];
    for (const auto& [key, value] : js.items()) {
        std::vector<std::string> folders = value["folders"];

        ExtraPathsView view;
        view.start = mExtraPaths.size();
        view.duration = folders.size();
        mAdditionalSaveFolders.insert_or_assign(std::stoull(key, nullptr, 16), view);

        mExtraPaths.insert(mExtraPaths.end(), folders.begin(), folders.end());
    }

    // parse additional extdata folders
    auto& je = json["additional_extdata_folders"];
    for (const auto& [key, value] : je.items()) {
        std::vector<std::string> folders = value["folders"];

        ExtraPathsView view;
        view.start = mExtraPaths.size();
        view.duration = folders.size();
        mAdditionalExtdataFolders.insert_or_assign(std::stoull(key, nullptr, 16), std::move(view));

        mExtraPaths.insert(mExtraPaths.end(), folders.begin(), folders.end());
    }
}

bool ConfigDataHolder::filter(uint64_t id)
{
    return mFilterIds.find(id) != mFilterIds.end();
}

bool ConfigDataHolder::favorite(uint64_t id)
{
    return mFavoriteIds.find(id) != mFavoriteIds.end();
}

bool ConfigDataHolder::nandSaves()
{
    return mNandSaves;
}

std::vector<std::string> ConfigDataHolder::additionalWifiSlotsFolders()
{
    return mAdditionalWifiSlotsFolders.duration == 0 ? std::vector<std::string>{} : mAdditionalWifiSlotsFolders.getRange(mExtraPaths);
}
std::vector<std::string> ConfigDataHolder::additionalSaveFolders(uint64_t id)
{
    auto folders = mAdditionalSaveFolders.find(id);
    return folders == mAdditionalSaveFolders.end() ? std::vector<std::string>{} : folders->second.getRange(mExtraPaths);
}

std::vector<std::string> ConfigDataHolder::additionalExtdataFolders(uint64_t id)
{
    auto folders = mAdditionalExtdataFolders.find(id);
    return folders == mAdditionalExtdataFolders.end() ? std::vector<std::string>{} : folders->second.getRange(mExtraPaths);
}

bool ConfigDataHolder::shouldScanCard()
{
    return mScanCard;
}
