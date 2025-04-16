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

#include "loader.hpp"
#include "main.hpp"
#include "thread.hpp"
#include "title.hpp"
#include <chrono>

namespace {
    std::vector<Title> titleSaves;
    std::vector<Title> titleExtdatas;

    bool forceRefresh           = false;
    std::atomic_flag doCartScan = ATOMIC_FLAG_INIT;
    const size_t ENTRYSIZE      = 5341;

    struct ExportTitleListCacheParams {
        std::vector<Title> titleSaves;
        std::vector<Title> titleExtdatas;
        std::u16string savecachePath;
        std::u16string extdatacachePath;
    };

    static void exportTitleListCacheThreaded(void* arg)
    {
        ExportTitleListCacheParams* params = static_cast<ExportTitleListCacheParams*>(arg);
        TitleLoader::exportTitleListCache(params->titleSaves, params->savecachePath);
        TitleLoader::exportTitleListCache(params->titleExtdatas, params->extdatacachePath);
        delete params;
    }
}

bool TitleLoader::validId(u64 id)
{
    // check for invalid titles
    switch ((u32)id) {
        // Instruction Manual
        case 0x00008602:
        case 0x00009202:
        case 0x00009B02:
        case 0x0000A402:
        case 0x0000AC02:
        case 0x0000B402:
        // Internet Browser
        case 0x00008802:
        case 0x00009402:
        case 0x00009D02:
        case 0x0000A602:
        case 0x0000AE02:
        case 0x0000B602:
        case 0x20008802:
        case 0x20009402:
        case 0x20009D02:
        case 0x2000AE02:
        // Garbage
        case 0x00021A00:
            return false;
    }

    // check for updates
    u32 high = id >> 32;
    if (high == 0x0004000E) {
        return false;
    }

    return !Configuration::getInstance().filter(id);
}

void TitleLoader::getTitle(Title& dst, int i)
{
    const Mode_t mode = Archive::mode();
    if (i < getTitleCount()) {
        dst = mode == MODE_SAVE ? titleSaves.at(i) : titleExtdatas.at(i);
    }
}

int TitleLoader::getTitleCount(void)
{
    const Mode_t mode = Archive::mode();
    return mode == MODE_SAVE ? titleSaves.size() : titleExtdatas.size();
}

C2D_Image TitleLoader::icon(int i)
{
    const Mode_t mode = Archive::mode();
    return mode == MODE_SAVE ? titleSaves.at(i).icon() : titleExtdatas.at(i).icon();
}

bool TitleLoader::favorite(int i)
{
    const Mode_t mode = Archive::mode();
    u64 id            = mode == MODE_SAVE ? titleSaves.at(i).id() : titleExtdatas.at(i).id();
    return Configuration::getInstance().favorite(id);
}

void TitleLoader::refreshDirectories(u64 id)
{
    const Mode_t mode = Archive::mode();
    if (mode == MODE_SAVE) {
        for (size_t i = 0; i < titleSaves.size(); i++) {
            if (titleSaves.at(i).id() == id) {
                titleSaves.at(i).refreshDirectories();
            }
        }
    }
    else {
        for (size_t i = 0; i < titleExtdatas.size(); i++) {
            if (titleExtdatas.at(i).id() == id) {
                titleExtdatas.at(i).refreshDirectories();
            }
        }
    }
}

/**
 * CACHE STRUCTURE
 * start, len
 * id (0, 8)
 * productCode (8, 16)
 * accessibleSave (24, 1)
 * accessibleExtdata (25, 1)
 * shortDescription (26, 64)
 * longDescription (90, 128)
 * savePath (218, 256)
 * extdataPath (474, 256)
 * mediaType (730, 1)
 * fs cardtype (731, 1)
 * cardtype (732, 1)
 * bigIconData (733, 4608)
 */

