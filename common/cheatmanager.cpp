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

#include "cheatmanager.hpp"
#include "platform.hpp"
#include "io.hpp"
#include "fileptr.hpp"
#include "logger.hpp"

#include <bzlib.h>

CheatManager::CheatManager()
{
    mCheats = nullptr;
    IODataHolder iodata;
    iodata.srcPath = Platform::Files::Cheats;
    if (io::fileExists(iodata)) {
        FilePtr in = openFile(Platform::Files::Cheats, "rt");
        if (in) {
            mCheats = std::make_shared<nlohmann::json>(nlohmann::json::parse(in.get(), nullptr, false));
        }
        else {
            Logger::log(Logger::WARN, "Failed to open %s with errno %d.", Platform::Files::Cheats, errno);
        }
    }
    else {
        // load compressed archive in memory
        FilePtr f = openFile(Platform::Files::DefaultCheats, "rb");
        if (f) {
            fseek(f.get(), 0, SEEK_END);
            long size            = ftell(f.get());
            unsigned int destLen = CHEAT_SIZE_DECOMPRESSED;
            auto c = std::make_unique<char[]>(size);
            auto d = std::make_unique<char[]>(destLen);
            rewind(f.get());
            fread(c.get(), 1, size, f.get());

            int r = BZ2_bzBuffToBuffDecompress(d.get(), &destLen, c.get(), size, 0, 0);
            if (r == BZ_OK) {
                mCheats = std::make_shared<nlohmann::json>(nlohmann::json::parse(d.get(), d.get() + destLen));
            }
        }
        else {
            Logger::log(Logger::WARN, "Failed to open %s with errno %d.", Platform::Files::DefaultCheats, errno);
        }
    }
}

bool CheatManager::areCheatsAvailable(const std::string& key)
{
    return mCheats->find(key) != mCheats->end();
}
