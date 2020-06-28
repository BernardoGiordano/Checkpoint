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

#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#define BASE_CKPT_DIR "sdmc:/3ds/Checkpoint"

namespace Platform {
    namespace Files {
        constexpr const char* Log = BASE_CKPT_DIR "/checkpoint.log";
        constexpr const char* Cache = BASE_CKPT_DIR "/fulltitlecache";
        constexpr const char* Config = BASE_CKPT_DIR "/config.json";
        constexpr const char* Cheats = BASE_CKPT_DIR "/cheats.json";
        constexpr const char* DefaultConfig = "romfs:/config.json";
        constexpr const char* DefaultCheats = "romfs:/cheats/cheats.json.bz2";
    }
    namespace Directories {
        constexpr const char* SaveBackupsDir = BASE_CKPT_DIR "/saves";
        constexpr const char* ExtdataBackupsDir = BASE_CKPT_DIR "/extdata";
        constexpr const char* WifiSlotBackupsDir = BASE_CKPT_DIR "/wifi";
    }
    namespace Formats {
        constexpr const char* CheatPath = "sdmc:/cheats/%s.txt";
    }
    constexpr size_t BUFFER_SIZE = 0x50000;
}

#undef BASE_CKPT_DIR

#endif
