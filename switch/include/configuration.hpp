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

#ifndef CONFIGHANDLER_HPP
#define CONFIGHANDLER_HPP

#include "io.hpp"
#include "json.hpp"
#include "util.hpp"
#include <switch.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward-declared, not #include "title.hpp": title.hpp includes this header,
// so a full include here would leave sort_t undefined by the time this class
// body needs it (whichever header is included first wins the include-guard
// race). sort_t is a named enum with a fixed underlying type specifically so
// this forward declaration is legal — see the comment on its definition.
enum sort_t : u8;

#define CONFIG_VERSION 5

class Configuration {
public:
    static Configuration& getInstance(void)
    {
        static Configuration mConfiguration;
        return mConfiguration;
    }

    bool filter(u64 id);
    bool favorite(u64 id);
    bool isPKSMBridgeEnabled(void);
    bool isFTPEnabled(void);
    // Whether a restore asks for a YesNo confirmation first (parity with the 3DS
    // confirm_restore setting). Default true — a restore overwrites the save.
    bool isConfirmRestoreEnabled(void);
    std::vector<std::string> additionalSaveFolders(u64 id);
    std::vector<std::string> additionalDeviceSaveFolders(u64 id);
    void save(void);
    void load(void);
    void parse(void);

    // Every id currently hidden/favorited (Settings > Library reads these to
    // build its two lists; getCompleteTitleList() supplies the id->name map).
    std::vector<u64> hiddenIds(void);
    std::vector<u64> favoriteIds(void);
    // Every id with at least one additional save folder configured (Settings
    // > Save folders). User (account) saves and device saves keep separate
    // folder lists: an extra folder only feeds the backup list of the save
    // kind it was configured for.
    std::vector<u64> additionalSaveFolderIds(void);
    std::vector<u64> additionalDeviceSaveFolderIds(void);

    // Mutators. Each writes mJson and calls save() immediately — Settings has
    // no separate "save" step; every change writes config.json synchronously.
    void setFilter(u64 id, bool hidden);
    void setFavorite(u64 id, bool favorite);
    void setPKSMBridgeEnabled(bool enabled);
    void setFTPEnabled(bool enabled);
    void setConfirmRestoreEnabled(bool enabled);
    void addAdditionalSaveFolder(u64 id, const std::string& path);
    void removeAdditionalSaveFolder(u64 id, const std::string& path);
    void addAdditionalDeviceSaveFolder(u64 id, const std::string& path);
    void removeAdditionalDeviceSaveFolder(u64 id, const std::string& path);

    // "dark" (default, only theme that renders today) or "light" (persisted,
    // reserved for a future light theme).
    std::string theme(void);
    void setTheme(const std::string& theme);

    // Default/current title-grid sort mode. Persisted so it survives a
    // relaunch; the grid's X-button cycle and the Settings "Default sort"
    // spinner both read/write this same setting through TitleCatalog.
    sort_t sortMode(void);
    void setSortMode(sort_t mode);

    const std::string BASEPATH = "/switch/Checkpoint/config.json";

private:
    Configuration(void);
    ~Configuration(void);

    void store(void);

    // Shared body of the user/device folder mutators: `map` is the parsed
    // cache, `key` the config.json object both stay in sync with.
    void addFolder(std::unordered_map<u64, std::vector<std::string>>& map, const char* key, u64 id, const std::string& path);
    void removeFolder(std::unordered_map<u64, std::vector<std::string>>& map, const char* key, u64 id, const std::string& path);

    Configuration(Configuration const&)  = delete;
    void operator=(Configuration const&) = delete;

    nlohmann::json mJson;
    bool PKSMBridgeEnabled;
    bool FTPEnabled;
    bool mConfirmRestore;
    std::unordered_set<u64> mFilterIds, mFavoriteIds;
    std::unordered_map<u64, std::vector<std::string>> mAdditionalSaveFolders, mAdditionalDeviceSaveFolders;
    std::string mTheme;
    sort_t mSortMode;
};

#endif