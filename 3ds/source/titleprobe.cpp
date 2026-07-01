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

#include "titleprobe.hpp"
#include "titlequirks.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {
    Tex3DS_SubTexture dsIconSubt3x = {32, 32, 0.0f, 1.0f, 1.0f, 0.0f};
    C2D_Image dsIcon               = {nullptr, &dsIconSubt3x};

    C2D_Image loadTextureIcon(smdh_s* smdh)
    {
        return loadTextureFromBytes(smdh->bigIconData);
    }

    // Some titles (notably malformed VC injects) don't null-terminate SMDH title
    // fields; reading them as raw char16_t* would run past the fixed-size array
    // into whatever struct data follows, producing a huge garbage string that can
    // overflow the shared C2D text buffer. Bound the scan to the field's size.
    std::u16string boundedU16String(const u16* data, size_t maxLen)
    {
        size_t len = 0;
        while (len < maxLen && data[len] != 0) {
            len++;
        }
        return std::u16string((const char16_t*)data, len);
    }

    void loadDSIcon(u8* banner)
    {
        static constexpr int WIDTH_POW2  = 32;
        static constexpr int HEIGHT_POW2 = 32;
        if (!dsIcon.tex) {
            dsIcon.tex = new C3D_Tex;
            C3D_TexInit(dsIcon.tex, WIDTH_POW2, HEIGHT_POW2, GPU_RGB565);
        }

        struct bannerData {
            u16 version;
            u16 crc;
            u8 reserved[28];
            u8 data[512];
            u16 palette[16];
        };
        bannerData* iconData = (bannerData*)banner;

        u16* output = (u16*)dsIcon.tex->data;
        for (size_t x = 0; x < 32; x++) {
            for (size_t y = 0; y < 32; y++) {
                u32 srcOff   = (((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7)) * 4 + ((x & 7) >> 1);
                u32 srcShift = (x & 1) * 4;

                u16 pIndex = (iconData->data[srcOff] >> srcShift) & 0xF;
                u16 color  = 0xFFFF;
                if (pIndex != 0) {
                    u16 r = iconData->palette[pIndex] & 0x1F;
                    u16 g = (iconData->palette[pIndex] >> 5) & 0x1F;
                    u16 b = (iconData->palette[pIndex] >> 10) & 0x1F;
                    color = (r << 11) | (g << 6) | (g >> 4) | (b);
                }

                u32 dst     = ((((y >> 3) * (32 >> 3) + (x >> 3)) << 6) +
                           ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
                output[dst] = color;
            }
        }
    }

    // Probe a CTR (3DS) title: SMDH metadata, save/extdata accessibility, and
    // backup-directory creation. Fills the out-params; returns true when usable.
    bool probeCtr(u64 id, FS_MediaType media, u8* productCode, bool& accessibleSave, bool& gba, bool& accessibleExtdata,
        std::u16string& shortDescription, std::u16string& longDescription, std::u16string& savePath, std::u16string& extdataPath, C2D_Image& icon,
        bool& hasIcon)
    {
        const u32 low    = (u32)id;
        const u32 high   = (u32)(id >> 32);
        const u32 unique = low >> 8;

        smdh_s* smdh = (id == TID_PKSM) ? loadSMDH("romfs:/PKSM.smdh") : loadSMDH(low, high, media);
        if (smdh == NULL) {
            Logging::error("Failed to load title {:X} due to smdh == NULL", id);
            return false;
        }

        char uniqueStr[12] = {0};
        sprintf(uniqueStr, "0x%05X ", (unsigned int)unique);

        shortDescription = StringUtils::removeForbiddenCharacters(boundedU16String(smdh->applicationTitles[1].shortDescription, 0x40));
        longDescription  = boundedU16String(smdh->applicationTitles[1].longDescription, 0x80);
        savePath         = StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/") + StringUtils::UTF8toUTF16(uniqueStr) + shortDescription;
        extdataPath      = StringUtils::UTF8toUTF16("/3ds/Checkpoint/extdata/") + StringUtils::UTF8toUTF16(uniqueStr) + shortDescription;
        AM_GetTitleProductCode(media, id, (char*)productCode);

        accessibleSave    = SaveDataSource::ctrSave(media, low, high).accessible();
        gba               = (!accessibleSave) && SaveDataSource::rawGba(media, low, high).accessible();
        accessibleExtdata = SaveDataSource::extdata(TitleQuirks::extdataIdFor(id)).accessible();

        bool loadTitle = false;
        if (accessibleSave || gba) {
            loadTitle = true;
            if (!io::directoryExists(Archive::sdmc(), savePath)) {
                Result res = io::createDirectory(Archive::sdmc(), savePath);
                if (R_FAILED(res)) {
                    loadTitle = false;
                    Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
                }
            }
        }

        if (accessibleExtdata) {
            loadTitle = true;
            if (!io::directoryExists(Archive::sdmc(), extdataPath)) {
                Result res = io::createDirectory(Archive::sdmc(), extdataPath);
                if (R_FAILED(res)) {
                    loadTitle = false;
                    Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
                }
            }
        }

        if (loadTitle) {
            icon    = loadTextureIcon(smdh);
            hasIcon = true;
        }

        delete smdh;
        return loadTitle;
    }

    // Probe a legacy DS card title: rom header, banner icon, SPI card type.
    bool probeCard(FS_MediaType media, u8* productCode, bool& accessibleSave, bool& gba, bool& accessibleExtdata, std::u16string& shortDescription,
        std::u16string& longDescription, std::u16string& savePath, std::u16string& extdataPath, CardType& spiCard, C2D_Image& icon, bool& hasIcon)
    {
        u8* headerData = new u8[0x3B4];
        Result res     = FSUSER_GetLegacyRomHeader(media, 0LL, headerData);
        if (R_FAILED(res)) {
            delete[] headerData;
            Logging::error("Failed get legacy rom header with result 0x{:08X}.", res);
            return false;
        }

        char cardTitle[14] = {0};
        char gameCode[6]   = {0};

        std::copy(headerData, headerData + 12, cardTitle);
        std::copy(headerData + 12, headerData + 16, gameCode);
        cardTitle[13] = '\0';
        gameCode[5]   = '\0';

        delete[] headerData;
        headerData = new u8[0x23C0];
        FSUSER_GetLegacyBannerData(media, 0LL, headerData);
        loadDSIcon(headerData);
        icon    = dsIcon;
        hasIcon = true;
        delete[] headerData;

        res = SPIGetCardType(&spiCard, (gameCode[0] == 'I') ? 1 : 0);
        if (R_FAILED(res)) {
            Logging::error("Failed get SPI Card Type with result 0x{:08X}.", res);
            return false;
        }

        // No AM product-code API exists for a legacy card; synthesize the
        // "NTR-XXXX" cart identifier from the ROM header's game code instead.
        snprintf((char*)productCode, 16, "NTR-%s", gameCode);

        shortDescription = StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(cardTitle));
        longDescription  = shortDescription;
        savePath         = StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/") + StringUtils::UTF8toUTF16(gameCode) + StringUtils::UTF8toUTF16(" ") +
                   shortDescription;
        extdataPath = savePath;

        accessibleSave    = true;
        accessibleExtdata = false;
        gba               = false;

        bool loadTitle = true;
        if (!io::directoryExists(Archive::sdmc(), savePath)) {
            res = io::createDirectory(Archive::sdmc(), savePath);
            if (R_FAILED(res)) {
                loadTitle = false;
                Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
            }
        }
        return loadTitle;
    }
}

