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
#include "sortmode.hpp"
#include "titlecatalog.hpp"
#include <algorithm>

namespace {
    // Every id in config.json is stored as this same hex string, matching
    // TitleCatalog::getCompleteTitleList()'s key format so Settings' Library/
    // Save-folders tabs can look a stored id straight up in that map.
    std::string hexId(u64 id)
    {
        return StringUtils::format("0x%016llX", id);
    }
}

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
        if (!(mJson.contains("confirm-restore") && mJson["confirm-restore"].is_boolean())) {
            mJson["confirm-restore"] = true; // default: confirm, matching prior always-confirm behavior
            updateJson               = true;
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
        if (!(mJson.contains("additional_device_save_folders") && mJson["additional_device_save_folders"].is_object())) {
            mJson["additional_device_save_folders"] = nlohmann::json::object();
            updateJson                              = true;
        }
        if (!(mJson.contains("theme") && mJson["theme"].is_string())) {
            mJson["theme"] = "dark";
            updateJson     = true;
        }
        if (!(mJson.contains("sort-mode") && mJson["sort-mode"].is_string())) {
            mJson["sort-mode"] = SortMode::of(SORT_ALPHA).configKey;
            updateJson         = true;
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
        for (auto& obj : mJson["additional_device_save_folders"]) {
            if (!obj.is_object()) {
                mJson["additional_device_save_folders"] = nlohmann::json::object();
                updateJson                              = true;
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

std::vector<std::string> Configuration::additionalDeviceSaveFolders(u64 id)
{
    std::vector<std::string> emptyvec;
    auto folders = mAdditionalDeviceSaveFolders.find(id);
    return folders == mAdditionalDeviceSaveFolders.end() ? emptyvec : folders->second;
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
    mAdditionalDeviceSaveFolders.clear();

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

    // parse additional save folders (user + device kept as separate lists)
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::string> sfolders;
        for (auto& folder : folders) {
            sfolders.push_back(folder);
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), sfolders);
    }
    auto jsDevice = mJson["additional_device_save_folders"];
    for (auto it = jsDevice.begin(); it != jsDevice.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::string> sfolders;
        for (auto& folder : folders) {
            sfolders.push_back(folder);
        }
        mAdditionalDeviceSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), sfolders);
    }

    // parse PKSM Bridge flag
    PKSMBridgeEnabled = mJson["pksm-bridge"];
    // parse FTP flag
    FTPEnabled = mJson["ftp-enabled"];
    // parse confirm-restore flag
    mConfirmRestore = mJson.value("confirm-restore", true);

    mTheme    = mJson.value("theme", "dark");
    mSortMode = SortMode::fromConfigKey(mJson.value("sort-mode", std::string(SortMode::of(SORT_ALPHA).configKey)));
}

nlohmann::json Configuration::getJson(void)
{
    return mJson;
}

bool Configuration::isFTPEnabled(void)
{
    return FTPEnabled;
}

std::vector<u64> Configuration::hiddenIds(void)
{
    return std::vector<u64>(mFilterIds.begin(), mFilterIds.end());
}

std::vector<u64> Configuration::favoriteIds(void)
{
    return std::vector<u64>(mFavoriteIds.begin(), mFavoriteIds.end());
}

std::vector<u64> Configuration::additionalSaveFolderIds(void)
{
    std::vector<u64> ids;
    ids.reserve(mAdditionalSaveFolders.size());
    for (const auto& pair : mAdditionalSaveFolders) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::vector<u64> Configuration::additionalDeviceSaveFolderIds(void)
{
    std::vector<u64> ids;
    ids.reserve(mAdditionalDeviceSaveFolders.size());
    for (const auto& pair : mAdditionalDeviceSaveFolders) {
        ids.push_back(pair.first);
    }
    return ids;
}

void Configuration::setFilter(u64 id, bool hidden)
{
    if (hidden) {
        mFilterIds.emplace(id);
    }
    else {
        mFilterIds.erase(id);
    }
    mJson["filter"] = std::vector<std::string>(); // rebuild rather than track array-vs-set indices
    for (u64 filtered : mFilterIds) {
        mJson["filter"].push_back(hexId(filtered));
    }
    save();
}

void Configuration::setFavorite(u64 id, bool favorite)
{
    if (favorite) {
        mFavoriteIds.emplace(id);
    }
    else {
        mFavoriteIds.erase(id);
    }
    mJson["favorites"] = std::vector<std::string>();
    for (u64 fav : mFavoriteIds) {
        mJson["favorites"].push_back(hexId(fav));
    }
    save();
}

void Configuration::setPKSMBridgeEnabled(bool enabled)
{
    PKSMBridgeEnabled    = enabled;
    mJson["pksm-bridge"] = enabled;
    save();
}

void Configuration::setFTPEnabled(bool enabled)
{
    FTPEnabled           = enabled;
    mJson["ftp-enabled"] = enabled;
    save();
}

bool Configuration::isConfirmRestoreEnabled(void)
{
    return mConfirmRestore;
}

void Configuration::setConfirmRestoreEnabled(bool enabled)
{
    mConfirmRestore          = enabled;
    mJson["confirm-restore"] = enabled;
    save();
}

void Configuration::addFolder(std::unordered_map<u64, std::vector<std::string>>& map, const char* key, u64 id, const std::string& path)
{
    std::vector<std::string>& folders = map[id];
    if (std::find(folders.begin(), folders.end(), path) != folders.end()) {
        return;
    }
    folders.push_back(path);
    mJson[key][hexId(id)]["folders"] = folders;
    save();
}

void Configuration::removeFolder(std::unordered_map<u64, std::vector<std::string>>& map, const char* key, u64 id, const std::string& path)
{
    auto it = map.find(id);
    if (it == map.end()) {
        return;
    }
    auto& folders = it->second;
    folders.erase(std::remove(folders.begin(), folders.end(), path), folders.end());
    if (folders.empty()) {
        map.erase(it);
        mJson[key].erase(hexId(id));
    }
    else {
        mJson[key][hexId(id)]["folders"] = folders;
    }
    save();
}

void Configuration::addAdditionalSaveFolder(u64 id, const std::string& path)
{
    addFolder(mAdditionalSaveFolders, "additional_save_folders", id, path);
}

void Configuration::removeAdditionalSaveFolder(u64 id, const std::string& path)
{
    removeFolder(mAdditionalSaveFolders, "additional_save_folders", id, path);
}

void Configuration::addAdditionalDeviceSaveFolder(u64 id, const std::string& path)
{
    addFolder(mAdditionalDeviceSaveFolders, "additional_device_save_folders", id, path);
}

void Configuration::removeAdditionalDeviceSaveFolder(u64 id, const std::string& path)
{
    removeFolder(mAdditionalDeviceSaveFolders, "additional_device_save_folders", id, path);
}

std::string Configuration::theme(void)
{
    return mTheme;
}

void Configuration::setTheme(const std::string& theme)
{
    mTheme         = theme;
    mJson["theme"] = theme;
    save();
}

sort_t Configuration::sortMode(void)
{
    return mSortMode;
}

void Configuration::setSortMode(sort_t mode)
{
    mSortMode          = mode;
    mJson["sort-mode"] = SortMode::of(mode).configKey;
    save();
}
