/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

static std::vector<Thread> threads;

static bool forceRefresh             = false;

void Threads::create(ThreadFunc entrypoint)
{
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    Thread thread = threadCreate((ThreadFunc)entrypoint, NULL, 64 * 1024, prio - 1, -2, false);
    threads.push_back(thread);
}

void Threads::destroy(void)
{
    for (u32 i = 0; i < threads.size(); i++) {
        threadJoin(threads.at(i), U64_MAX);
        threadFree(threads.at(i));
    }
}

void Threads::titles(void)
{
    // don't load titles while they're loading
    if (g_isLoadingTitles) {
        return;
    }

    g_isLoadingTitles = true;
    loadTitles(forceRefresh);
    forceRefresh    = true;
    g_isLoadingTitles = false;
}