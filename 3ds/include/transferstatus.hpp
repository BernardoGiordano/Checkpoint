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

#ifndef TRANSFERSTATUS_HPP
#define TRANSFERSTATUS_HPP

#include <3ds.h>
#include <string>

// Whether the active transfer is an on-device file copy (the blocking
// backup/restore loop) or a network send/receive. The UI renders a different
// modal for each.
enum class TransferKind { Local, Network };

// A flat, immutable copy of the transfer progress, handed to the UI so it never
// touches the live state (which the HTTP server thread mutates) directly. Local
// copies fill in the file counters; network transfers fill in the byte counters.
struct TransferSnapshot {
    bool active       = false;
    TransferKind kind = TransferKind::Local;
    std::string mode;

    // Local copy: file-by-file copy, run on the TransferJob worker thread.
    std::u16string currentFile;
    u32 currentFileSize   = 0;
    u32 currentFileOffset = 0;
    size_t copyCount      = 0; // files done within the current save
    size_t copyTotal      = 0; // files in the current save
    size_t saveCount      = 0; // saves done within the batch
    size_t saveTotal      = 0; // saves in the batch (1 for a single backup/restore)

    // Network: HTTP send/receive byte counters.
    u64 bytesDone  = 0;
    u64 bytesTotal = 0;
};

// The single owner of "a transfer is in progress" state, replacing the loose
// globals that used to live in main.hpp. All access is mutex-guarded: the local
// copy figures are written by the TransferJob worker thread while the UI thread
// reads them; the byte counters and mode string are written by the HTTP server
// thread (and the sender). Unsynchronised access would be undefined behaviour,
// and 64-bit reads tear on the 3DS's 32-bit core.
namespace TransferStatus {
    // Local copy lifecycle. The batch (one or more saves) is framed by the
    // TransferJob: beginLocalBatch raises the modal and owns the active flag;
    // setSaveCount advances the per-save bar before each save. Within a save,
    // UiProgressSink drives beginLocalRun (per-save file run) and the file
    // figures. end() lowers the modal once the whole batch is done.
    void beginLocalBatch(size_t totalSaves);
    void setSaveCount(size_t count);
    void beginLocalRun(const std::string& mode, size_t totalFiles);
    void startFile(const std::u16string& name, u32 size);
    void setFileOffset(u32 offset);
    void finishFile();

    // Network lifecycle, driven by the HTTP server thread and the sender.
    // beginNetwork starts a transfer of `totalBytes` labelled `mode`.
    void beginNetwork(const std::string& mode, u64 totalBytes);
    void setMode(const std::string& mode);
    void setBytes(u64 done, u64 total);
    void setBytesDone(u64 done);
    void addBytesDone(u64 delta);

    // Ends the run (success or failure); clears the active flag.
    void end();

    // Thread-safe flat copy for the UI to render from.
    TransferSnapshot snapshot();

    // Formats a "done / total" byte pair in MB for the transfer progress UI.
    std::string bytesToMB(u64 done, u64 total);
}

#endif // TRANSFERSTATUS_HPP
