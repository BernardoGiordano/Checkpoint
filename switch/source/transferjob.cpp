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

#include "transferjob.hpp"
#include "progress.hpp"
#include "transferstatus.hpp"

namespace {
    constexpr size_t WORKER_STACK = 0x10000;
    constexpr int WORKER_PRIO     = 0x2C; // same as the network thread; the copy is IO-bound and yields on fs calls
}

void TransferJob::enqueueBackup(Title title, std::string dstPath, std::string successMsg)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push_back(WorkItem{false, std::move(title), std::move(dstPath), std::move(successMsg)});
}

void TransferJob::enqueueRestore(Title title, std::string srcPath, std::string successMsg)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push_back(WorkItem{true, std::move(title), std::move(srcPath), std::move(successMsg)});
}

void TransferJob::start(void)
{
    if (mState.load() == State::Running) {
        return;
    }

    size_t total;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        total = mQueue.size();
    }
    if (total == 0) {
        return;
    }

    TransferStatus::beginLocalBatch(total);
    mCancelRequested.store(false);
    mState.store(State::Running);

    if (R_FAILED(threadCreate(&mThread, TransferJob::runThread, this, nullptr, WORKER_STACK, WORKER_PRIO, -2)) || R_FAILED(threadStart(&mThread))) {
        // Could not spawn the worker: fall back to running the batch inline so the
        // saves still happen (the UI freezes for this run, as it did before).
        run();
        return;
    }
    mThreadValid = true;
}

void TransferJob::runThread(void* arg)
{
    static_cast<TransferJob*>(arg)->run();
}

void TransferJob::run(void)
{
    size_t done = 0;
    std::vector<u64> refreshIds;
    JobResult last;

    for (;;) {
        WorkItem item;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mQueue.empty()) {
                break;
            }
            item = std::move(mQueue.front());
            mQueue.pop_front();
        }

        TransferStatus::setSaveCount(done);

        // Only a backup item's sink is given the cancel flag, so cancelled() is
        // structurally always false while restoring a save.
        UiProgressSink sink(item.isRestore ? nullptr : &mCancelRequested);
        io::IoOutcome out = item.isRestore ? io::restore(item.title, item.path, sink) : io::backup(item.title, item.path, sink);
        if (out.ok && !item.isRestore) {
            refreshIds.push_back(item.title.id());
        }
        last = JobResult{item.isRestore, out.ok, out.res, out.stage, item.successMsg, {}, out.cancelled};
        done++;

        if (out.cancelled) {
            // Drop the rest of the queue: a cancel ends the whole batch, not just
            // the save that was mid-copy.
            std::lock_guard<std::mutex> lock(mMutex);
            mQueue.clear();
            break;
        }
    }

    last.refreshIds = std::move(refreshIds);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mResult = std::move(last);
    }

    TransferStatus::end();
    mState.store(State::Done);
}

std::optional<TransferJob::JobResult> TransferJob::takeResult(void)
{
    if (mState.load() != State::Done) {
        return std::nullopt;
    }

    if (mThreadValid) {
        threadWaitForExit(&mThread);
        threadClose(&mThread);
        mThreadValid = false;
    }

    JobResult result;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        result = std::move(mResult);
    }
    mState.store(State::Idle);
    return result;
}

void TransferJob::join(void)
{
    if (mThreadValid) {
        threadWaitForExit(&mThread);
        threadClose(&mThread);
        mThreadValid = false;
    }
}
