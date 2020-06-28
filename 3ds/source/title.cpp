/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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
#include "fspxi.hpp"
#include "appdata.hpp"
#include "logger.hpp"
#include "platform.hpp"
#include "stringutils.hpp"
#include "io.hpp"
#include "archive.hpp"
#include "config.hpp"
#include "smdh.hpp"
#include "colors.hpp"

#include <cstdio>
#include <cstring>

#include <filesystem>
namespace fs = std::filesystem;

static constexpr u64 TID_PKSM = 0x000400000EC10000ULL;

namespace Validation {
    bool titleId(u64 id)
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

        return !Configuration::get().filter(id);
    }
}

namespace Icon {
    struct Tex {
        C3D_Tex t;
        bool exists;
        Tex() : exists(false)
        {

        }
        void init(u16 w, u16 h, GPU_TEXCOLOR format)
        {
            if (!exists) {
                exists = true;
                C3D_TexInit(&t, w, h, format);
                t.border = 0xFFFFFFFF;
		        C3D_TexSetWrap(&t, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
            }
        }

        void clear()
        {
            if (exists) {
                exists = false;
                C3D_TexDelete(&t);
            }
        }
        ~Tex()
        {
            clear();
        }
    };

    // Icons are packed into 256x256 textures in a 5x5 grid to reduce memory footprint and maybe increase performance
    // (max installed title count)/(5x5) = 300/25 = 12 textures
    // with 12% of memory wasted each, vs the arbitrary previous amount
    // that were 64x64 with 43% wasted each
    Tex iconPacks[12];
    void clearIcons()
    {
        for(auto& pack : iconPacks) {
            pack.clear();
        }
    }
    void addIcon(TitleInfo& ti, long idx)
    {
        ldiv_t result = ldiv(idx, 5*5);
        ldiv_t subresult = ldiv(result.rem, 5);
        Tex& t = iconPacks[result.quot];
        t.init(256, 256, GPU_RGB565);

        constexpr float factor = 48.0f / 256.0f;
        const float top  = 1.0f - (subresult.quot * factor);
        const float left = subresult.rem * factor;
        ti.mSubTex = {48, 48, left, top, left + factor, top - factor};

        ti.mIcon.tex = &t.t;

        u8* dest = ((u8*)t.t.data) + (((subresult.rem * 48 * 8) + (subresult.quot * 48 * 256)) * sizeof(u16)) ;
        u8* src  = ti.mIconData;

        for (int j = 0; j < 48; j += 8) {
            memcpy(dest, src, 48 * 8 * sizeof(u16));
            src += 48 * 8 * sizeof(u16);
            dest += 256 * 8 * sizeof(u16);
        }
    }
}
namespace DSIcon {
    constexpr u32 WIDTH_POW2  = 32;
    constexpr u32 HEIGHT_POW2 = 32;
    constexpr Tex3DS_SubTexture dsIconSubt3x = {WIDTH_POW2, HEIGHT_POW2, 0.0f, 1.0f, 1.0f, 0.0f};
    Icon::Tex iconTex;
    bool alreadyInited = false;

