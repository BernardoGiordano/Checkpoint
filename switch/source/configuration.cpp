/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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
#include "titlecatalog.hpp"

Configuration::Configuration(void)
{
    // check for existing config.json files on the sd card, BASEPATH
    if (!io::fileExists(BASEPATH)) {
        store();
    }

    load();

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
        if (!(mJson.contains("pksm-bridge") && mJson["pksm-bridge"].is_boolean())) {
            mJson["pksm-bridge"] = false;
            updateJson           = true;
        }
        if (!(mJson.contains("ftp-enabled") && mJson["ftp-enabled"].is_boolean())) {
            mJson["ftp-enabled"] = false;
            updateJson           = true;
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
    }

    if (updateJson) {
        mJson["version"] = CONFIG_VERSION;
        save();
    }

    parse();
}

Configuration::~Configuration(void) {}

void Configuration::store(void)
{
    FILE* in = fopen("romfs:/config.json", "rt");
    if (in != NULL) {
        nlohmann::json src = nlohmann::json::parse(in, nullptr, false);
        fclose(in);

        std::string writeData = src.dump(2);
        writeData.shrink_to_fit();
        size_t size = writeData.size();

        FILE* out = fopen(BASEPATH.c_str(), "wt");
        if (out != NULL) {
            fwrite(writeData.c_str(), 1, size, out);
            fclose(out);
        }
    }
}

bool Configuration::filter(u64 id)
{
    return mFilterIds.find(id) != mFilterIds.end();
}

bool Configuration::favorite(u64 id)
{
    return mFavoriteIds.find(id) != mFavoriteIds.end();
}

std::vector<std::string> Configuration::additionalSaveFolders(u64 id)
{
    std::vector<std::string> emptyvec;
    auto folders = mAdditionalSaveFolders.find(id);
    return folders == mAdditionalSaveFolders.end() ? emptyvec : folders->second;
}

bool Configuration::isPKSMBridgeEnabled(void)
{
    return PKSMBridgeEnabled;
}

void Configuration::save(void)
{
    std::string writeData = mJson.dump(2);
    writeData.shrink_to_fit();
    size_t size = writeData.size();

    FILE* out = fopen(BASEPATH.c_str(), "wt");
    if (out != NULL) {
        fwrite(writeData.c_str(), 1, size, out);
        fclose(out);
    }
}

void Configuration::load(void)
{
    FILE* in = fopen(BASEPATH.c_str(), "rt");
    if (in != NULL) {
        mJson = nlohmann::json::parse(in, nullptr, false);
        fclose(in);
    }
}

void Configuration::parse(void)
{
    mFilterIds.clear();
    mFavoriteIds.clear();
    mAdditionalSaveFolders.clear();

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

    // parse additional save folders
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::string> sfolders;
        for (auto& folder : folders) {
            sfolders.push_back(folder);
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), sfolders);
    }

    // parse PKSM Bridge flag
    PKSMBridgeEnabled = mJson["pksm-bridge"];
    // parse FTP flag
    FTPEnabled = mJson["ftp-enabled"];
}

const char* Configuration::c_str(void)
{
    return mJson.dump().c_str();
}

nlohmann::json Configuration::getJson(void)
{
    return mJson;
}

bool Configuration::isFTPEnabled(void)
{
    return FTPEnabled;
}