bool TitleProbe::probe(Title& title, u64 id, FS_MediaType media, FS_CardType card)
{
    u8 productCode[16]  = {0};
    bool accessibleSave = false, gba = false, accessibleExtdata = false;
    std::u16string shortDescription, longDescription, savePath, extdataPath;
    CardType spiCard = NO_CHIP;
    C2D_Image icon{};
    bool hasIcon = false;

    bool loadTitle;
    if (card == CARD_CTR) {
        loadTitle = probeCtr(
            id, media, productCode, accessibleSave, gba, accessibleExtdata, shortDescription, longDescription, savePath, extdataPath, icon, hasIcon);
    }
    else {
        loadTitle = probeCard(media, productCode, accessibleSave, gba, accessibleExtdata, shortDescription, longDescription, savePath, extdataPath,
            spiCard, icon, hasIcon);
    }

    // On a hard failure (smdh == NULL, header/SPI error) probeCtr/probeCard
    // return false with every facet inaccessible; we still publish the Title but
    // the caller discards it on a false return, exactly as the old load() did.
    title.load(id, productCode, accessibleSave, gba, accessibleExtdata, StringUtils::UTF16toUTF8(shortDescription),
        StringUtils::UTF16toUTF8(longDescription), savePath, extdataPath, media, card, spiCard);
    if (hasIcon) {
        title.setIcon(icon);
    }
    title.refreshDirectories();
    return loadTitle;
}