void TitleLoader::exportTitleListCache(std::vector<Title>& list, const std::u16string& path)
{
    std::unique_ptr<u8[]> cache = std::unique_ptr<u8[]>(new u8[list.size() * ENTRYSIZE]());
    for (size_t i = 0; i < list.size(); i++) {
        u64 id                       = list.at(i).id();
        bool accessibleSave          = list.at(i).accessibleSave();
        bool accessibleExtdata       = list.at(i).accessibleExtdata();
        std::string shortDescription = StringUtils::UTF16toUTF8(list.at(i).getShortDescription());
        std::string longDescription  = StringUtils::UTF16toUTF8(list.at(i).getLongDescription());
        std::string savePath         = StringUtils::UTF16toUTF8(list.at(i).savePath());
        std::string extdataPath      = StringUtils::UTF16toUTF8(list.at(i).extdataPath());
        FS_MediaType media           = list.at(i).mediaType();
        FS_CardType cardType         = list.at(i).cardType();
        CardType card                = list.at(i).SPICardType();

        if (cardType == CARD_CTR) {
            smdh_s* smdh = loadSMDH(list.at(i).lowId(), list.at(i).highId(), media);
            if (smdh != NULL) {
                memcpy(cache.get() + i * ENTRYSIZE + 733, smdh->bigIconData, 0x900 * 2);
            }
            delete smdh;
        }

        memcpy(cache.get() + i * ENTRYSIZE + 0, &id, sizeof(u64));
        memcpy(cache.get() + i * ENTRYSIZE + 8, list.at(i).productCode, 16);
        memcpy(cache.get() + i * ENTRYSIZE + 24, &accessibleSave, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 25, &accessibleExtdata, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 26, shortDescription.c_str(), shortDescription.length());
        memcpy(cache.get() + i * ENTRYSIZE + 90, longDescription.c_str(), longDescription.length());
        memcpy(cache.get() + i * ENTRYSIZE + 218, savePath.c_str(), savePath.length());
        memcpy(cache.get() + i * ENTRYSIZE + 474, extdataPath.c_str(), extdataPath.length());
        memcpy(cache.get() + i * ENTRYSIZE + 730, &media, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 731, &cardType, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 732, &card, sizeof(u8));
    }

    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
    FSStream output(Archive::sdmc(), path, FS_OPEN_WRITE, list.size() * ENTRYSIZE);
    output.write(cache.get(), list.size() * ENTRYSIZE);
    output.close();
}

