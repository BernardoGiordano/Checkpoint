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

#ifndef DSCARD_HPP
#define DSCARD_HPP

#include <3ds.h>
#include <cstddef>

// A few DS carts have no SPI save chip at all: ROM and save share one on-cart
// NAND, and the save lives in the "RW area" the ROM header points at. Reaching
// it means driving the card's ROM bus in NAND mode (commands 0x8B ROM mode,
// 0xB2 RW mode, 0x81/0x82/0x84/0x85 write buffer, 0xD6 status) — the card
// hardware talk DS mode does. In 3DS mode the system only ever exposes the SPI
// save chip (pxi:dev) and the legacy header/banner reads, so such a save can
// neither be backed up nor restored from here; GodMode9i, running in DS mode,
// is the tool for it. What Checkpoint can do is recognize the cart and say so,
// instead of reporting the empty SPI probe as a failed archive open.
namespace DSCard {
    // The slice of the NDS ROM header FSUSER_GetLegacyRomHeader vends.
    inline constexpr size_t headerSize = 0x3B4;

    // Save hardware as described by the ROM header. Filled for every DS cart, so
    // the log carries the same fields for carts that turn out to be ordinary.
    struct NandSave {
        bool present     = false; // save lives in on-cart NAND, not on an SPI chip
        bool headerRead  = false; // false when the ROM header could not be read
        char gameCode[5] = {0};   // "UORE" & co, NUL-terminated
        u8 unitCode      = 0;     // 0 = NTR cart, nonzero = TWL cart (unit size differs)
        u16 rawRomEnd    = 0;     // header 0x94, in cart units
        u16 rawRwStart   = 0;     // header 0x96, in cart units
        u32 romEnd       = 0;     // rawRomEnd decoded to a byte offset
        u32 rwStart      = 0;     // rawRwStart decoded to a byte offset: where the save starts
        u32 saveSize     = 0;     // known per game code; 0 for a NAND cart we don't know the size of
    };

    // Parses a header of at least `headerSize` bytes.
    NandSave parseNandSave(const u8* header);

    // Reads the inserted card's ROM header and parses it. `headerRead` is false,
    // and everything else zero, when the header cannot be read.
    NandSave probeNandSave(FS_MediaType media);

    // Logs the save-hardware fields above. Called for every DS cart probe so a bug
    // report about an unsupported cart arrives with the numbers already in it.
    void logNandSave(const NandSave& info);
}

#endif
