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

#include "sleepguard.hpp"
#include "logging.hpp"
#include <mutex>
#include <switch.h>

namespace {
    std::mutex sMutex;
    bool sHeld = false;
    // What auto-sleep was set to before the first hold(), so release() puts the
    // console back the way it was found instead of assuming it was enabled.
    bool sPrevDisabled = false;
}

void SleepGuard::hold()
{
    std::lock_guard<std::mutex> lock(sMutex);
    if (sHeld) {
        return;
    }

    bool disabled = false;
    if (R_FAILED(appletIsAutoSleepDisabled(&disabled))) {
        disabled = false;
    }
    sPrevDisabled = disabled;

    const Result res = appletSetAutoSleepDisabled(true);
    if (R_FAILED(res)) {
        // Not fatal: the operation runs anyway, it is just interruptible by the
        // inactivity timer again. Worth a line, since that is a plausible cause
        // of a transfer that "stopped" halfway.
        Logging::warning("Could not hold off auto-sleep with result 0x{:08X}. A long operation may be interrupted by sleep.", (u32)res);
        return;
    }
    sHeld = true;
}

void SleepGuard::release()
{
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sHeld) {
        return;
    }
    sHeld            = false;
    const Result res = appletSetAutoSleepDisabled(sPrevDisabled);
    if (R_FAILED(res)) {
        Logging::warning("Could not restore auto-sleep with result 0x{:08X}.", (u32)res);
    }
}
