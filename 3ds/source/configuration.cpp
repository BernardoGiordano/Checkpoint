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

#include "configuration.hpp"

Configuration::Configuration(void)
{
    // check for existing config.json files on the sd card, BASEPATH
    if (!io::fileExists(Archive::sdmc(), StringUtils::UTF8toUTF16(BASEPATH.c_str())))
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
        if (mJson.find("extdata_locations") == mJson.end())
        {
            mJson["extdata_locations"] = {
                {"0x00055E00", "0x055D"},
                {"0x0011C400", "0x11C5"},
                {"0x00175E00", "0x1648"},
                {"0x00179600", "0x1794"},
                {"0x00179800", "0x1794"},
                {"0x00179700", "0x1795"},
                {"0x0017A800", "0x1795"},
                {"0x0012DD00", "0x12DC"},
                {"0x0012DE00", "0x12DC"},
                {"0x001B5100", "0x1B50"},
                {"0x001C5100", "0x0BD3"},
                {"0x001D4E00", "0x0BD3"}
            };
            updateJson = true;
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

    // parse nand saves
    mNandSaves = mJson["nand_saves"];

    // parse additional save folders
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it)
    {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::u16string> u16folders;
        for (auto& folder : folders)
        {
            u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
    }

    // parse additional extdata folders
    auto je = mJson["additional_extdata_folders"];
    for (auto it = je.begin(); it != je.end(); ++it)
    {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::u16string> u16folders;
        for (auto& folder : folders)
        {
            u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
        }
        mAdditionalExtdataFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
    }
    
    auto extdata = mJson["extdata_locations"];
    for (auto it = extdata.begin(); it != extdata.end(); ++it)
    {
        u32 key = std::stoul((std::string) it.key(), nullptr, 16);
        u32 value = std::stoul((std::string) it.value(), nullptr, 16);
        extdataLocations.emplace(key, value);
    }
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

bool Configuration::hasAlternateExtdataLocation(u32 id)
{
    return extdataLocations.find(id) != extdataLocations.end();
}

u32 Configuration::getAlternateExtdataLocation(u32 id)
{
    return extdataLocations.find(id)->second;
}
