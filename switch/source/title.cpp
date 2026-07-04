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

#include "title.hpp"

void Title::init(u8 saveDataType, u64 id, AccountUid userID, u8 spaceId, const std::string& name, const std::string& author,
    const std::string& userName, const std::string& path)
{
    mId              = id;
    mUserId          = userID;
    mSaveDataType    = saveDataType;
    mSaveDataSpaceId = spaceId;
    mUserName        = userName;
    mAuthor          = author;
    mName            = name;
    mPath            = path;
    mDisplayName     = StringUtils::removeAccents(mName);

    refreshDirectories();
}

u8 Title::saveDataType(void)
{
    return mSaveDataType;
}

u8 Title::saveDataSpaceId(void)
{
    return mSaveDataSpaceId;
}

u64 Title::id(void)
{
    return mId;
}

u64 Title::saveId(void)
{
    return mSaveId;
}

void Title::saveId(u64 saveId)
{
    mSaveId = saveId;
}

AccountUid Title::userId(void)
{
    return mUserId;
}

std::string Title::userName(void)
{
    return mUserName;
}

std::string Title::author(void)
{
    return mAuthor;
}

std::string Title::name(void)
{
    return mName;
}

std::string Title::displayName(void)
{
    return mDisplayName;
}

std::string Title::path(void)
{
    return mPath;
}

std::string Title::fullPath(size_t index)
{
    return mFullSavePaths.at(index);
}

const std::vector<std::string>& Title::saves()
{
    return mSaves;
}

u64 Title::playTimeNanoseconds(void)
{
    return mPlayTimeNanoseconds;
}

std::string Title::playTime(void)
{
    const u64 playTimeMinutes = mPlayTimeNanoseconds / 60000000000;
    return StringUtils::format("%d", playTimeMinutes / 60) + ":" + StringUtils::format("%02d", playTimeMinutes % 60) + " hours";
}

void Title::playTimeNanoseconds(u64 playTimeNanoseconds)
{
    mPlayTimeNanoseconds = playTimeNanoseconds;
}

u32 Title::lastPlayedTimestamp(void)
{
    return mLastPlayedTimestamp;
}

void Title::lastPlayedTimestamp(u32 lastPlayedTimestamp)
{
    mLastPlayedTimestamp = lastPlayedTimestamp;
}

void Title::refreshDirectories(void)
{
    mSaves.clear();
    mFullSavePaths.clear();

    Directory savelist(mPath);
    if (savelist.good()) {
        for (size_t i = 0, sz = savelist.size(); i < sz; i++) {
            if (savelist.folder(i)) {
                mSaves.push_back(savelist.entry(i));
                mFullSavePaths.push_back(mPath + "/" + savelist.entry(i));
            }
        }

        std::sort(mSaves.rbegin(), mSaves.rend());
        std::sort(mFullSavePaths.rbegin(), mFullSavePaths.rend());
        mSaves.insert(mSaves.begin(), "New...");
        mFullSavePaths.insert(mFullSavePaths.begin(), "New...");
    }
    else {
        Logging::error("Couldn't retrieve the extdata directory list for the title {}", name());
    }

    // Extra backup folders from configuration. User (account) and device saves
    // have separate lists; other kinds (BCAT/system) take none, so a folder
    // configured for a game's user save never shows up in its other lists.
    std::vector<std::string> additionalFolders;
    if (mSaveDataType == FsSaveDataType_Account) {
        additionalFolders = Configuration::getInstance().additionalSaveFolders(mId);
    }
    else if (mSaveDataType == FsSaveDataType_Device) {
        additionalFolders = Configuration::getInstance().additionalDeviceSaveFolders(mId);
    }
    for (std::vector<std::string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); ++it) {
        // we have other folders to parse
        Directory list(*it);
        if (list.good()) {
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                if (list.folder(i)) {
                    mSaves.push_back(list.entry(i));
                    mFullSavePaths.push_back(*it + "/" + list.entry(i));
                }
            }
        }
    }
}
