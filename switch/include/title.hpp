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
    void init(u8 saveDataType, u64 titleid, u128 userID, const std::string& name, const std::string& author);
    ~Title(void){};

    std::string author(void);
    std::pair<std::string, std::string> displayName(void);
    SDL_Texture* icon(void);
    u64 id(void);
    std::string name(void);
    std::string path(void);
    std::string fullPath(size_t index);
    void refreshDirectories(void);
    u64 saveId();
    void saveId(u64 id);
    std::vector<std::string> saves(void);
    u8 saveDataType(void);
    bool systemSave(void);
    u128 userId(void);
    std::string userName(void);

private:
    u64 mId;
    u64 mSaveId;
    u128 mUserId;
    std::string mUserName;
    std::string mName;
    std::string mSafeName;
    std::string mAuthor;
    std::string mPath;
    std::vector<std::string> mSaves;
    std::vector<std::string> mFullSavePaths;
    u8 mSaveDataType;
};

void getTitle(Title& dst, u128 uid, size_t i);
size_t getTitleCount(u128 uid);
void loadTitles(void);
void refreshDirectories(u64 id);
bool favorite(u128 uid, int i);
void freeIcons(void);
SDL_Texture* smallIcon(u128 uid, size_t i);
std::unordered_map<std::string, std::string> getCompleteTitleList(void);

#endif