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

#ifndef MAIN_HPP
#define MAIN_HPP

#include "Screen.hpp"
#include "logging.hpp"
#include <atomic>
#include <citro2d.h>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline std::shared_ptr<Screen> g_screen = nullptr;
inline bool g_bottomScrollEnabled       = false;
inline float g_timer                    = 0;
inline std::string g_selectedCheatKey;
inline std::vector<std::string> g_selectedCheatCodes;
inline std::atomic<bool> g_isLoadingTitles = false;
inline int g_loadingTitlesCounter          = 0;
inline int g_loadingTitlesLimit            = 0;

inline std::u16string g_currentFile;
inline bool g_isTransferringFile = false;
inline size_t g_copyCount        = 0;
inline size_t g_copyTotal        = 0;
inline std::string g_transferMode;
inline u32 g_currentFileOffset = 0;
inline u32 g_currentFileSize   = 0;
inline bool g_transferIsNetwork = false;
inline u64 g_transferBytesDone  = 0;
inline u64 g_transferBytesTotal = 0;

// g_transferMode and the byte counters are written by the HTTP server thread
// (while receiving) and read by the UI thread, so all access must go through
// these helpers. Concurrent access to the std::string is otherwise undefined
// behavior, and 64-bit reads tear on the 3DS's 32-bit core.
inline std::mutex g_transferStatusMutex;

inline void transferSetMode(const std::string& mode)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    g_transferMode = mode;
}

inline std::string transferGetMode(void)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    return g_transferMode;
}

inline void transferSetProgress(u64 done, u64 total)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    g_transferBytesDone  = done;
    g_transferBytesTotal = total;
}

inline void transferSetDone(u64 done)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    g_transferBytesDone = done;
}

inline void transferAddDone(u64 delta)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    g_transferBytesDone += delta;
}

inline void transferGetProgress(u64& done, u64& total)
{
    std::lock_guard<std::mutex> lock(g_transferStatusMutex);
    done  = g_transferBytesDone;
    total = g_transferBytesTotal;
}

// Formats a "done / total" byte pair in MB for the transfer progress UI.
inline std::string transferBytesToMB(u64 done, u64 total)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%.1f / %.1f MB", (double)done / (1024.0 * 1024.0), (double)total / (1024.0 * 1024.0));
    return std::string(buf);
}

#endif
