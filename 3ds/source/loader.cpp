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

#include "loader.hpp"
#include "title.hpp"
#include "titlecache.hpp"
#include "titlequirks.hpp"
#include <cctype>
#include <chrono>
#include <mutex>

namespace {
    std::string toLowerAscii(const std::string& s)
    {
        std::string out = s;
        for (size_t i = 0; i < out.size(); i++) {
            out[i] = (char)std::tolower((unsigned char)out[i]);
        }
        return out;
    }
}

bool TitleCatalog::validId(u64 id)
{
    if (TitleQuirks::isSystemExcluded(id)) {
        return false;
    }

    return !Configuration::getInstance().filter(id);
}

void TitleCatalog::getTitle(Title& dst, int i, BackupKind kind)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const auto& vec = kind == BackupKind::Save ? mSaves : mExtdatas;
    if (i >= 0 && i < (int)vec.size()) {
        dst = vec.at(i);
    }
    else {
        dst.load();
        dst.setIcon(Gui::noIcon());
    }
}

bool TitleCatalog::getTitleById(Title& dst, u64 id)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& title : mSaves) {
        if (title.id() == id) {
            dst = title;
            return true;
        }
    }
    for (const auto& title : mExtdatas) {
        if (title.id() == id) {
            dst = title;
            return true;
        }
    }
    return false;
}

bool TitleCatalog::getTitleByName(Title& dst, const std::string& name)
{
    if (name.empty()) {
        return false;
    }

    std::string needle = toLowerAscii(name);
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& title : mSaves) {
        if (toLowerAscii(title.shortDescription()) == needle) {
            dst = title;
            return true;
        }
    }
    for (const auto& title : mExtdatas) {
        if (toLowerAscii(title.shortDescription()) == needle) {
            dst = title;
            return true;
        }
    }
    return false;
}

int TitleCatalog::getTitleCount(BackupKind kind)
{
    std::lock_guard<std::mutex> lock(mMutex);
    return kind == BackupKind::Save ? mSaves.size() : mExtdatas.size();
}

C2D_Image TitleCatalog::icon(int i, BackupKind kind)
{
    std::lock_guard<std::mutex> lock(mMutex);
    auto& vec = kind == BackupKind::Save ? mSaves : mExtdatas;
    if (i >= 0 && i < (int)vec.size()) {
        return vec.at(i).icon();
    }
    return Gui::noIcon();
}

bool TitleCatalog::favorite(int i, BackupKind kind)
{
    u64 id;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto& vec = kind == BackupKind::Save ? mSaves : mExtdatas;
        if (i < 0 || i >= (int)vec.size()) {
            return false;
        }
        id = vec.at(i).id();
    }
    return Configuration::getInstance().favorite(id);
}

void TitleCatalog::refreshDirectories(u64 id)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (size_t i = 0; i < mSaves.size(); i++) {
        if (mSaves.at(i).id() == id) {
            mSaves.at(i).refreshDirectories();
        }
    }
    for (size_t i = 0; i < mExtdatas.size(); i++) {
        if (mExtdatas.at(i).id() == id) {
            mExtdatas.at(i).refreshDirectories();
        }
    }
}

void TitleCatalog::refreshAllDirectories(void)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto& title : mSaves) {
        title.refreshDirectories();
    }
    for (auto& title : mExtdatas) {
        title.refreshDirectories();
    }
}

LoadProgress TitleCatalog::progress(void)
{
    return LoadProgress{mLoading.load(), mCounter.load(), mLimit.load()};
}

void TitleCatalog::exportTitleListCache(std::vector<Title>& list, const std::u16string& path)
{
    const size_t bytes          = list.size() * TitleCache::ENTRY_SIZE;
    std::unique_ptr<u8[]> cache = std::unique_ptr<u8[]>(new u8[bytes]());
    for (size_t i = 0; i < list.size(); i++) {
        TitleCache::encode(cache.get() + i * TitleCache::ENTRY_SIZE, list.at(i));
    }

    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
    FSStream output(Archive::sdmc(), path, FS_OPEN_WRITE, bytes);
    output.write(cache.get(), bytes);
    output.close();
}

