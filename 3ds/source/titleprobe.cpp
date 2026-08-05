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
#include "dscard.hpp"
#include "paths.hpp"
#include "titlequirks.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstring>

namespace {
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

    // Probe a CTR (3DS) title: SMDH metadata, save/extdata accessibility, and
    // backup-directory creation. Fills the out-params and stores the icon bytes
    // under `id`; returns true when usable.
    bool probeCtr(u64 id, FS_MediaType media, u8* productCode, bool& accessibleSave, bool& gba, bool& accessibleExtdata,
        std::u16string& shortDescription, std::u16string& longDescription, std::u16string& savePath, std::u16string& extdataPath, IconStore& icons)
    {
        const u32 low  = (u32)id;
        const u32 high = (u32)(id >> 32);

        smdh_s* smdh = (id == TID_PKSM) ? loadSMDH("romfs:/PKSM.smdh") : loadSMDH(low, high, media);
        if (smdh == NULL) {
            Logging::error("Failed to load title {:X} due to smdh == NULL", id);
            return false;
        }

        shortDescription = StringUtils::removeForbiddenCharacters(boundedU16String(smdh->applicationTitles[1].shortDescription, 0x40));
        longDescription  = boundedU16String(smdh->applicationTitles[1].longDescription, 0x80);
        savePath         = Paths::ctrSavePath(id, shortDescription);
        extdataPath      = Paths::ctrExtdataPath(id, shortDescription);
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
            icons.storeCtrIcon(id, smdh->bigIconData);
        }

        delete smdh;
        return loadTitle;
    }

    // A DS cart has no title id (AM only vends them for CTR/TWL). Without one,
    // every cart would collide on id 0, making per-title config (favorites,
    // hidden titles, additional save folders) meaningless. Synthesize a stable
    // key from the 4-char ROM game code instead, tagged with a private high word
    // that AM never emits so it can't clash with a real title id or trip the
    // system-exclusion / update / DSi-data ranges in TitleQuirks::isSystemExcluded.
    constexpr u64 DS_CARD_ID_MARKER = 0x0000444E00000000ULL; // 'D','N' — DS NTR
    u64 dsCardId(const char* gameCode)
    {
        u32 packed = 0;
        for (int i = 0; i < 4; i++) {
            packed = (packed << 8) | (u8)gameCode[i];
        }
        return DS_CARD_ID_MARKER | packed;
    }

    // Probe a legacy DS card title: rom header, banner icon, SPI card type. `id`
    // is overwritten with a synthesized stable key (see dsCardId) before use.
    bool probeCard(u64& id, FS_MediaType media, u8* productCode, bool& accessibleSave, bool& gba, bool& accessibleExtdata,
        std::u16string& shortDescription, std::u16string& longDescription, std::u16string& savePath, std::u16string& extdataPath, CardType& spiCard,
        IconStore& icons)
    {
        u8* headerData = new u8[DSCard::headerSize];
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

        // Log the save hardware the header describes for every cart: an
        // unsupported-cart report then arrives with the numbers already in it,
        // next to the JEDEC id and card type SPIGetCardType logs below.
        DSCard::logNandSave(DSCard::parseNandSave(headerData));

        // Replace the placeholder id (0) with a stable per-cart key derived from
        // the game code, so config keyed on the title id works for DS carts too.
        id = dsCardId(gameCode);

        delete[] headerData;
        headerData = new u8[0x23C0];
        FSUSER_GetLegacyBannerData(media, 0LL, headerData);
        icons.storeDsIcon(id, headerData);
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

    // Probe an installed DSiWare / system TWL title on NAND: same legacy header
    // and banner as a DS card, but addressed by title id, with the save living
    // as plain files on the TWL FAT instead of an SPI chip.
    bool probeTwl(u64 id, u8* productCode, bool& accessibleSave, bool& gba, bool& accessibleExtdata, std::u16string& shortDescription,
        std::u16string& longDescription, std::u16string& savePath, std::u16string& extdataPath, IconStore& icons)
    {
        u8* headerData = new u8[0x3B4];
        Result res     = FSUSER_GetLegacyRomHeader(MEDIATYPE_NAND, id, headerData);
        if (R_FAILED(res)) {
            delete[] headerData;
            Logging::error("Failed get legacy rom header for title 0x{:016X} with result 0x{:08X}.", id, res);
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
        FSUSER_GetLegacyBannerData(MEDIATYPE_NAND, id, headerData);
        icons.storeDsIcon(id, headerData);
        delete[] headerData;

        snprintf((char*)productCode, 16, "TWL-%s", gameCode);

        shortDescription = StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(cardTitle));
        longDescription  = shortDescription;
        savePath         = StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/") + StringUtils::UTF8toUTF16(gameCode) + StringUtils::UTF8toUTF16(" ") +
                   shortDescription;
        extdataPath = savePath;

        accessibleSave    = SaveDataSource::twlSave((u32)id, (u32)(id >> 32)).accessible();
        accessibleExtdata = false;
        gba               = false;

        if (!accessibleSave) {
            return false;
        }

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

bool TitleProbe::probe(Title& title, u64 id, FS_MediaType media, FS_CardType card, IconStore& icons)
{
    u8 productCode[16]  = {0};
    bool accessibleSave = false, gba = false, accessibleExtdata = false;
    std::u16string shortDescription, longDescription, savePath, extdataPath;
    CardType spiCard = NO_CHIP;

    bool loadTitle;
    if (card == CARD_CTR) {
        loadTitle =
            probeCtr(id, media, productCode, accessibleSave, gba, accessibleExtdata, shortDescription, longDescription, savePath, extdataPath, icons);
    }
    else if (media == MEDIATYPE_NAND) {
        loadTitle =
            probeTwl(id, productCode, accessibleSave, gba, accessibleExtdata, shortDescription, longDescription, savePath, extdataPath, icons);
    }
    else {
        loadTitle = probeCard(
            id, media, productCode, accessibleSave, gba, accessibleExtdata, shortDescription, longDescription, savePath, extdataPath, spiCard, icons);
    }

    // On a hard failure (smdh == NULL, header/SPI error) probeCtr/probeCard
    // return false with every facet inaccessible; we still publish the Title but
    // the caller discards it on a false return, exactly as the old load() did.
    title.load(id, productCode, accessibleSave, gba, accessibleExtdata, StringUtils::UTF16toUTF8(shortDescription),
        StringUtils::UTF16toUTF8(longDescription), savePath, extdataPath, media, card, spiCard);
    title.refreshDirectories();
    return loadTitle;
}
