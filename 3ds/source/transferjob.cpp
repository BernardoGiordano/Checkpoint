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
#include "backuptarget.hpp"
#include "io.hpp"
#include "progress.hpp"
#include "thread.hpp"
#include "transfer.hpp"
#include "transferstatus.hpp"

void TransferJob::enqueueBackup(Title title, BackupKind kind, std::u16string dstPath, std::string dataType)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push_back(WorkItem{.op = Kind::Backup,
        .title                    = std::move(title),
        .kind                     = kind,
        .path                     = std::move(dstPath),
        .dataType                 = std::move(dataType),
        .successMsg               = "Progress correctly saved to disk.",
        .backupName               = "",
        .ip                       = "",
        .token                    = ""});
}

void TransferJob::enqueueRestore(Title title, BackupKind kind, std::u16string srcPath, std::string dataType, std::string successMsg)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mQueue.push_back(WorkItem{.op = Kind::Restore,
        .title                    = std::move(title),
        .kind                     = kind,
        .path                     = std::move(srcPath),
        .dataType                 = std::move(dataType),
        .successMsg               = std::move(successMsg),
        .backupName               = "",
        .ip                       = "",
        .token                    = ""});
}

void TransferJob::enqueueSend(
    Title title, std::u16string backupPath, std::string backupName, std::string dataType, std::string ip, u16 port, std::string token)
{
    std::lock_guard<std::mutex> lock(mMutex);
    WorkItem item;
    item.op         = Kind::Send;
    item.title      = std::move(title);
    item.path       = std::move(backupPath);
    item.dataType   = std::move(dataType);
    item.successMsg = "Transfer completed.";
    item.backupName = std::move(backupName);
    item.ip         = std::move(ip);
    item.port       = port;
    item.token      = std::move(token);
    mQueue.push_back(std::move(item));
}

void TransferJob::start(void)
{
    if (mState.load() == State::Running) {
        return;
    }

    size_t total;
    bool anyLocal = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        total = mQueue.size();
        for (const auto& item : mQueue) {
            if (item.op != Kind::Send) {
                anyLocal = true;
            }
        }
    }
    if (total == 0) {
        return;
    }

    // Sends drive TransferStatus themselves (beginNetwork inside sendBackup);
    // only local copies use the batch counter / bottom modal.
    if (anyLocal) {
        TransferStatus::beginLocalBatch(total);
    }
    mState.store(State::Running);
    Threads::executeTask(TransferJob::runThread);
}

void TransferJob::runThread(void)
{
    get().run();
}

void TransferJob::run(void)
{
    UiProgressSink sink;
    size_t done = 0;

    // Accumulate the batch outcome in locals; a later item's success must not
    // erase an earlier failure. We keep the first failing item's result and,
    // if nothing failed, the last item's (all-success) result.
    JobResult firstFailure;
    bool haveFailure = false;
    JobResult lastResult;
    size_t failed = 0;
    size_t total  = 0;

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

        JobResult current;
        if (item.op == Kind::Send) {
            Transfer::SendOutcome out = Transfer::sendBackup(item.title, item.path, item.backupName, item.dataType, item.ip, item.port, item.token);
            current                   = JobResult{.isRestore = false,
                                  .ok                        = out.ok,
                                  .res                       = 0,
                                  .stage                     = io::BackupStage::Copy,
                                  .successMsg                = item.successMsg,
                                  .dataType                  = item.dataType,
                                  .send                      = out};
        }
        else {
            TransferStatus::setSaveCount(done);

            BackupTarget target = item.title.backup(item.kind);
            io::IoOutcome out   = item.op == Kind::Restore ? io::restore(target, item.path, sink) : io::backup(target, item.path, sink);

            current = JobResult{.isRestore = item.op == Kind::Restore,
                .ok                        = out.ok,
                .res                       = out.res,
                .stage                     = out.stage,
                .successMsg                = item.successMsg,
                .dataType                  = item.dataType,
                .send                      = std::nullopt};
        }

        total++;
        if (!current.ok) {
            failed++;
            if (!haveFailure) {
                firstFailure = current;
                haveFailure  = true;
            }
        }
        lastResult = current;
        done++;
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mResult        = haveFailure ? firstFailure : lastResult;
        mResult.failed = failed;
        mResult.total  = total;
    }

    TransferStatus::end();
    mState.store(State::Done);
}

std::optional<TransferJob::JobResult> TransferJob::takeResult(void)
{
    if (mState.load() != State::Done) {
        return std::nullopt;
    }

    JobResult result;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        result = mResult;
    }
    mState.store(State::Idle);
    return result;
}
