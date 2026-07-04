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

#include "configuration.hpp"
#include "archive.hpp"
#include "io.hpp"
#include "util.hpp"
#include <algorithm>

Configuration::Configuration(void)
{
    // check for existing config.json files on the sd card, BASEPATH
    if (!io::fileExists(Archive::sdmc(), StringUtils::UTF8toUTF16(BASEPATH.c_str()))) {
        loadFromRomfs();
    }
    else {
        FILE* in = fopen(BASEPATH.c_str(), "rt");
        if (in != NULL) {
            mJson = std::make_unique<nlohmann::json>(nlohmann::json::parse(in, nullptr, false));
            fclose(in);

            // check if json is valid
            if (!mJson->is_object()) {
                loadFromRomfs();
                return;
            }

            bool updateJson = false;
            if (mJson->find("version") == mJson->end()) {
                // if config is present but is < 3.4.2, override it
                loadFromRomfs();
                return;
            }
            else {
                if ((*mJson)["version"] < CURRENT_VERSION) {
                    (*mJson)["version"] = CURRENT_VERSION;
                    updateJson          = true;
                }
                if (!(mJson->contains("nand_saves") && (*mJson)["nand_saves"].is_boolean())) {
                    (*mJson)["nand_saves"] = false;
                    updateJson             = true;
                }
                if (!(mJson->contains("dsiware_saves") && (*mJson)["dsiware_saves"].is_boolean())) {
                    (*mJson)["dsiware_saves"] = false;
                    updateJson                = true;
                }
                if (!(mJson->contains("scan_cart") && (*mJson)["scan_cart"].is_boolean())) {
                    (*mJson)["scan_cart"] = false;
                    updateJson            = true;
                }
                if (!(mJson->contains("transfer_enabled") && (*mJson)["transfer_enabled"].is_boolean())) {
                    (*mJson)["transfer_enabled"] = false;
                    updateJson                   = true;
                }
                if (!(mJson->contains("confirm_restore") && (*mJson)["confirm_restore"].is_boolean())) {
                    (*mJson)["confirm_restore"] = true;
                    updateJson                  = true;
                }
                if (!(mJson->contains("theme") && (*mJson)["theme"].is_string())) {
                    (*mJson)["theme"] = "dark";
                    updateJson        = true;
                }
                if (!(mJson->contains("filter") && (*mJson)["filter"].is_array())) {
                    (*mJson)["filter"] = nlohmann::json::array();
                    updateJson         = true;
                }
                if (!(mJson->contains("favorites") && (*mJson)["favorites"].is_array())) {
                    (*mJson)["favorites"] = nlohmann::json::array();
                    updateJson            = true;
                }
                if (!(mJson->contains("additional_save_folders") && (*mJson)["additional_save_folders"].is_object())) {
                    (*mJson)["additional_save_folders"] = nlohmann::json::object();
                    updateJson                          = true;
                }
                if (!(mJson->contains("additional_extdata_folders") && (*mJson)["additional_extdata_folders"].is_object())) {
                    (*mJson)["additional_extdata_folders"] = nlohmann::json::object();
                    updateJson                             = true;
                }
                // check every single entry in the arrays...
                for (auto& obj : (*mJson)["filter"]) {
                    if (!obj.is_string()) {
                        (*mJson)["filter"] = nlohmann::json::array();
                        updateJson         = true;
                        break;
                    }
                }
                for (auto& obj : (*mJson)["favorites"]) {
                    if (!obj.is_string()) {
                        (*mJson)["favorites"] = nlohmann::json::array();
                        updateJson            = true;
                        break;
                    }
                }
                for (auto& obj : (*mJson)["additional_save_folders"]) {
                    if (!obj.is_object()) {
                        (*mJson)["additional_save_folders"] = nlohmann::json::object();
                        updateJson                          = true;
                        break;
                    }
                }
                for (auto& obj : (*mJson)["additional_extdata_folders"]) {
                    if (!obj.is_object()) {
                        (*mJson)["additional_extdata_folders"] = nlohmann::json::object();
                        updateJson                             = true;
                        break;
                    }
                }
            }

            if (updateJson) {
                (*mJson)["version"] = CURRENT_VERSION;
                save();
            }

            // parse filters
            std::vector<std::string> filter = (*mJson)["filter"];
            for (auto& id : filter) {
                mFilterIds.emplace(strtoull(id.c_str(), NULL, 16));
            }

            // parse favorites
            std::vector<std::string> favorites = (*mJson)["favorites"];
            for (auto& id : favorites) {
                mFavoriteIds.emplace(strtoull(id.c_str(), NULL, 16));
            }

            mNandSaves       = (*mJson)["nand_saves"];
            mDSiWareSaves    = (*mJson)["dsiware_saves"];
            mScanCard        = (*mJson)["scan_cart"];
            mTransferEnabled = (*mJson)["transfer_enabled"];
            mConfirmRestore  = (*mJson)["confirm_restore"];
            mTheme           = (*mJson)["theme"];

            // parse additional save folders
            auto js = (*mJson)["additional_save_folders"];
            for (auto it = js.begin(); it != js.end(); ++it) {
                std::vector<std::string> folders = it.value()["folders"];
                std::vector<std::u16string> u16folders;
                for (auto& folder : folders) {
                    u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
                }
                mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
            }

            // parse additional extdata folders
            auto je = (*mJson)["additional_extdata_folders"];
            for (auto it = je.begin(); it != je.end(); ++it) {
                std::vector<std::string> folders = it.value()["folders"];
                std::vector<std::u16string> u16folders;
                for (auto& folder : folders) {
                    u16folders.push_back(StringUtils::UTF8toUTF16(folder.c_str()));
                }
                mAdditionalExtdataFolders.emplace(strtoull(it.key().c_str(), NULL, 16), u16folders);
            }
        }
        else {
            loadFromRomfs();
        }
    }
}

