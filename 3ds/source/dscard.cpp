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

#include "dscard.hpp"
#include "logging.hpp"
#include <cstring>

namespace {
    // NDS ROM header offsets (GBATEK).
    constexpr size_t OFF_GAME_CODE    = 0x0C;
    constexpr size_t OFF_UNIT_CODE    = 0x12;
    constexpr size_t OFF_NAND_ROMEND  = 0x94;
    constexpr size_t OFF_NAND_RWSTART = 0x96;

    // The header stores both NAND offsets in cart units, which differ between an
    // NTR cart (WarioWare D.I.Y., Jam with the Band) and a TWL one (Face Training).
    constexpr u32 unitSize(u8 unitCode)
    {
        return unitCode == 0 ? 0x20000 : 0x80000;
    }

    // The header says where the RW area starts but not how far it runs, so the
    // size comes from the game code, as in GodMode9i's cardNandGetSaveSize.
    u32 nandSaveSize(const char* gameCode)
    {
        // Region byte (gameCode[3]) is not part of the match.
        struct Known {
            char code[4];
            u32 size;
        };
        static constexpr Known known[] = {
            {"UXB", 8u << 20},   // Jam with the Band / Daigasso! Band Brothers DX
            {"UOR", 16u << 20},  // WarioWare D.I.Y.
            {"USK", 64u << 20},  // Face Training
            {"UGD", 128u << 20}, // DS Guide
        };

        for (const auto& k : known) {
            if (std::strncmp(gameCode, k.code, 3) == 0) {
                return k.size;
            }
        }
        return 0;
    }

    u16 readU16(const u8* p)
    {
        return (u16)(p[0] | (p[1] << 8));
    }
}

DSCard::NandSave DSCard::parseNandSave(const u8* header)
{
    NandSave info;
    info.headerRead = true;

    std::memcpy(info.gameCode, header + OFF_GAME_CODE, 4);
    info.gameCode[4] = '\0';
    info.unitCode    = header[OFF_UNIT_CODE];
    info.rawRomEnd   = readU16(header + OFF_NAND_ROMEND);
    info.rawRwStart  = readU16(header + OFF_NAND_RWSTART);
    info.saveSize    = nandSaveSize(info.gameCode);

    // An ordinary cart leaves both fields zero, so a plausible pair is the
    // detection: nonzero ROM end with the RW area starting at or after it.
    const bool headerSaysNand = info.rawRomEnd != 0 && info.rawRwStart >= info.rawRomEnd;
    if (headerSaysNand) {
        info.romEnd  = (u32)info.rawRomEnd * unitSize(info.unitCode);
        info.rwStart = (u32)info.rawRwStart * unitSize(info.unitCode);
    }
    else if (info.saveSize != 0) {
        // Jam with the Band (J) is a NAND cart whose header omits the fields; its
        // RW area sits at the fixed offset GodMode9i falls back to.
        info.romEnd  = 0x7200000;
        info.rwStart = 0x7200000;
    }

    info.present = headerSaysNand || info.saveSize != 0;
    return info;
}

DSCard::NandSave DSCard::probeNandSave(FS_MediaType media)
{
    u8 header[headerSize] = {0};

    Result res = FSUSER_GetLegacyRomHeader(media, 0LL, header);
    if (R_FAILED(res)) {
        Logging::error("Failed to read the DS card rom header with result 0x{:08X}.", (u32)res);
        return NandSave{};
    }

    return parseNandSave(header);
}

void DSCard::logNandSave(const NandSave& info)
{
    if (!info.headerRead) {
        Logging::info("DS card save hardware: rom header unavailable.");
        return;
    }

    Logging::info("DS card {} save hardware: unit code 0x{:02X}, NAND rom end 0x{:04X}, NAND rw start 0x{:04X}.", info.gameCode, info.unitCode,
        info.rawRomEnd, info.rawRwStart);

    if (info.present) {
        Logging::info("DS card {} stores its save in on-cart NAND at offset 0x{:08X} (size 0x{:08X}); 3DS mode cannot reach it.", info.gameCode,
            info.rwStart, info.saveSize);
    }
}
