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

#include "configuration.hpp"

Configuration::Configuration(void)
{
    // check for existing config.json files on the sd card, BASEPATH
    if (!io::fileExists(Archive::sdmc(), StringUtils::UTF8toUTF16(BASEPATH.c_str()))) {
        store();
    }

    mJson = loadJson(BASEPATH);

    // check if json is valid
    if (!mJson.is_object()) {
        store();
    }

    bool updateJson = false;
    if (mJson.find("version") == mJson.end()) {
        // if config is present but is < 3.4.2, override it
        store();
    }
    else {
        if (mJson["version"] < CONFIG_VERSION) {
            mJson["version"] = CONFIG_VERSION;
            updateJson       = true;
        }
        if (!(mJson.contains("nand_saves") && mJson["nand_saves"].is_boolean())) {
            mJson["nand_saves"] = false;
            updateJson          = true;
        }
        if (!(mJson.contains("scan_cart") && mJson["scan_cart"].is_boolean())) {
            mJson["scan_cart"] = false;
            updateJson         = true;
        }
        if (!(mJson.contains("filter") && mJson["filter"].is_array())) {
            mJson["filter"] = nlohmann::json::array();
            updateJson      = true;
        }
        if (!(mJson.contains("favorites") && mJson["favorites"].is_array())) {
            mJson["favorites"] = nlohmann::json::array();
            updateJson         = true;
        }
        if (!(mJson.contains("additional_save_folders") && mJson["additional_save_folders"].is_object())) {
            mJson["additional_save_folders"] = nlohmann::json::object();
            updateJson                       = true;
        }
        if (!(mJson.contains("additional_extdata_folders") && mJson["additional_extdata_folders"].is_object())) {
            mJson["additional_extdata_folders"] = nlohmann::json::object();
            updateJson                          = true;
        }
        // check every single entry in the arrays...
        for (auto& obj : mJson["filter"]) {
            if (!obj.is_string()) {
                mJson["filter"] = nlohmann::json::array();
                updateJson      = true;
                break;
            }
        }
        for (auto& obj : mJson["favorites"]) {
            if (!obj.is_string()) {
                mJson["favorites"] = nlohmann::json::array();
                updateJson         = true;
                break;
            }
        }
        for (auto& obj : mJson["additional_save_folders"]) {
            if (!obj.is_object()) {
                mJson["additional_save_folders"] = nlohmann::json::object();
                updateJson                       = true;
                break;
            }
        }
        for (auto& obj : mJson["additional_extdata_folders"]) {
            if (!obj.is_object()) {
                mJson["additional_extdata_folders"] = nlohmann::json::object();
                updateJson                          = true;
                break;
            }
        }
    }

    if (updateJson) {
        mJson["version"] = CONFIG_VERSION;
        storeJson(mJson, BASEPATH);
    }

    // parse filters
    std::vector<std::string> filter = mJson["filter"];
    for (auto& id : filter) {
        mFilterIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    // parse favorites
    std::vector<std::string> favorites = mJson["favorites"];
    for (auto& id : favorites) {
        mFavoriteIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    mNandSaves = mJson["nand_saves"];
    mScanCard  = mJson["scan_cart"];

    // parse additional save folders
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::u16string> u16folders;
        for (auto& folder : folders) {
            u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
    }

    // parse additional extdata folders
    auto je = mJson["additional_extdata_folders"];
    for (auto it = je.begin(); it != je.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::u16string> u16folders;
        for (auto& folder : folders) {
            u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
        }
        mAdditionalExtdataFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
    }
}

nlohmann::json Configuration::loadJson(const std::string& path)
{
    nlohmann::json json;
    FILE* in = fopen(path.c_str(), "rt");
    if (in != NULL) {
        json = nlohmann::json::parse(in, nullptr, false);
        fclose(in);
    }
    return json;
}

void Configuration::storeJson(nlohmann::json& json, const std::string& path)
{
    std::string writeData = json.dump(2);
    writeData.shrink_to_fit();
    size_t size = writeData.size();

    FILE* out = fopen(path.c_str(), "wt");
    if (out != NULL) {
        fwrite(writeData.c_str(), 1, size, out);
        fclose(out);
    }
}

void Configuration::store(void)
{
    nlohmann::json src = loadJson("romfs:/config.json");
    storeJson(src, BASEPATH);
}

bool Configuration::filter(u64 id)
{
    return mFilterIds.find(id) != mFilterIds.end();
}

bool Configuration::favorite(u64 id)
{
    return mFavoriteIds.find(id) != mFavoriteIds.end();
}

bool Configuration::nandSaves(void)
{
    return mNandSaves;
}

std::vector<std::u16string> Configuration::additionalSaveFolders(u64 id)
{
    std::vector<std::u16string> emptyvec;
    auto folders = mAdditionalSaveFolders.find(id);
    return folders == mAdditionalSaveFolders.end() ? emptyvec : folders->second;
}

std::vector<std::u16string> Configuration::additionalExtdataFolders(u64 id)
{
    std::vector<std::u16string> emptyvec;
    auto folders = mAdditionalExtdataFolders.find(id);
    return folders == mAdditionalExtdataFolders.end() ? emptyvec : folders->second;
}

bool Configuration::shouldScanCard(void)
{
    return mScanCard;
}
