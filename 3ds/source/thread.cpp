/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62
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
#include "DataMutex.hpp"
#include "SmallVector.hpp"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <vector>

namespace {
    constexpr int MIN_HANDLES = 2;
    Thread reaperThread;
    // Exit event, "update your list" event, and threads themselves
    DataMutex<std::pair<SmallVector<Thread, Threads::MAX_THREADS>, SmallVector<Handle, MIN_HANDLES + Threads::MAX_THREADS>>> threads;

    void reapThread(void* arg)
    {
        while (true) {
            s32 signaledHandle;
            {
                u32 size;
                const Handle* handles;
                // Letting the lock expire is fine because the only thing that could happen between
                // then and the use is adding a handle, which won't change anything since we only
                // use the original size
                // In an ideal world, svcWaitSynchronizationN would atomically release and regain
                // the lock, but we can't have nice things
                {
                    auto lockedThreadData                            = threads.lock();
                    const auto& [lockedThreads, reaperThreadHandles] = *lockedThreadData;
                    size                                             = lockedThreads.size();
                    handles                                          = reaperThreadHandles.data();
                }
                svcWaitSynchronizationN(&signaledHandle, handles, MIN_HANDLES + size, false, U64_MAX);
            }
            switch (signaledHandle) {
                case 0: {
                    auto lockedThreads = threads.lock();
                    for (size_t i = 0; i < lockedThreads->first.size(); i++) {
                        svcWaitSynchronization(lockedThreads->second[MIN_HANDLES + i], U64_MAX);
                        threadFree(lockedThreads->first[i]);
                    }
                    return;
                }
                case 1:
                    continue;
                default: {
                    auto lockedThreads = threads.lock();
                    threadFree(lockedThreads->first[signaledHandle - 2]);
                    lockedThreads->first.erase(lockedThreads->first.begin() + signaledHandle - 2);
                    lockedThreads->second.erase(lockedThreads->second.begin() + signaledHandle);
                } break;
            }
        }
    }

    struct Task {
        void (*entrypoint)(void*);
        void* arg;
    };

    DataMutex<std::vector<Task>> workerTasks;
    LightSemaphore moreTasks;
    std::atomic<u8> numWorkers  = 0;
    std::atomic<u8> freeWorkers = 0;
    u8 maxWorkers               = 0;
    u8 minWorkers               = 0;

    void taskWorkerThread()
    {
        numWorkers++;
        while (true) {
            if (LightSemaphore_TryAcquire(&moreTasks, 1)) {
                if (numWorkers <= minWorkers) {
                    freeWorkers++;
                    LightSemaphore_Acquire(&moreTasks, 1);
                    freeWorkers--;
                }
                else {
                    break;
                }
            }

            Task t = std::invoke([] {
                auto tasks = workerTasks.lock();
                if (tasks->size() == 0) {
                    return Task{nullptr, nullptr};
                }
                else {
                    Task ret = tasks.get()[0];
                    tasks->erase(tasks->begin());
                    return ret;
                }
            });

            if (!t.entrypoint) {
                break;
            }

            t.entrypoint(t.arg);
        }
        numWorkers--;
    }
}

bool Threads::init(u8 min, u8 max)
{
    minWorkers         = min;
    maxWorkers         = max;
    auto lockedThreads = threads.lock();
    lockedThreads->second.emplace_back();
    if (R_FAILED(svcCreateEvent(&lockedThreads->second[0], RESET_ONESHOT))) {
        return false;
    }
    lockedThreads->second.emplace_back();
    if (R_FAILED(svcCreateEvent(&lockedThreads->second[1], RESET_ONESHOT))) {
        return false;
    }
    s32 prio = 0;
    if (R_FAILED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
        return false;
    }
    reaperThread = threadCreate(reapThread, nullptr, 0x400, prio - 3, -2, false);
    if (!reaperThread) {
        return false;
    }

    LightSemaphore_Init(&moreTasks, 0, 10000);
    for (int i = 0; i < minWorkers; i++) {
        if (!Threads::create(WORKER_STACK, taskWorkerThread)) {
            return false;
        }
    }
    return true;
}

bool Threads::create(void (*entrypoint)(void*), void* arg, std::optional<size_t> stackSize)
{
    auto lockedThreads = threads.lock();
    if (lockedThreads->first.size() >= Threads::MAX_THREADS) {
        return false;
    }
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    Thread thread = threadCreate(entrypoint, arg, stackSize.value_or(DEFAULT_STACK), prio - 1, -2, false);

    if (thread) {
        lockedThreads->first.emplace_back(thread);
        lockedThreads->second.emplace_back(threadGetHandle(thread));
        svcSignalEvent(lockedThreads->second[1]);
        return true;
    }

    return false;
}

void Threads::executeTask(void (*task)(void*), void* arg)
{
    workerTasks.lock()->emplace_back(task, arg);
    LightSemaphore_Release(&moreTasks, 1);
    if (numWorkers < maxWorkers && freeWorkers == 0) {
        Threads::create(WORKER_STACK, taskWorkerThread);
    }
}

void Threads::exit(void)
{
    workerTasks.lock()->clear();
    LightSemaphore_Release(&moreTasks, numWorkers);
    svcSignalEvent(threads.lock()->second[0]);
    threadJoin(reaperThread, U64_MAX);
    threadFree(reaperThread);
    svcCloseHandle(threads.lock()->second[0]);
    svcCloseHandle(threads.lock()->second[1]);
}