Configuration::~Configuration() {}

void Configuration::loadFromRomfs(void)
{
    FILE* in = fopen("romfs:/config.json", "rt");
    if (in != NULL) {
        mJson = std::make_unique<nlohmann::json>(nlohmann::json::parse(in, nullptr, false));
        fclose(in);
        save();
    }
}

void Configuration::save(void)
{
    std::string writeData = mJson->dump(2);
    writeData.shrink_to_fit();
    size_t size = writeData.size();

    FILE* out = fopen(BASEPATH.c_str(), "wt");
    if (out != NULL) {
        fwrite(writeData.c_str(), 1, size, out);
        fclose(out);
        oldSize = size;
    }
    mDirty = false;
}

void Configuration::commit(void)
{
    if (mDirty) {
        save();
    }
}

bool Configuration::filter(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    return mFilterIds.find(id) != mFilterIds.end();
}

bool Configuration::favorite(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    return mFavoriteIds.find(id) != mFavoriteIds.end();
}

bool Configuration::nandSaves(void)
{
    return mNandSaves;
}

bool Configuration::dsiwareSaves(void)
{
    return mDSiWareSaves;
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

bool Configuration::transferEnabled(void)
{
    return mTransferEnabled;
}

bool Configuration::confirmRestore(void)
{
    return mConfirmRestore;
}

std::string Configuration::theme(void)
{
    return mTheme;
}

void Configuration::setNandSaves(bool v)
{
    mNandSaves             = v;
    (*mJson)["nand_saves"] = v;
    mDirty                 = true;
}

void Configuration::setDSiWareSaves(bool v)
{
    mDSiWareSaves             = v;
    (*mJson)["dsiware_saves"] = v;
    mDirty                    = true;
}

void Configuration::setScanCard(bool v)
{
    mScanCard             = v;
    (*mJson)["scan_cart"] = v;
    mDirty                = true;
}

void Configuration::setTransferEnabled(bool v)
{
    mTransferEnabled             = v;
    (*mJson)["transfer_enabled"] = v;
    mDirty                       = true;
}

void Configuration::setConfirmRestore(bool v)
{
    mConfirmRestore             = v;
    (*mJson)["confirm_restore"] = v;
    mDirty                      = true;
}

void Configuration::setTheme(const std::string& v)
{
    mTheme            = v;
    (*mJson)["theme"] = v;
    mDirty            = true;
}

// Rebuilds a json string-array of hex title ids from the given set.
static nlohmann::json idSetToJson(const std::unordered_set<u64>& ids)
{
    auto arr = nlohmann::json::array();
    for (auto id : ids) {
        arr.push_back(StringUtils::format("%016llX", id));
    }
    return arr;
}

// Rebuilds a json object { "<hexid>": { "folders": [ ... ] } } from the map.
static nlohmann::json folderMapToJson(const std::unordered_map<u64, std::vector<std::u16string>>& map)
{
    auto obj = nlohmann::json::object();
    for (auto& entry : map) {
        auto folders = nlohmann::json::array();
        for (auto& folder : entry.second) {
            folders.push_back(StringUtils::UTF16toUTF8(folder));
        }
        obj[StringUtils::format("%016llX", entry.first)]["folders"] = folders;
    }
    return obj;
}

void Configuration::addFavorite(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    if (!mFavoriteIds.insert(id).second) {
        return; // already a favorite
    }
    (*mJson)["favorites"] = idSetToJson(mFavoriteIds);
    mDirty                = true;
}

void Configuration::addFilter(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    if (!mFilterIds.insert(id).second) {
        return; // already filtered
    }
    (*mJson)["filter"] = idSetToJson(mFilterIds);
    mDirty             = true;
}

void Configuration::addSaveFolder(u64 id, const std::u16string& path)
{
    auto& vec = mAdditionalSaveFolders[id];
    if (std::find(vec.begin(), vec.end(), path) != vec.end()) {
        return; // already present for this title
    }
    vec.push_back(path);
    (*mJson)["additional_save_folders"] = folderMapToJson(mAdditionalSaveFolders);
    mDirty                              = true;
}

void Configuration::addExtdataFolder(u64 id, const std::u16string& path)
{
    auto& vec = mAdditionalExtdataFolders[id];
    if (std::find(vec.begin(), vec.end(), path) != vec.end()) {
        return; // already present for this title
    }
    vec.push_back(path);
    (*mJson)["additional_extdata_folders"] = folderMapToJson(mAdditionalExtdataFolders);
    mDirty                                 = true;
}

void Configuration::removeFavorite(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    if (mFavoriteIds.erase(id) == 0) {
        return;
    }
    (*mJson)["favorites"] = idSetToJson(mFavoriteIds);
    mDirty                = true;
}

void Configuration::removeFilter(u64 id)
{
    std::lock_guard<std::mutex> lock(mIdMutex);
    if (mFilterIds.erase(id) == 0) {
        return;
    }
    (*mJson)["filter"] = idSetToJson(mFilterIds);
    mDirty             = true;
}

void Configuration::removeSaveFolder(u64 id, size_t index)
{
    auto it = mAdditionalSaveFolders.find(id);
    if (it == mAdditionalSaveFolders.end() || index >= it->second.size()) {
        return;
    }
    it->second.erase(it->second.begin() + index);
    if (it->second.empty()) {
        mAdditionalSaveFolders.erase(it);
    }
    (*mJson)["additional_save_folders"] = folderMapToJson(mAdditionalSaveFolders);
    mDirty                              = true;
}

void Configuration::removeExtdataFolder(u64 id, size_t index)
{
    auto it = mAdditionalExtdataFolders.find(id);
    if (it == mAdditionalExtdataFolders.end() || index >= it->second.size()) {
        return;
    }
    it->second.erase(it->second.begin() + index);
    if (it->second.empty()) {
        mAdditionalExtdataFolders.erase(it);
    }
    (*mJson)["additional_extdata_folders"] = folderMapToJson(mAdditionalExtdataFolders);
    mDirty                                 = true;
}