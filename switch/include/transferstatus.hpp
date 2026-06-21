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
    std::string mode;
    std::string currentFile;
    u64 currentFileSize   = 0;
    u64 currentFileOffset = 0;
    size_t copyCount      = 0;
    size_t copyTotal      = 0;
};

// The single owner of "a save transfer is in progress" state, replacing the loose
// globals that used to live in main.hpp. All access is mutex-guarded so the UI
// always renders from a consistent snapshot rather than reading half-written
// counters directly.
namespace TransferStatus {
    // Starts a run of `totalFiles` files labelled `mode` ("Backup"/"Restore").
    void beginLocal(const std::string& mode, size_t totalFiles);
    void startFile(const std::string& name, u64 size);
    void setFileOffset(u64 offset);
    void finishFile();

    // Ends the run (success or failure); clears the active flag.
    void end();

    // Thread-safe flat copy for the UI to render from.
    TransferSnapshot snapshot();
}

#endif // TRANSFERSTATUS_HPP