void TitleCatalog::importTitleListCache(void)
{
    static const std::u16string saveCachePath    = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache");
    static const std::u16string extdataCachePath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache");

    FSStream inputsaves(Archive::sdmc(), saveCachePath, FS_OPEN_READ);
    u32 sizesaves = inputsaves.size() / TitleCache::ENTRY_SIZE;
    std::unique_ptr<u8[]> cachesaves(new u8[inputsaves.size()]);
    inputsaves.read(cachesaves.get(), inputsaves.size());
    inputsaves.close();

    FSStream inputextdatas(Archive::sdmc(), extdataCachePath, FS_OPEN_READ);
    u32 sizeextdatas = inputextdatas.size() / TitleCache::ENTRY_SIZE;
    std::unique_ptr<u8[]> cacheextdatas(new u8[inputextdatas.size()]);
    inputextdatas.read(cacheextdatas.get(), inputextdatas.size());
    inputextdatas.close();

    mLimit = sizesaves + sizeextdatas;

    mSaves.reserve(sizesaves);
    mExtdatas.reserve(sizeextdatas);

    // fill the lists with blank titles first
    for (size_t i = 0, sz = std::max(sizesaves, sizeextdatas); i < sz; i++) {
        Title title;
        title.load();
        if (i < sizesaves) {
            mSaves.push_back(title);
        }
        if (i < sizeextdatas) {
            mExtdatas.push_back(title);
        }
    }

    // store already loaded ids
    std::vector<u64> alreadystored;

    for (size_t i = 0; i < sizesaves; i++) {
        const u8* titleData = cachesaves.get() + i * TitleCache::ENTRY_SIZE;
        mSaves.at(i)        = TitleCache::decode(titleData);
        alreadystored.push_back(TitleCache::readId(titleData));

        mCounter++;
    }

    for (size_t i = 0; i < sizeextdatas; i++) {
        const u8* titleData = cacheextdatas.get() + i * TitleCache::ENTRY_SIZE;

        u64 id                        = TitleCache::readId(titleData);
        std::vector<u64>::iterator it = find(alreadystored.begin(), alreadystored.end(), id);
        if (it == alreadystored.end()) {
            mExtdatas.at(i) = TitleCache::decode(titleData);

            mCounter++;
        }
        else {
            auto pos = it - alreadystored.begin();

            // avoid to copy a cartridge title into the extdata list twice
            if (i != 0 && pos == 0) {
                auto newpos     = find(alreadystored.rbegin(), alreadystored.rend(), id);
                mExtdatas.at(i) = mSaves.at(alreadystored.rend() - newpos - 1);
            }
            else {
                mExtdatas.at(i) = mSaves.at(pos);
            }
        }
    }
}

bool TitleCatalog::scanCard(void)
{
    if (mLoading) {
        return false;
    }

    static bool isScanning = false;
    if (isScanning) {
        return false;
    }
    else {
        isScanning = true;
    }

    bool ret   = false;
    Result res = 0;
    u32 count  = 0;
    FS_CardType cardType;
    res = FSUSER_GetCardType(&cardType);
    if (R_SUCCEEDED(res)) {
        if (cardType == CARD_CTR) {
            res = AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
            if (R_SUCCEEDED(res) && count > 0) {
                u64 id;
                res = AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, &id);
                if (validId(id)) {
                    Title title;
                    if (title.load(id, MEDIATYPE_GAME_CARD, cardType)) {
                        ret = true;
                        if (title.accessibleSave()) {
                            std::lock_guard<std::mutex> lock(mMutex);
                            if (mSaves.empty() || mSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                mSaves.insert(mSaves.begin(), title);
                            }
                        }
                        if (title.accessibleExtdata()) {
                            std::lock_guard<std::mutex> lock(mMutex);
                            if (mExtdatas.empty() || mExtdatas.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                mExtdatas.insert(mExtdatas.begin(), title);
                            }
                        }
                    }
                }
            }
        }
        else {
            Title title;
            if (title.load(0, MEDIATYPE_GAME_CARD, cardType)) {
                ret = true;
                std::lock_guard<std::mutex> lock(mMutex);
                if (mSaves.empty() || mSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                    mSaves.insert(mSaves.begin(), title);
                }
            }
        }
    }
    isScanning = false;
    return ret;
}

