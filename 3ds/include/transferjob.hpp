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

#include "title.hpp"
#include "transfer.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

// The single owner of "a backup/restore is running on a worker thread". It keeps
// io::backup / io::restore off the main loop so the UI keeps rendering during the
// copy (the loop draws the transfer modal from TransferStatus every frame).
//
// Lifecycle: the main thread resolves each save (Title, kind, path, and the UI
// strings) and enqueues it, then calls start(); a single worker drains the queue
// in order, reporting progress through UiProgressSink into TransferStatus. The
// main loop polls takeResult() each frame and raises the result overlay once the
// whole batch is done. enqueue/start run on the main thread before the worker
// starts; the worker owns the queue while running, so there is no concurrent
// enqueue. A Meyer's singleton, matching TitleCatalog / Configuration.
class TransferJob {
public:
    // The outcome the main loop turns into an Info/Error overlay. Carries the
    // last save's result (single ops are a batch of one); io text is built by the
    // caller from `stage` + `dataType`.
    struct JobResult {
        bool isRestore        = false;
        bool ok               = true;
        Result res            = 0;
        io::BackupStage stage = io::BackupStage::Copy;
        std::string successMsg; // shown on success (already resolved, e.g. the restored name)
        std::string dataType;   // "save" | "extdata", used to build the failure message
        // Set only for network sends; when present the screen maps
        // SendOutcome::stage to a message instead of the io fields above.
        std::optional<Transfer::SendOutcome> send;
    };

    static TransferJob& get(void)
    {
        static TransferJob instance;
        return instance;
    }

    // Owns `title` for the duration of the copy: the BackupTarget the worker
    // builds holds a reference into it, so it must outlive the main-thread local
    // it was copied from. `dstPath`/`srcPath` are already fully resolved.
    void enqueueBackup(Title title, BackupKind kind, std::u16string dstPath, std::string dataType);
    void enqueueRestore(Title title, BackupKind kind, std::u16string srcPath, std::string dataType, std::string successMsg);

    // Network send of an existing backup. Runs Transfer::sendBackup on the
    // worker; progress reaches the top-screen modal via TransferStatus.
    void enqueueSend(
        Title title, std::u16string backupPath, std::string backupName, std::string dataType, std::string ip, u16 port, std::string token);

    // Drains the queue on a worker thread, if there is work and none is already
    // running. Idempotent and safe to call with an empty queue.
    void start(void);

    // True while the worker is draining the queue (the main loop renders the
    // modal and ignores input while this holds).
    bool active(void) const { return mState.load() == State::Running; }

    // If the batch finished, returns the last save's outcome and resets to idle;
    // otherwise nullopt.
    std::optional<JobResult> takeResult(void);

    // Worker entry point (thunk to get().run()).
    static void runThread(void);

private:
    TransferJob(void) = default;

    void run(void);

    enum class Kind { Backup, Restore, Send };

    struct WorkItem {
        Kind op = Kind::Backup;
        Title title;
        BackupKind kind = BackupKind::Save; // unused for Send
        std::u16string path;
        std::string dataType;
        std::string successMsg;
        // Send-only fields
        std::string backupName;
        std::string ip;
        u16 port = 0;
        std::string token;
    };

    enum class State { Idle, Running, Done };

    std::deque<WorkItem> mQueue; // filled on the main thread before start(), drained on the worker
    std::mutex mMutex;           // guards mQueue and mResult
    std::atomic<State> mState{State::Idle};
    JobResult mResult; // last save's outcome; published once mState becomes Done
};

#endif // TRANSFERJOB_HPP
