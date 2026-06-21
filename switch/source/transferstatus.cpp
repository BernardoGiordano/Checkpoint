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

#include "transferstatus.hpp"
#include <mutex>

namespace {
    std::mutex sMutex;
    TransferSnapshot sState;
}

namespace TransferStatus {
    void beginLocal(const std::string& mode, size_t totalFiles)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sState.active    = true;
        sState.mode      = mode;
        sState.copyCount = 0;
        sState.copyTotal = totalFiles;
        sState.currentFile.clear();
        sState.currentFileSize   = 0;
        sState.currentFileOffset = 0;
    }

    void startFile(const std::string& name, u64 size)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sState.currentFile       = name;
        sState.currentFileSize   = size;
        sState.currentFileOffset = 0;
    }

    void setFileOffset(u64 offset)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sState.currentFileOffset = offset;
    }

    void finishFile()
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sState.copyCount++;
    }

    void end()
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sState.active = false;
    }

    TransferSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(sMutex);
        return sState;
    }
}
