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

#ifndef TITLE_HPP
#define TITLE_HPP

#include "SDLHelper.hpp"
#include "account.hpp"
#include "configuration.hpp"
#include "filesystem.hpp"
#include "io.hpp"
#include <algorithm>
#include <stdlib.h>
#include <string>
#include <switch.h>
#include <unordered_map>
#include <utility>
#include <vector>

class Title {
public:
    void init(u8 saveDataType, u64 titleid, AccountUid userID, const std::string& name, const std::string& author);
    ~Title() = default;

    std::string author(void);
    std::pair<std::string, std::string> displayName(void);
    SDL_Texture* icon(void);
    u64 id(void);
    std::string name(void);
    std::string path(void);
    u32 playTimeMinutes(void);
    std::string playTime(void);
    void playTimeMinutes(u32 playTimeMinutes);
    u32 lastPlayedTimestamp(void);
    void lastPlayedTimestamp(u32 lastPlayedTimestamp);
    std::string fullPath(size_t index);
    void refreshDirectories(void);
    u64 saveId();
    void saveId(u64 id);
    std::vector<std::string> saves(void);
    u8 saveDataType(void);
    AccountUid userId(void);
    std::string userName(void);

private:
    u64 mId;
    u64 mSaveId;
    AccountUid mUserId;
    std::string mUserName;
    std::string mName;
    std::string mSafeName;
    std::string mAuthor;
    std::string mPath;
    std::vector<std::string> mSaves;
    std::vector<std::string> mFullSavePaths;
    u8 mSaveDataType;
    std::pair<std::string, std::string> mDisplayName;
    u32 mPlayTimeMinutes;
    u32 mLastPlayedTimestamp;
};

void getTitle(Title& dst, AccountUid uid, size_t i);
size_t getTitleCount(AccountUid uid);
void loadTitles(void);
void sortTitles(void);
void rotateSortMode(void);
void refreshDirectories(u64 id);
bool favorite(AccountUid uid, int i);
void freeIcons(void);
SDL_Texture* smallIcon(AccountUid uid, size_t i);
std::unordered_map<std::string, std::string> getCompleteTitleList(void);

#endif