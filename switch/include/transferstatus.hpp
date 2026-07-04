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

#include <string>
#include <switch.h>

// A flat, immutable copy of the on-device save-copy progress, handed to the UI
// so it never touches the live state directly. Unlike the 3DS build, the switch
// network/FTP path does not report progress through this state, so there is no
// network byte-counter variant here: every transfer is a local file copy.
struct TransferSnapshot {
    bool active = false;
    // Whether this run may be aborted mid-flight (backup yes; restore no — a
    // restore aborted mid-write could leave a truncated save). The UI reads this
    // instead of comparing the mode string.
    bool cancellable = false;
    std::string mode;
    std::string currentFile;
    u64 currentFileSize   = 0;
    u64 currentFileOffset = 0;
    size_t copyCount      = 0; // files done within the current save
    size_t copyTotal      = 0; // files in the current save
    size_t saveCount      = 0; // saves done within the batch
    size_t saveTotal      = 0; // saves in the batch (1 for a single backup/restore)
};

// The single owner of "a save transfer is in progress" state, replacing the loose
// globals that used to live in main.hpp. All access is mutex-guarded: the figures
// are written by the TransferJob worker thread while the UI thread reads them, so
// it always renders from a consistent snapshot rather than half-written counters.
namespace TransferStatus {
    // Local copy lifecycle. The batch (one or more saves) is framed by the
    // TransferJob: beginLocalBatch raises the modal and owns the active flag;
    // setSaveCount advances the per-save bar before each save. Within a save,
    // UiProgressSink drives beginLocalRun (per-save file run) and the file
    // figures. end() lowers the modal once the whole batch is done.
    void beginLocalBatch(size_t totalSaves);
    void setSaveCount(size_t count);
    void beginLocalRun(const std::string& mode, size_t totalFiles, bool cancellable);
    void startFile(const std::string& name, u64 size);
    void setFileOffset(u64 offset);
    void finishFile();

    // Ends the run (success or failure); clears the active flag.
    void end();

    // Thread-safe flat copy for the UI to render from.
    TransferSnapshot snapshot();
}

#endif // TRANSFERSTATUS_HPP
