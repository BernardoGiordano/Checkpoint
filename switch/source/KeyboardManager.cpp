/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#include "KeyboardManager.hpp"
#include "backupsize.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
    // libnx's text-check callback carries no user pointer, so the range lives at
    // file scope. Safe: scripts run one at a time and swkbd blocks the main thread
    // for the whole session, so no two numpad prompts overlap.
    int g_numpadMin = 0;
    int g_numpadMax = 0;

    SwkbdTextCheckResult numpadRangeCheck(char* tmpString, size_t tmpStringSize)
    {
        char* end        = nullptr;
        const long value = strtol(tmpString, &end, 10);
        if (end == tmpString || *end != '\0' || value < g_numpadMin || value > g_numpadMax) {
            snprintf(tmpString, tmpStringSize, "Enter a value between %d and %d.", g_numpadMin, g_numpadMax);
            return SwkbdTextCheckResult_Bad;
        }
        return SwkbdTextCheckResult_OK;
    }
}

KeyboardManager::KeyboardManager(void)
{
    systemKeyboardAvailable = false;
    if (appletGetAppletType() == AppletType_Application) {
        SwkbdConfig kbd;
        res = swkbdCreate(&kbd, 0);
        if (R_SUCCEEDED(res)) {
            systemKeyboardAvailable = true;
            swkbdClose(&kbd);
        }
    }
}

std::pair<bool, std::string> KeyboardManager::keyboard(const std::string& suggestion)
{
    if (systemKeyboardAvailable) {
        // The size-cache walk floods the FS service and delays the swkbd applet
        // launch by the walk's full remaining duration; hold it while the
        // keyboard session runs.
        BackupSizeCache::PauseGuard pauseWalk;
        SwkbdConfig kbd;
        if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
            swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetInitialText(&kbd, suggestion.c_str());
            swkbdConfigSetStringLenMax(&kbd, CUSTOM_PATH_LEN);
            char tmpoutstr[CUSTOM_PATH_LEN] = {0};
            Result res                      = swkbdShow(&kbd, tmpoutstr, CUSTOM_PATH_LEN);
            swkbdClose(&kbd);
            if (R_SUCCEEDED(res)) {
                return std::make_pair(true, std::string(tmpoutstr));
            }
        }
    }
    return std::make_pair(false, suggestion);
}

int KeyboardManager::numpad(const std::string& hint, int min, int max)
{
    if (!systemKeyboardAvailable) {
        return -1;
    }

    // Same PauseGuard rationale as text(): the size-cache walk delays the swkbd
    // applet launch by the walk's full remaining duration.
    BackupSizeCache::PauseGuard pauseWalk;
    SwkbdConfig kbd;
    int result = -1;
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        swkbdConfigSetGuideText(&kbd, hint.c_str());

        int digits = 1;
        for (int m = max; m >= 10; m /= 10) {
            digits++;
        }
        swkbdConfigSetStringLenMax(&kbd, digits);

        g_numpadMin = min;
        g_numpadMax = max;
        swkbdConfigSetTextCheckCallback(&kbd, numpadRangeCheck);

        char out[16] = {0};
        Result rc    = swkbdShow(&kbd, out, sizeof(out));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) {
            result = (int)strtol(out, nullptr, 10);
        }
    }
    return result;
}

std::string KeyboardManager::text(const std::string& suggestion, const std::string& hint, size_t maxLen)
{
    if (!systemKeyboardAvailable || maxLen == 0) {
        return "";
    }

    // Same PauseGuard rationale as keyboard(): the size-cache walk delays the
    // swkbd applet launch by the walk's full remaining duration.
    BackupSizeCache::PauseGuard pauseWalk;
    SwkbdConfig kbd;
    std::string out;
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, hint.c_str());
        swkbdConfigSetInitialText(&kbd, suggestion.c_str());
        swkbdConfigSetStringLenMax(&kbd, maxLen);
        std::vector<char> buf(maxLen * 4 + 1, 0); // UTF-8 worst case per char
        Result rc = swkbdShow(&kbd, buf.data(), buf.size());
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) {
            out = buf.data();
        }
    }
    return out;
}