void TitleCatalog::loadTitles(bool forceRefreshParam)
{
    auto totalStart   = std::chrono::high_resolution_clock::now();
    auto sectionStart = totalStart;
    try {
        static const std::u16string savecachePath    = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache");
        static const std::u16string extdatacachePath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache");

        mSaves.clear();
        mExtdatas.clear();
        mSaves.reserve(128);
        mExtdatas.reserve(128);

        bool optimizedLoad = false;

        u8 hash[SHA256_BLOCK_SIZE];
        calculateTitleDBHash(hash);

        std::u16string titlesHashPath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/titles.sha");
        if (!io::fileExists(Archive::sdmc(), titlesHashPath) || !io::fileExists(Archive::sdmc(), savecachePath) ||
            !io::fileExists(Archive::sdmc(), extdatacachePath)) {
            // create title list sha256 hash file if it doesn't exist in the working directory
            FSStream output(Archive::sdmc(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
            output.write(hash, SHA256_BLOCK_SIZE);
            output.close();
        }
        else {
            // compare current hash with the previous hash
            FSStream input(Archive::sdmc(), titlesHashPath, FS_OPEN_READ);
            if (input.good() && input.size() == SHA256_BLOCK_SIZE) {
                u8* buf = new u8[input.size()];
                input.read(buf, input.size());
                input.close();

                if (memcmp(hash, buf, SHA256_BLOCK_SIZE) == 0) {
                    // hash matches
                    optimizedLoad = true;
                }
                else {
                    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, titlesHashPath.data()));
                    FSStream output(Archive::sdmc(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
                    output.write(hash, SHA256_BLOCK_SIZE);
                    output.close();
                }

                delete[] buf;
            }
        }

        if (optimizedLoad && !forceRefreshParam) {
            mCounter = 0;

            sectionStart = std::chrono::high_resolution_clock::now();
            // deserialize data
            importTitleListCache();

            auto importEnd      = std::chrono::high_resolution_clock::now();
            auto importDuration = std::chrono::duration_cast<std::chrono::milliseconds>(importEnd - sectionStart);
            Logging::debug("Title cache import completed in {} ms", importDuration.count());

            mCounter = mSaves.size() + mExtdatas.size();

            sectionStart = std::chrono::high_resolution_clock::now();

            for (auto& title : mSaves) {
                title.refreshDirectories();
            }
            for (auto& title : mExtdatas) {
                title.refreshDirectories();
            }

            auto refreshEnd      = std::chrono::high_resolution_clock::now();
            auto refreshDuration = std::chrono::duration_cast<std::chrono::milliseconds>(refreshEnd - sectionStart);
            Logging::debug("Directory refresh completed in {} ms", refreshDuration.count());
        }
        else {
            u32 count     = 0;
            u32 nandCount = 0;
            u32 sdCount   = 0;
            u32 cartCount = 0;

            if (Configuration::getInstance().nandSaves()) {
                AM_GetTitleCount(MEDIATYPE_NAND, &nandCount);
            }

            AM_GetTitleCount(MEDIATYPE_SD, &sdCount);
            AM_GetTitleCount(MEDIATYPE_GAME_CARD, &cartCount);

            mLimit   = nandCount + sdCount + cartCount;
            mCounter = 0;

            if (Configuration::getInstance().nandSaves()) {
                AM_GetTitleCount(MEDIATYPE_NAND, &count);
                std::unique_ptr<u64[]> ids_nand = std::unique_ptr<u64[]>(new u64[count]);
                AM_GetTitleList(NULL, MEDIATYPE_NAND, count, ids_nand.get());

                for (u32 i = 0; i < count; i++) {
                    if (validId(ids_nand[i])) {
                        Title title;
                        if (title.load(ids_nand[i], MEDIATYPE_NAND, CARD_CTR)) {
                            if (title.accessibleSave()) {
                                mSaves.push_back(title);
                            }

                            if (title.accessibleExtdata()) {
                                mExtdatas.push_back(title);
                            }
                        }
                    }

                    mCounter++;
                }
            }

            count = 0;
            AM_GetTitleCount(MEDIATYPE_SD, &count);
            std::unique_ptr<u64[]> ids = std::unique_ptr<u64[]>(new u64[count]);
            AM_GetTitleList(NULL, MEDIATYPE_SD, count, ids.get());

            for (u32 i = 0; i < count; i++) {
                if (validId(ids[i])) {
                    Title title;
                    if (title.load(ids[i], MEDIATYPE_SD, CARD_CTR)) {
                        if (title.accessibleSave() || title.isGBAVC()) {
                            mSaves.push_back(title);
                        }

                        if (title.accessibleExtdata()) {
                            mExtdatas.push_back(title);
                        }
                    }
                }

                mCounter++;
            }

            // always check for PKSM's extdata archive
            bool isPKSMIdAlreadyHere = false;
            for (u32 i = 0; i < count; i++) {
                if (ids[i] == TID_PKSM) {
                    isPKSMIdAlreadyHere = true;
                    break;
                }
            }
            if (!isPKSMIdAlreadyHere) {
                Title title;
                if (title.load(TID_PKSM, MEDIATYPE_SD, CARD_CTR)) {
                    if (title.accessibleExtdata()) {
                        mExtdatas.push_back(title);
                    }
                }

                mCounter++;
            }
        }

        std::sort(mSaves.begin(), mSaves.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        std::sort(mExtdatas.begin(), mExtdatas.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        if (!optimizedLoad || forceRefreshParam) {
            auto exportStart = std::chrono::high_resolution_clock::now();
            Logging::debug("Starting title cache export");
            exportTitleListCache(mSaves, savecachePath);
            exportTitleListCache(mExtdatas, extdatacachePath);
            auto exportEnd      = std::chrono::high_resolution_clock::now();
            auto exportDuration = std::chrono::duration_cast<std::chrono::milliseconds>(exportEnd - exportStart);
            Logging::debug("Title cache export completed in {} ms", exportDuration.count());
        }

        FS_CardType cardType;
        Result res = FSUSER_GetCardType(&cardType);
        if (R_SUCCEEDED(res)) {
            if (cardType == CARD_CTR) {
                u32 count = 0;
                AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
                if (count > 0) {
                    std::unique_ptr<u64[]> ids = std::unique_ptr<u64[]>(new u64[count]);
                    AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, ids.get());
                    if (validId(ids[0])) {
                        Title title;
                        if (title.load(ids[0], MEDIATYPE_GAME_CARD, cardType)) {
                            if (title.accessibleSave()) {
                                mSaves.insert(mSaves.begin(), title);
                            }

                            if (title.accessibleExtdata()) {
                                mExtdatas.insert(mExtdatas.begin(), title);
                            }
                        }
                    }
                    mCounter++;
                }
            }
            else {
                Title title;
                if (title.load(0, MEDIATYPE_GAME_CARD, cardType)) {
                    mSaves.insert(mSaves.begin(), title);
                }
                mCounter++;
            }
        }
    }
    catch (const std::exception& e) {
        Logging::error("Exception in loadTitles: {}", e.what());
    }
    catch (...) {
        Logging::error("Unknown exception in loadTitles");
    }

    auto totalEnd      = std::chrono::high_resolution_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart);
    Logging::debug("Title list loaded in {} ms (total)", totalDuration.count());
}

void TitleCatalog::loadTitlesThread(void)
{
    TitleCatalog& self = get();

    // don't load titles while they're loading
    if (self.mLoading) {
        return;
    }

    self.mLoading = true;
    self.mCounter = 0;
    self.mLimit   = 0;

    self.loadTitles(self.mForceRefresh);
    self.mForceRefresh = true;
    self.mLoading      = false;
}

void TitleCatalog::cartScan(void)
{
    TitleCatalog& self = get();

    bool oldCardIn;
    FSUSER_CardSlotIsInserted(&oldCardIn);

    while (self.mDoCartScan.test_and_set()) {
        bool cardIn = false;

        FSUSER_CardSlotIsInserted(&cardIn);
        if (cardIn != oldCardIn) {
            bool power;
            FSUSER_CardSlotGetCardIFPowerStatus(&power);
            if (cardIn) {
                if (!power) {
                    FSUSER_CardSlotPowerOn(&power);
                }
                while (!power && self.mDoCartScan.test_and_set()) {
                    FSUSER_CardSlotGetCardIFPowerStatus(&power);
                }
                svcSleepThread(500'000'000);
                for (size_t i = 0; i < 10; i++) {
                    if ((oldCardIn = self.scanCard())) {
                        break;
                    }
                }
            }
            else {
                FSUSER_CardSlotPowerOff(&power);
                if (!self.mLoading) {
                    std::lock_guard<std::mutex> lock(self.mMutex);
                    if (!self.mSaves.empty() && self.mSaves.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                        self.mSaves.erase(self.mSaves.begin());
                    }
                    if (!self.mExtdatas.empty() && self.mExtdatas.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                        self.mExtdatas.erase(self.mExtdatas.begin());
                    }
                }
                oldCardIn = false;
            }
        }
    }
}

void TitleCatalog::cartScanFlagTestAndSet(void)
{
    get().mDoCartScan.test_and_set();
}

void TitleCatalog::clearCartScanFlag(void)
{
    get().mDoCartScan.clear();
}
