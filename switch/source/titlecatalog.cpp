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

#include "titlecatalog.hpp"
#include "savekind.hpp"
#include "titleprobe.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

void TitleCatalog::loadTitles(void)
{
    mTitles.clear();

    NsApplicationControlData* nsacd = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (nsacd == NULL) {
        return;
    }
    memset(nsacd, 0, sizeof(NsApplicationControlData));

    FsSaveDataInfoReader reader;
    FsSaveDataInfo info;
    s64 total_entries = 0;

    // BCAT/Device/System singletons are appended to every user's list once the scan finishes.
    std::vector<Title> bcatTitles;
    std::vector<Title> deviceTitles;
    std::vector<Title> systemTitles;

    Result res = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User);
    if (R_SUCCEEDED(res)) {
        while (1) {
            res = fsSaveDataInfoReaderRead(&reader, &info, 1, &total_entries);
            if (R_FAILED(res) || total_entries == 0) {
                break;
            }

            Title title;
            if (!TitleProbe::probe(title, info, mIcons, nsacd)) {
                continue;
            }

            switch (info.save_data_type) {
                case FsSaveDataType_Account: {
                    auto it = mTitles.find(info.uid);
                    if (it != mTitles.end()) {
                        it->second.push_back(title);
                    }
                    else {
                        mTitles.emplace(info.uid, std::vector<Title>{title});
                    }
                    break;
                }
                case FsSaveDataType_Bcat:
                    bcatTitles.push_back(title);
                    break;
                case FsSaveDataType_Device:
                    deviceTitles.push_back(title);
                    break;
                default:
                    break;
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }

    // enumerate system saves from the System space
    res = fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_System);
    if (R_SUCCEEDED(res)) {
        while (1) {
            res = fsSaveDataInfoReaderRead(&reader, &info, 1, &total_entries);
            if (R_FAILED(res) || total_entries == 0) {
                break;
            }
            if (info.save_data_type != FsSaveDataType_System) {
                continue;
            }

            Title title;
            if (TitleProbe::probe(title, info, mIcons, nsacd)) {
                systemTitles.push_back(title);
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }

    free(nsacd);

    // append the shared BCAT / device / system saves to every user's list
    for (auto& pair : mTitles) {
        for (const auto& bcatTitle : bcatTitles) {
            pair.second.push_back(bcatTitle);
        }
        for (const auto& deviceTitle : deviceTitles) {
            pair.second.push_back(deviceTitle);
        }
        for (const auto& systemTitle : systemTitles) {
            pair.second.push_back(systemTitle);
        }
    }

    sortTitles();
}

void TitleCatalog::sortTitles(void)
{
    for (auto& vect : mTitles) {
        std::sort(vect.second.begin(), vect.second.end(), [this](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            switch (mSortMode) {
                case SORT_LAST_PLAYED:
                    return l.lastPlayedTimestamp() > r.lastPlayedTimestamp();
                case SORT_PLAY_TIME:
                    return l.playTimeNanoseconds() > r.playTimeNanoseconds();
                case SORT_ALPHA:
                default:
                    return l.name() < r.name();
            }
        });
    }
    mGeneration++;
}

void TitleCatalog::rotateSortMode(void)
{
    mSortMode = static_cast<sort_t>((mSortMode + 1) % SORT_MODES_COUNT);
    sortTitles();
}

void TitleCatalog::getTitle(Title& dst, AccountUid uid, size_t i)
{
    auto it = mTitles.find(uid);
    if (it != mTitles.end() && i < getTitleCount(uid)) {
        dst = it->second.at(i);
    }
}

size_t TitleCatalog::getTitleCount(AccountUid uid)
{
    auto it = mTitles.find(uid);
    return it != mTitles.end() ? it->second.size() : 0;
}

// Raw-index favorite lookup; reached only through filteredFavorite, which maps the
// filtered cell index back to the raw title index first.
bool TitleCatalog::favorite(AccountUid uid, int i)
{
    auto it = mTitles.find(uid);
    return it != mTitles.end() ? Configuration::getInstance().favorite(it->second.at(i).id()) : false;
}

void TitleCatalog::refreshDirectories(u64 id)
{
    for (auto& pair : mTitles) {
        for (size_t i = 0; i < pair.second.size(); i++) {
            if (pair.second.at(i).id() == id) {
                pair.second.at(i).refreshDirectories();
            }
        }
    }
    mGeneration++;
}

SDL_Texture* TitleCatalog::iconFor(u64 id)
{
    return mIcons.get(id);
}

size_t TitleCatalog::getFilteredTitleCount(AccountUid uid, saveTypeFilter_t filter)
{
    auto it = mTitles.find(uid);
    if (it == mTitles.end())
        return 0;
    u8 type      = SaveKind::of(filter).saveDataType;
    size_t count = 0;
    for (auto& t : it->second) {
        if (t.saveDataType() == type)
            count++;
    }
    return count;
}

void TitleCatalog::getFilteredTitle(Title& dst, AccountUid uid, saveTypeFilter_t filter, size_t i)
{
    auto it = mTitles.find(uid);
    if (it == mTitles.end())
        return;
    u8 type      = SaveKind::of(filter).saveDataType;
    size_t count = 0;
    for (auto& t : it->second) {
        if (t.saveDataType() == type) {
            if (count == i) {
                dst = t;
                return;
            }
            count++;
        }
    }
}

size_t TitleCatalog::filteredToRawIndex(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx)
{
    auto it = mTitles.find(uid);
    if (it == mTitles.end())
        return 0;
    u8 type      = SaveKind::of(filter).saveDataType;
    size_t count = 0;
    for (size_t j = 0; j < it->second.size(); j++) {
        if (it->second[j].saveDataType() == type) {
            if (count == filteredIdx)
                return j;
            count++;
        }
    }
    return 0;
}

bool TitleCatalog::filteredFavorite(AccountUid uid, saveTypeFilter_t filter, int i)
{
    size_t raw = filteredToRawIndex(uid, filter, (size_t)i);
    return favorite(uid, (int)raw);
}

SDL_Texture* TitleCatalog::filteredSmallIcon(AccountUid uid, saveTypeFilter_t filter, size_t i)
{
    auto it = mTitles.find(uid);
    if (it == mTitles.end())
        return NULL;
    size_t raw = filteredToRawIndex(uid, filter, i);
    return mIcons.get(it->second.at(raw).id());
}

void TitleCatalog::freeIcons(void)
{
    mIcons.clear();
}

std::unordered_map<std::string, std::string> TitleCatalog::getCompleteTitleList(void)
{
    std::unordered_map<std::string, std::string> map;
    for (const auto& pair : mTitles) {
        for (auto value : pair.second) {
            map.insert({StringUtils::format("0x%016llX", value.id()), value.name()});
        }
    }
    return map;
}