void TitleLoader::importTitleListCache(void)
{
    FSStream inputsaves(Archive::sdmc(), StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache"), FS_OPEN_READ);
    u32 sizesaves  = inputsaves.size() / ENTRYSIZE;
    u8* cachesaves = new u8[inputsaves.size()];
    inputsaves.read(cachesaves, inputsaves.size());
    inputsaves.close();

    FSStream inputextdatas(Archive::sdmc(), StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache"), FS_OPEN_READ);
    u32 sizeextdatas  = inputextdatas.size() / ENTRYSIZE;
    u8* cacheextdatas = new u8[inputextdatas.size()];
    inputextdatas.read(cacheextdatas, inputextdatas.size());
    inputextdatas.close();

    g_loadingTitlesLimit = sizesaves + sizeextdatas;

    // fill the lists with blank titles firsts
    for (size_t i = 0, sz = std::max(sizesaves, sizeextdatas); i < sz; i++) {
        Title title;
        title.load();
        if (i < sizesaves) {
            titleSaves.push_back(title);
        }
        if (i < sizeextdatas) {
            titleExtdatas.push_back(title);
        }
    }

    // store already loaded ids
    std::vector<u64> alreadystored;

    for (size_t i = 0; i < sizesaves; i++) {
        u64 id;
        u8 productCode[16];
        bool accessibleSave;
        bool accessibleExtdata;
        char shortDescription[0x40];
        char longDescription[0x80];
        char savePath[256];
        char extdataPath[256];
        FS_MediaType media;
        FS_CardType cardType;
        CardType card;

        memcpy(&id, cachesaves + i * ENTRYSIZE, sizeof(u64));
        memcpy(productCode, cachesaves + i * ENTRYSIZE + 8, 16);
        memcpy(&accessibleSave, cachesaves + i * ENTRYSIZE + 24, sizeof(u8));
        memcpy(&accessibleExtdata, cachesaves + i * ENTRYSIZE + 25, sizeof(u8));
        memcpy(shortDescription, cachesaves + i * ENTRYSIZE + 26, 0x40);
        memcpy(longDescription, cachesaves + i * ENTRYSIZE + 90, 0x80);
        memcpy(savePath, cachesaves + i * ENTRYSIZE + 218, 256);
        memcpy(extdataPath, cachesaves + i * ENTRYSIZE + 474, 256);
        memcpy(&media, cachesaves + i * ENTRYSIZE + 730, sizeof(u8));
        memcpy(&cardType, cachesaves + i * ENTRYSIZE + 731, sizeof(u8));
        memcpy(&card, cachesaves + i * ENTRYSIZE + 732, sizeof(u8));

        Title title;
        title.load(id, productCode, accessibleSave, accessibleExtdata, StringUtils::UTF8toUTF16(shortDescription),
            StringUtils::UTF8toUTF16(longDescription), StringUtils::UTF8toUTF16(savePath), StringUtils::UTF8toUTF16(extdataPath), media, cardType,
            card);

        if (cardType == CARD_CTR) {
            u16 bigIconData[0x900];
            memcpy(bigIconData, cachesaves + i * ENTRYSIZE + 733, 0x900 * 2);
            C2D_Image smallIcon = loadTextureFromBytes(bigIconData);
            title.setIcon(smallIcon);
        }
        else {
            // this cannot happen
            title.setIcon(Gui::noIcon());
        }

        titleSaves.at(i) = title;
        alreadystored.push_back(id);

        g_loadingTitlesCounter++;
    }

    for (size_t i = 0; i < sizeextdatas; i++) {
        u64 id;
        memcpy(&id, cacheextdatas + i * ENTRYSIZE, sizeof(u64));
        std::vector<u64>::iterator it = find(alreadystored.begin(), alreadystored.end(), id);
        if (it == alreadystored.end()) {
            u8 productCode[16];
            bool accessibleSave;
            bool accessibleExtdata;
            char shortDescription[0x40];
            char longDescription[0x80];
            char savePath[256];
            char extdataPath[256];
            FS_MediaType media;
            FS_CardType cardType;
            CardType card;

            memcpy(productCode, cacheextdatas + i * ENTRYSIZE + 8, 16);
            memcpy(&accessibleSave, cacheextdatas + i * ENTRYSIZE + 24, sizeof(u8));
            memcpy(&accessibleExtdata, cacheextdatas + i * ENTRYSIZE + 25, sizeof(u8));
            memcpy(shortDescription, cacheextdatas + i * ENTRYSIZE + 26, 0x40);
            memcpy(longDescription, cacheextdatas + i * ENTRYSIZE + 90, 0x80);
            memcpy(savePath, cacheextdatas + i * ENTRYSIZE + 218, 256);
            memcpy(extdataPath, cacheextdatas + i * ENTRYSIZE + 474, 256);
            memcpy(&media, cacheextdatas + i * ENTRYSIZE + 730, sizeof(u8));
            memcpy(&cardType, cacheextdatas + i * ENTRYSIZE + 731, sizeof(u8));
            memcpy(&card, cacheextdatas + i * ENTRYSIZE + 732, sizeof(u8));

            Title title;
            title.load(id, productCode, accessibleSave, accessibleExtdata, StringUtils::UTF8toUTF16(shortDescription),
                StringUtils::UTF8toUTF16(longDescription), StringUtils::UTF8toUTF16(savePath), StringUtils::UTF8toUTF16(extdataPath), media, cardType,
                card);

            if (cardType == CARD_CTR) {
                u16 bigIconData[0x900];
                memcpy(bigIconData, cacheextdatas + i * ENTRYSIZE + 733, 0x900 * 2);
                C2D_Image smallIcon = loadTextureFromBytes(bigIconData);
                title.setIcon(smallIcon);
            }
            else {
                // this cannot happen
                title.setIcon(Gui::noIcon());
            }

            titleExtdatas.at(i) = title;

            g_loadingTitlesCounter++;
        }
        else {
            auto pos = it - alreadystored.begin();

            // avoid to copy a cartridge title into the extdata list twice
            if (i != 0 && pos == 0) {
                auto newpos         = find(alreadystored.rbegin(), alreadystored.rend(), id);
                titleExtdatas.at(i) = titleSaves.at(alreadystored.rend() - newpos - 1);
            }
            else {
                titleExtdatas.at(i) = titleSaves.at(pos);
            }
        }
    }

    delete[] cachesaves;
    delete[] cacheextdatas;
}

bool TitleLoader::scanCard(void)
{
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
                            if (titleSaves.empty() || titleSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                titleSaves.insert(titleSaves.begin(), title);
                            }
                        }
                        if (title.accessibleExtdata()) {
                            if (titleExtdatas.empty() || titleExtdatas.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                titleExtdatas.insert(titleExtdatas.begin(), title);
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
                if (titleSaves.empty() || titleSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                    titleSaves.insert(titleSaves.begin(), title);
                }
            }
        }
    }
    isScanning = false;
    return ret;
}

