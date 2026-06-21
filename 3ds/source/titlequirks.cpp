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

#include "titlequirks.hpp"

bool TitleQuirks::isSystemExcluded(u64 id)
{
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
            return true;
    }

    // updates
    if ((id >> 32) == 0x0004000E) {
        return true;
    }

    return false;
}

u32 TitleQuirks::extdataIdFor(u64 id)
{
    u32 low = (u32)id;
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

bool TitleQuirks::isActivityLog(u32 lowId, FS_MediaType media)
{
    bool activityId = false;
    switch (lowId) {
        case 0x00020200:
        case 0x00021200:
        case 0x00022200:
        case 0x00026200:
        case 0x00027200:
        case 0x00028200:
            activityId = true;
    }
    return media == MEDIATYPE_NAND && activityId;
}