    void load(u8* banner)
    {
        iconTex.init(WIDTH_POW2, HEIGHT_POW2, GPU_RGB565);
        // iconTex.init(WIDTH_POW2, HEIGHT_POW2, GPU_RGBA5551);

        struct {
            u16 version;
            u16 crc;
            u8 reserved[28];
            u8 data[512];
            u16 palette[16];
        } iconData;
        memcpy(&iconData, banner, sizeof(iconData));

        u8* output = (u8*)iconTex.t.data;
        for (size_t x = 0; x < WIDTH_POW2; x++) {
            for (size_t y = 0; y < HEIGHT_POW2; y++) {
                u32 srcOff   = (((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7)) * 4 + ((x & 7) >> 1);
                u32 srcShift = (x & 1) * 4;

                u16 pIndex = (iconData.data[srcOff] >> srcShift) & 0xF;
                u16 color  = 0xFFFF;
                if (pIndex != 0) {
                    u16 r = iconData.palette[pIndex] & 0x1F;
                    u16 g = (iconData.palette[pIndex] >> 5) & 0x1F;
                    u16 b = (iconData.palette[pIndex] >> 10) & 0x1F;
                    color = (r << 11) | (g << 6) | (g >> 4) | (b);
                    // color = (r << 11) | (g << 6) | (b << 1) | (1);
                }

                u32 dst     = ((((y >> 3) * (WIDTH_POW2 >> 3) + (x >> 3)) << 6) +
                        ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
                memcpy(output + (dst * sizeof(u16)), &color, sizeof(u16));
            }
        }
    }
}

TitleInfo::TitleInfo(u64 _id, FS_MediaType _media, FS_CardType _card)
{
    mId = _id;
    mMedia = _media;
    mCard = _card;
    mCardType = SPI::CHIP_LAST;

    #define FILL_WITH(cont, val) std::fill(std::begin(cont), std::end(cont), val)
    FILL_WITH(mUniqueIdText, 0);
    FILL_WITH(mProductCode, 0);
    FILL_WITH(mShortDesc, 0);
    FILL_WITH(mLongDesc, 0);
    FILL_WITH(mIconData, 0);
    #undef FILL_WITH

    mHasSave    = false;
    mIsGBAVC    = false;
    mHasExtdata = false;

    mIcon   = {nullptr, nullptr};
    mSubTex = {0, 0, 0, 0, 0, 0};
}

u32 TitleInfo::uniqueId() const
{
    return (lowId() >> 8);
}
u32 TitleInfo::highId() const
{
    return u32((mId >> 32) & 0xFFFFFFFF);
}
u32 TitleInfo::lowId() const
{
    return u32(mId & 0xFFFFFFFF);
}
u32 TitleInfo::extdataId() const
{
    const u32 low = lowId();
    switch (low) {
        case 0x00055E00:
            return 0x055D; // Pokémon Y
        case 0x0011C400:
            return 0x11C5; // Pokémon Omega Ruby
        case 0x00175E00:
            return 0x1648; // Pokémon Moon
        case 0x00179600:
        case 0x00179800:
            return 0x1794; // Fire Emblem Conquest SE NA
        case 0x00179700:
        case 0x0017A800:
            return 0x1795; // Fire Emblem Conquest SE EU
        case 0x0012DD00:
        case 0x0012DE00:
            return 0x12DC; // Fire Emblem If JP
        case 0x001B5100:
            return 0x1B50; // Pokémon Ultramoon
                           // TODO: need confirmation for this
                           // case 0x001C5100:
                           // case 0x001C5300:
                           //     return 0x0BD3; // Etrian Odyssey V: Beyond the Myth
    }

    return low >> 8;
}
const char* TitleInfo::mediatypeString() const
{
    switch (mMedia) {
        case MEDIATYPE_SD:
            return "SD Card";
        case MEDIATYPE_GAME_CARD:
            return "Cartridge";
        case MEDIATYPE_NAND:
            return "NAND";
        default:
            return " ";
    }

    return " ";
}

void Title::loaderThreadFunc(void* arg)
{
    DataHolder& data = *static_cast<DataHolder*>(arg);
    while(LightEvent_Wait(&data.titleLoadingThreadBeginEvent), data.titleLoadingThreadKeepGoing.test_and_set()) // ayy, comma operator saves the day
    {
        data.titleLoadingComplete = false;

        LightLock_Lock(&data.backupableVectorLock);
        data.thingsToActOn[BackupTypes::Save].clear();
        data.thingsToActOn[BackupTypes::Extdata].clear();
        data.titles.clear();
        LightLock_Unlock(&data.backupableVectorLock);

        #define ADD_TITLE(TypSav) { \
            LightLock_Lock(&data.backupableVectorLock); \
            if (title.mInfo.mCard == CARD_CTR) { \
                Icon::addIcon(title.mInfo, data.titles.size()); \
            } \
            title.refreshDirectories(); \
            if (title.mInfo.mIsGBAVC) { \
                auto holder = std::make_unique<BackupableWithData<GBASaveTitleHolder>>(data.titles); \
                data.thingsToActOn[BackupTypes::Save].push_back(std::move(holder)); \
                \
            } \
            else if (title.mInfo.mHasSave) { \
                auto holder = std::make_unique<BackupableWithData<TypSav>>(data.titles); \
                data.thingsToActOn[BackupTypes::Save].push_back(std::move(holder)); \
            } \
            \
            if (title.mInfo.mHasExtdata) { \
                auto holder = std::make_unique<BackupableWithData<ExtdataTitleHolder>>(data.titles); \
                data.thingsToActOn[BackupTypes::Extdata].push_back(std::move(holder)); \
            } \
            data.titles.push_back(std::move(title));  \
            LightLock_Unlock(&data.backupableVectorLock); \
        }

        u32 count = 0;
        if (Configuration::get().nandSaves()) {
            AM_GetTitleCount(MEDIATYPE_NAND, &count);
            auto ids_nand = std::make_unique<u64[]>(count);
            AM_GetTitleList(nullptr, MEDIATYPE_NAND, count, ids_nand.get());

            for (u32 i = 0; i < count; i++) {
                if (!data.titleLoadingThreadKeepGoing.test_and_set()) return;

                if (Validation::titleId(ids_nand[i])) {
                    Title title(ids_nand[i], MEDIATYPE_NAND, CARD_CTR);
                    if (title.mValid) ADD_TITLE(SaveTitleHolder);
                }
            }
        }

        AM_GetTitleCount(MEDIATYPE_SD, &count);
        auto ids_sd = std::make_unique<u64[]>(count);
        AM_GetTitleList(nullptr, MEDIATYPE_SD, count, ids_sd.get());

        for (u32 i = 0; i < count; i++) {
            if (!data.titleLoadingThreadKeepGoing.test_and_set()) return;

            if (Validation::titleId(ids_sd[i])) {
                Title title(ids_sd[i], MEDIATYPE_SD, CARD_CTR);
                if (title.mValid) ADD_TITLE(SaveTitleHolder);
            }
        }

        if (!data.titleLoadingThreadKeepGoing.test_and_set()) return;

        const bool isPKSMIdAlreadyHere = std::find(ids_sd.get(), ids_sd.get() + count, TID_PKSM);
        if (!isPKSMIdAlreadyHere) {
            Title title(TID_PKSM, MEDIATYPE_SD, CARD_CTR);
            if (title.mValid) ADD_TITLE(SaveTitleHolder);
        }

        if (!data.titleLoadingThreadKeepGoing.test_and_set()) return;

        FS_CardType cardType;
        Result res = FSUSER_GetCardType(&cardType);
        if (R_SUCCEEDED(res)) {
            if (cardType == CARD_CTR) {
                u32 count = 0;
                AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
                if (count > 0) {
                    u64 id_card = 0;
                    AM_GetTitleList(nullptr, MEDIATYPE_GAME_CARD, 1, &id_card);
                    if (Validation::titleId(id_card)) {
                        Title title(id_card, MEDIATYPE_GAME_CARD, cardType);
                        if (title.mValid) ADD_TITLE(SaveTitleHolder);
                    }
                }
            }
            else {
                Title title(0, MEDIATYPE_GAME_CARD, cardType);
                if (title.mValid) ADD_TITLE(DSSaveTitleHolder); // the Extdata will never be used for a DS card
            }
        }

        #undef ADD_TITLE

        data.titleLoadingComplete = true;
    }
}
void Title::freeIcons()
{
    Icon::clearIcons();
}

bool Title::isActivityLog(const TitleInfo& info)
{
    bool activityId = false;
    switch (info.lowId()) {
        case 0x00020200:
        case 0x00021200:
        case 0x00022200:
        case 0x00026200:
        case 0x00027200:
        case 0x00028200:
            activityId = true;
    }
    return info.mMedia == MEDIATYPE_NAND && activityId;
}

std::string Title::saveBackupsDir(const TitleInfo& info)
{
    return StringUtils::format("%s/%s %s", Platform::Directories::SaveBackupsDir, info.mUniqueIdText, info.mShortDesc);
}
std::string Title::extdataBackupsDir(const TitleInfo& info)
{
    return StringUtils::format("%s/%s %s", Platform::Directories::ExtdataBackupsDir, info.mUniqueIdText, info.mShortDesc);
}

Title::Title(u64 _id, FS_MediaType _media, FS_CardType _card) : mInfo(_id, _media, _card), mValid(false)
{
    if (mInfo.mCard == CARD_CTR) {
        std::unique_ptr<smdh_s> smdh;
        if (mInfo.mId == TID_PKSM) {
            smdh = loadSMDH("romfs:/PKSM.smdh");
        }
        else {
            smdh = loadSMDH(mInfo.lowId(), mInfo.highId(), mInfo.mMedia);
        }
        if (!smdh) {
            Logger::log(Logger::ERROR, "Failed to load title 0x%016lX due to smdh == NULL", mInfo.mId);
            return;
        }

        snprintf(mInfo.mUniqueIdText, 8, "0x%05lX", mInfo.uniqueId());

        std::string desc = StringUtils::removeForbiddenCharacters(StringUtils::UTF16toUTF8(smdh->applicationTitles[1].shortDescription));
        strncpy(mInfo.mShortDesc, desc.c_str(), 0x80);
        desc = StringUtils::removeForbiddenCharacters(StringUtils::UTF16toUTF8(smdh->applicationTitles[1].longDescription));
        strncpy(mInfo.mLongDesc, desc.c_str(), 0x100);
        AM_GetTitleProductCode(mInfo.mMedia, mInfo.mId, mInfo.mProductCode);

        Result saveResult = -1;
        bool isGBA = false;
        if (mInfo.mMedia == MEDIATYPE_NAND) {
            const u32 path[2] = {mInfo.mMedia, (0x00020000 | (mInfo.lowId() >> 8))};
            saveResult = Archive::mount(ARCHIVE_SYSTEM_SAVEDATA, {PATH_BINARY, 8, path});
        }
        else {
            const u32 path[3] = {mInfo.mMedia, mInfo.lowId(), mInfo.highId()};
            saveResult = Archive::mount(ARCHIVE_USER_SAVEDATA, {PATH_BINARY, 12, path});
        }
        if (R_SUCCEEDED(saveResult)) {
            Archive::unmount();
        }
        else {
            if (FSPXI::checkHasSave(mInfo.lowId(), mInfo.highId(), mInfo.mMedia)) {
                saveResult = 0;
                isGBA = true;
            }
        }
        
        Result extdataResult = -1;
        if (mInfo.mMedia != MEDIATYPE_NAND)
        {
            const u32 path[3] = {MEDIATYPE_SD, mInfo.extdataId(), 0};
            extdataResult = Archive::mount(ARCHIVE_EXTDATA, {PATH_BINARY, 12, path});
        }
        if (R_SUCCEEDED(extdataResult)) {
            Archive::unmount();
        }

        mInfo.mHasSave    = R_SUCCEEDED(saveResult);
        mInfo.mIsGBAVC    = isGBA;
        mInfo.mHasExtdata = R_SUCCEEDED(extdataResult);

        bool loadTitle = false;

        if (mInfo.mHasSave) {
            loadTitle = true;
            IODataHolder iodata;
            iodata.srcPath = saveBackupsDir(mInfo);
            if (!io::directoryExists(iodata)) {
                io::ActionResult res = io::createDirectory(iodata);
                if (res == io::ActionResult::Failure) {
                    loadTitle = false;
                    Logger::log(Logger::ERROR, "Failed to create save backup directory.");
                }
            }
        }

        if (mInfo.mHasExtdata) {
            loadTitle = true;
            IODataHolder iodata;
            iodata.srcPath = extdataBackupsDir(mInfo);
            if (!io::directoryExists(iodata)) {
                io::ActionResult res = io::createDirectory(iodata);
                if (res == io::ActionResult::Failure) {
                    loadTitle = false;
                    Logger::log(Logger::ERROR, "Failed to create extdata backup directory.");
                }
            }
        }

        if (loadTitle) {
            memcpy(mInfo.mIconData, smdh->bigIconData, 48 * 48 * 2);
        }

        mValid = loadTitle;
    }
    else {
        // 0x3B0 fits in 0x23C0, so allocate the bigger buffer and use that for the whole function
        auto headerData = std::make_unique<u8[]>(0x23C0);
        Result res      = FSUSER_GetLegacyRomHeader(mInfo.mMedia, 0LL, headerData.get());
        if (R_FAILED(res)) {
            Logger::log(Logger::ERROR, "Failed get legacy rom header with result 0x%08lX.", res);
            return;
        }

        char _cardTitle[14] = {0};
        std::fill(std::begin(mInfo.mUniqueIdText), std::end(mInfo.mUniqueIdText), 0);
        std::copy(headerData.get() +  0, headerData.get() + 12, _cardTitle);
        std::copy(headerData.get() + 12, headerData.get() + 16, mInfo.mUniqueIdText);
        _cardTitle[13] = '\0';

        FSUSER_GetLegacyBannerData(mInfo.mMedia, 0LL, headerData.get());
        DSIcon::load(headerData.get());

        res = SPI::GetCardType(&mInfo.mCardType, (mInfo.mUniqueIdText[0] == 'I') ? 1 : 0);
        if (R_FAILED(res)) {
            Logger::log(Logger::ERROR, "Failed get SPI Card Type with result 0x%08lX.", res);
            return;
        }

        auto title = StringUtils::removeForbiddenCharacters(_cardTitle);
        std::copy(title.begin(), title.end(), mInfo.mShortDesc);
        std::copy(title.begin(), title.end(), mInfo.mLongDesc);
        memset(mInfo.mProductCode, 0, 16);

        mInfo.mHasSave    = true;
        mInfo.mIcon       = {&DSIcon::iconTex.t, &DSIcon::dsIconSubt3x};

        mValid = true;
    }
}
Title::Title(TitleInfo&& info) : mInfo(info), mValid(true)
{

}

void Title::refreshDirectories()
{
    mSaveBackups.clear();
    mExtdataBackups.clear();

    std::error_code ec;  // used to not crash when folders dont exist

    if (mInfo.mHasSave) {
        // standard save backups
        for(auto& bak_entry : fs::directory_iterator(Title::saveBackupsDir(mInfo), ec)) {
            if (bak_entry.is_directory()) {
                mSaveBackups.push_back(std::make_pair(-1, bak_entry.path().stem().string()));
            }
        }
        ec.clear();

        // save backups from configuration
        int idx = 0;
        for(const auto& additionalPath : Configuration::get().additionalSaveFolders(mInfo.mId)) {
            for(auto& bak_entry : fs::directory_iterator(additionalPath, ec)) {
                if (bak_entry.is_directory()) {
                    mSaveBackups.push_back(std::make_pair(idx, bak_entry.path().stem().string()));
                }
            }
            ec.clear();
            idx++;
        }

        std::sort(mSaveBackups.begin(), mSaveBackups.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    }

    if (mInfo.mHasExtdata) {
        // standard extdata backups
        for(auto& bak_entry : fs::directory_iterator(Title::extdataBackupsDir(mInfo), ec)) {
            if (bak_entry.is_directory()) {
                mExtdataBackups.push_back(std::make_pair(-1, bak_entry.path().stem().string()));
            }
        }
        ec.clear();

        // extdata backups from configuration
        int idx = 0;
        for(const auto& additionalPath : Configuration::get().additionalExtdataFolders(mInfo.mId)) {
            for(auto& bak_entry : fs::directory_iterator(additionalPath, ec)) {
                if (bak_entry.is_directory()) {
                    mExtdataBackups.push_back(std::make_pair(idx, bak_entry.path().stem().string()));
                }
            }
            ec.clear();
            idx++;
        }

        std::sort(mExtdataBackups.begin(), mExtdataBackups.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    }
}
