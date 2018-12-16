/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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
    if (!io::fileExists(BASEPATH))
    {
        store();
    }

    // load json config file
    std::ifstream i(BASEPATH);
    i >> mJson;
    i.close();

    bool updateJson = false;
    if (mJson.find("version") == mJson.end())
    {
        // if config is present but is < 3.4.2, override it
        store();
    }
    else 
    {
        // 3.4.2 -> 3.5.0
        if (mJson["version"] < 2)
        {
            mJson["favorites"] = nlohmann::json::array();
            updateJson = true;
        }
        // 3.5.0 -> 3.5.1
        if (mJson["version"] < 3)
        {
            mJson["pksm-bridge"] = true;
            updateJson= true;
        }
    }

    if (updateJson) 
    {
        mJson["version"] = CONFIG_VERSION;
        std::ofstream o(BASEPATH);
        o << std::setw(2) << mJson << std::endl;
        o.close();
    }

    // parse filters
    std::vector<std::string> filter = mJson["filter"];
    for (auto& id : filter)
    {
        mFilterIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    // parse favorites
    std::vector<std::string> favorites = mJson["favorites"];
    for (auto& id : favorites)
    {
        mFavoriteIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    // parse additional save folders
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it)
    {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::string> sfolders;
        for (auto& folder : folders)
        {
            sfolders.push_back(folder);
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), sfolders);
    }

    // parse PKSM Bridge flag
    PKSMBridgeEnabled = mJson["pksm-bridge"];
}

void Configuration::store(void)
{
    std::ifstream src("romfs:/config.json");
    std::ofstream dst(BASEPATH);
    dst << src.rdbuf();
    src.close();
    dst.close();
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