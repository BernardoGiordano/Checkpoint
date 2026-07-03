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

#ifndef PROGRESS_HPP
#define PROGRESS_HPP

#include <atomic>
#include <string>
#include <switch.h>

// Injectable seam for the long, blocking IO in io::backup / io::restore to
// report progress without knowing how (or whether) it is displayed. The IO
// reports honestly through this interface; the adapter decides what to do with
// it. Two adapters keep the seam real: UiProgressSink drives the on-screen
// transfer modal, RecordingProgressSink captures the figures for headless use.
struct ProgressSink {
    virtual ~ProgressSink() = default;

    // Begins a run of `totalFiles` files, labelled `mode` ("Backup"/"Restore").
    virtual void begin(const std::string& mode, size_t totalFiles) = 0;
    // Starts copying a single file of `size` bytes.
    virtual void startFile(const std::string& name, u64 size) = 0;
    // Reports the absolute byte offset reached within the current file.
    virtual void advanceBytes(u64 offset) = 0;
    // Marks the current file complete.
    virtual void finishFile() = 0;
    // Ends the run (success or failure).
    virtual void end() = 0;
    // Polled by the copy loops between files (and between chunks of one big
    // file) to abort early. False by default: only a sink built for a backup
    // item ever reports true, so a restore can never observe a cancel mid-copy.
    virtual bool cancelled() const { return false; }
};

// Real adapter: mirrors progress into the global transfer state read by the
// transfer modal. It only writes TransferStatus and never renders — the copy
// runs on the TransferJob worker thread while the main loop draws the modal from
// the snapshot, so the UI keeps animating throughout.
//
// Optionally built with a pointer to the TransferJob cancel flag: present for a
// backup item, nullptr for a restore item, so cancelled() is structurally always
// false for restore and the shared copy loops never break on it there.
class UiProgressSink : public ProgressSink {
public:
    explicit UiProgressSink(const std::atomic<bool>* cancelFlag = nullptr) : mCancelFlag(cancelFlag) {}

    void begin(const std::string& mode, size_t totalFiles) override;
    void startFile(const std::string& name, u64 size) override;
    void advanceBytes(u64 offset) override;
    void finishFile() override;
    void end() override;
    bool cancelled() const override { return mCancelFlag && mCancelFlag->load(); }

private:
    const std::atomic<bool>* mCancelFlag;
};

// Headless adapter: records the last figures reported, renders nothing. The
// second adapter that makes the seam real and serves as the test surface.
struct RecordingProgressSink : public ProgressSink {
    std::string mode;
    size_t totalFiles = 0;
    size_t filesDone  = 0;
    u64 lastOffset    = 0;
    bool ended        = false;

    void begin(const std::string& m, size_t total) override
    {
        mode       = m;
        totalFiles = total;
    }
    void startFile(const std::string&, u64) override { lastOffset = 0; }
    void advanceBytes(u64 offset) override { lastOffset = offset; }
    void finishFile() override { filesDone++; }
    void end() override { ended = true; }
};

#endif // PROGRESS_HPP
