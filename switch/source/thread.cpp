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

#include "thread.hpp"
#include "logging.hpp"
#include <switch.h>

namespace {
    Thread gThread;
    bool gThreadLive = false;
    void (*gEntry)(void);

    void thunk(void*)
    {
        gEntry();
    }
}

void Threads::join(void)
{
    if (gThreadLive) {
        threadWaitForExit(&gThread);
        threadClose(&gThread);
        gThreadLive = false;
    }
}

bool Threads::create(std::optional<size_t> stackSize, void (*entrypoint)(void))
{
    // Reap the previous worker before reusing the slot. The sole caller
    // (ScriptRunner) only creates a new thread once the previous run's result
    // was collected, so this wait is at most the tail of a returning thread.
    join();

    // Stack size must be page-aligned or threadCreate fails with
    // LibnxError_BadInput (see the network thread in main.cpp).
    size_t stack = stackSize.value_or(0x8000);
    stack        = (stack + 0xFFF) & ~(size_t)0xFFF;

    gEntry    = entrypoint;
    Result rc = threadCreate(&gThread, thunk, nullptr, nullptr, stack, 0x2C, -2);
    if (R_FAILED(rc)) {
        Logging::error("[thread] threadCreate failed with result 0x{:08X}", rc);
        return false;
    }
    if (R_FAILED(rc = threadStart(&gThread))) {
        Logging::error("[thread] threadStart failed with result 0x{:08X}", rc);
        threadClose(&gThread);
        return false;
    }
    gThreadLive = true;
    return true;
}