void TitleLoader::cartScan(void)
{
    bool oldCardIn;
    FSUSER_CardSlotIsInserted(&oldCardIn);

    while (doCartScan.test_and_set()) {
        bool cardIn = false;

        FSUSER_CardSlotIsInserted(&cardIn);
        if (cardIn != oldCardIn) {
            bool power;
            FSUSER_CardSlotGetCardIFPowerStatus(&power);
            if (cardIn) {
                if (!power) {
                    FSUSER_CardSlotPowerOn(&power);
                }
                while (!power && doCartScan.test_and_set()) {
                    FSUSER_CardSlotGetCardIFPowerStatus(&power);
                }
                svcSleepThread(500'000'000);
                for (size_t i = 0; i < 10; i++) {
                    if ((oldCardIn = scanCard())) {
                        break;
                    }
                }
            }
            else {
                FSUSER_CardSlotPowerOff(&power);
                if (!titleSaves.empty() && titleSaves.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                    titleSaves.erase(titleSaves.begin());
                }
                if (!titleExtdatas.empty() && titleExtdatas.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                    titleExtdatas.erase(titleExtdatas.begin());
                }
                oldCardIn = false;
            }
        }
    }
}

void TitleLoader::loadTitles(bool forceRefreshParam)
{
    auto start = std::chrono::high_resolution_clock::now();
    try {
        static const std::u16string savecachePath    = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache");
        static const std::u16string extdatacachePath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache");

        // on refreshing
        titleSaves.clear();
        titleExtdatas.clear();

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
            g_loadingTitlesCounter = 0;

            // deserialize data
            importTitleListCache();

            g_loadingTitlesCounter = titleSaves.size() + titleExtdatas.size();

            for (auto& title : titleSaves) {
                title.refreshDirectories();
            }
            for (auto& title : titleExtdatas) {
                title.refreshDirectories();
            }
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

            g_loadingTitlesLimit   = nandCount + sdCount + cartCount;
            g_loadingTitlesCounter = 0;

            if (Configuration::getInstance().nandSaves()) {
                AM_GetTitleCount(MEDIATYPE_NAND, &count);
                std::unique_ptr<u64[]> ids_nand = std::unique_ptr<u64[]>(new u64[count]);
                AM_GetTitleList(NULL, MEDIATYPE_NAND, count, ids_nand.get());

                for (u32 i = 0; i < count; i++) {
                    if (validId(ids_nand[i])) {
                        Title title;
                        if (title.load(ids_nand[i], MEDIATYPE_NAND, CARD_CTR)) {
                            if (title.accessibleSave()) {
                                titleSaves.push_back(title);
                            }
                            // TODO: extdata?
                        }
                    }

                    g_loadingTitlesCounter++;
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
                        if (title.accessibleSave()) {
                            titleSaves.push_back(title);
                        }

                        if (title.accessibleExtdata()) {
                            titleExtdatas.push_back(title);
                        }
                    }
                }

                g_loadingTitlesCounter++;
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
                        titleExtdatas.push_back(title);
                    }
                }

                g_loadingTitlesCounter++;
            }
        }

        std::sort(titleSaves.begin(), titleSaves.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        std::sort(titleExtdatas.begin(), titleExtdatas.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        if (!optimizedLoad) {
            ExportTitleListCacheParams* params = new ExportTitleListCacheParams();
            params->titleSaves                 = titleSaves;
            params->titleExtdatas              = titleExtdatas;
            params->savecachePath              = savecachePath;
            params->extdatacachePath           = extdatacachePath;
            Threads::executeTask(exportTitleListCacheThreaded, params);
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
                                titleSaves.insert(titleSaves.begin(), title);
                            }

                            if (title.accessibleExtdata()) {
                                titleExtdatas.insert(titleExtdatas.begin(), title);
                            }
                        }
                    }
                    g_loadingTitlesCounter++;
                }
            }
            else {
                Title title;
                if (title.load(0, MEDIATYPE_GAME_CARD, cardType)) {
                    titleSaves.insert(titleSaves.begin(), title);
                }
                g_loadingTitlesCounter++;
            }
        }
    }
    catch (const std::exception& e) {
        Logging::error("Exception in loadTitles: {}", e.what());
    }
    catch (...) {
        Logging::error("Unknown exception in loadTitles");
    }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    Logging::debug("Title list loaded in {} ms", duration.count());
}

void TitleLoader::loadTitlesThread(void)
{
    // don't load titles while they're loading
    if (g_isLoadingTitles) {
        return;
    }

    g_isLoadingTitles      = true;
    g_loadingTitlesCounter = 0;
    g_loadingTitlesLimit   = 0;

    loadTitles(forceRefresh);
    forceRefresh      = true;
    g_isLoadingTitles = false;
}

void TitleLoader::cartScanFlagTestAndSet(void)
{
    doCartScan.test_and_set();
}

void TitleLoader::clearCartScanFlag(void)
{
    doCartScan.clear();
}
