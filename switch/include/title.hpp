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

#ifndef TITLE_HPP
#define TITLE_HPP

#include "DekoHelper.hpp"
#include "account.hpp"
#include "configuration.hpp"
#include "filesystem.hpp"
#include "io.hpp"
#include <algorithm>
#include <stdlib.h>
#include <string>
#include <switch.h>
#include <unordered_map>
#include <vector>

// Sort order applied to a user's title list. Owned by TitleCatalog (mSortMode),
// persisted through Configuration so it survives a relaunch; the UI reads it
// back through SortMode::label() for both the grid's X-button cycle and the
// Settings "Default sort" spinner — the same setting, not two.
//
// A named enum with a fixed underlying type so configuration.hpp can
// forward-declare it without a full #include: title.hpp already includes
// configuration.hpp, and configuration.hpp needs sort_t in Configuration's own
// signature, so an #include cycle would otherwise leave sort_t undefined at the
// point configuration.hpp needs it.
enum sort_t : u8 { SORT_ALPHA, SORT_LAST_PLAYED, SORT_MOST_BACKUPS, SORT_FAVORITES_FIRST, SORT_MODES_COUNT };

class Title {
public:
    // No-IO populate from values TitleProbe has already resolved (name/author from
    // the control data, userName + on-SD path from the SaveDataSource). Computes
    // the display name and scans the backup folders, but creates nothing.
    void init(u8 saveDataType, u64 titleid, AccountUid userID, u8 spaceId, const std::string& name, const std::string& author,
        const std::string& userName, const std::string& path);
    ~Title() = default;

    std::string author(void);
    std::string displayName(void);
    u64 id(void);
    std::string name(void);
    std::string path(void);
    u64 playTimeNanoseconds(void);
    std::string playTime(void);
    void playTimeNanoseconds(u64 playTimeNanoseconds);
    u32 lastPlayedTimestamp(void);
    void lastPlayedTimestamp(u32 lastPlayedTimestamp);
    std::string fullPath(size_t index);
    void refreshDirectories(void);
    u64 saveId();
    void saveId(u64 id);
    const std::vector<std::string>& saves(void);
    u8 saveDataType(void);
    u8 saveDataSpaceId(void);
    AccountUid userId(void);
    std::string userName(void);

private:
    u64 mId;
    u64 mSaveId;
    AccountUid mUserId;
    std::string mUserName;
    std::string mName;
    std::string mAuthor;
    std::string mPath;
    std::vector<std::string> mSaves;
    std::vector<std::string> mFullSavePaths;
    u8 mSaveDataType;
    u8 mSaveDataSpaceId;
    std::string mDisplayName;
    u64 mPlayTimeNanoseconds;
    u32 mLastPlayedTimestamp;
};

typedef enum { FILTER_SAVES, FILTER_BCAT, FILTER_DEVICE, FILTER_SYSTEM } saveTypeFilter_t;

#endif