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

#ifndef TRANSFERJOB_HPP
#define TRANSFERJOB_HPP

#include "io.hpp"
#include "title.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <switch.h>
#include <vector>

// The single owner of "a backup/restore is running on a worker thread". It keeps
// io::backup / io::restore off the main loop so the UI keeps rendering during the
// copy (the loop draws the transfer modal from TransferStatus every frame).
//
// Lifecycle: the main thread resolves each save (Title, path, and the success
// string, including any keyboard prompt) and enqueues it, then calls start(); a
// single libnx worker drains the queue in order, reporting through UiProgressSink
// into TransferStatus. The worker does pure IO only — it never touches the
// TitleCatalog (which has no mutex on the Switch); instead it collects the ids of
// the saves it backed up, and the main thread refreshes their backup-folder lists
// when it picks up the result via takeResult(). enqueue/start run on the main
// thread before the worker starts, so there is no concurrent enqueue. A Meyer's
// singleton, matching TitleCatalog / Configuration.
class TransferJob {
public:
    // The outcome the main loop turns into an Info/Error overlay. Carries the last
    // save's result (single ops are a batch of one) plus the ids the main thread
    // must refresh; io text is built by the caller from `stage`.
    struct JobResult {
        bool isRestore        = false;
        bool ok               = true;
        Result res            = 0;
        io::BackupStage stage = io::BackupStage::Copy;
        std::string successMsg;      // shown on success (already resolved)
        std::vector<u64> refreshIds; // title ids whose backup list the main thread must refresh
        bool cancelled = false;      // set when a backup was aborted via requestCancel(); ok is false, res is 0
    };

    static TransferJob& get(void)
    {
        static TransferJob instance;
        return instance;
    }

    // Owns `title` for the duration of the copy: io::backup/restore take a
    // reference into it, so it must outlive the main-thread local it was copied
    // from. `dstPath`/`srcPath` are already fully resolved.
    void enqueueBackup(Title title, std::string dstPath, std::string successMsg);
    void enqueueRestore(Title title, std::string srcPath, std::string successMsg);

    // Drains the queue on a worker thread, if there is work and none is already
    // running. Idempotent and safe to call with an empty queue.
    void start(void);

    // True while the worker is draining the queue (the main loop renders the modal
    // and ignores input while this holds).
    bool active(void) const { return mState.load() == State::Running; }

    // Backup-only cancel: raises a flag the worker's UiProgressSink polls, but
    // only while it is built for a backup item (nullptr for restore), so a
    // cancel can never leave a half-written save on the console. A cancelled
    // backup deletes its partial folder and drops the rest of the batch.
    void requestCancel(void) { mCancelRequested.store(true); }

    // If the batch finished, joins the worker, returns the last save's outcome and
    // resets to idle; otherwise nullopt.
    std::optional<JobResult> takeResult(void);

    // Blocks until any running batch finishes and the worker is joined. Called at
    // shutdown so the process never tears down services while the copy is live.
    void join(void);

private:
    TransferJob(void) = default;

    static void runThread(void* arg);
    void run(void);

    struct WorkItem {
        bool isRestore;
        Title title;
        std::string path;
        std::string successMsg;
    };

    enum class State { Idle, Running, Done };

    std::deque<WorkItem> mQueue; // filled on the main thread before start(), drained on the worker
    std::mutex mMutex;           // guards mQueue and mResult
    std::atomic<State> mState{State::Idle};
    std::atomic<bool> mCancelRequested{false}; // reset by start(); read through a backup item's UiProgressSink
    JobResult mResult;                         // last save's outcome; published once mState becomes Done
    Thread mThread;
    bool mThreadValid = false;
};

#endif // TRANSFERJOB_HPP
