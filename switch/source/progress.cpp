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

#include "progress.hpp"
#include "SDLHelper.hpp"
#include "main.hpp"
#include "transferstatus.hpp"

// Renders a single frame so the transfer progress modal keeps refreshing while a
// long, blocking copy is running on the main thread.
static void renderTransferFrame()
{
    g_screen->draw();
    SDLH_Render();
}

void UiProgressSink::begin(const std::string& mode, size_t totalFiles)
{
    TransferStatus::beginLocal(mode, totalFiles);
}

void UiProgressSink::startFile(const std::string& name, u64 size)
{
    TransferStatus::startFile(name, size);
    mFileSize     = size;
    mLastRendered = -1;
    renderTransferFrame();
}

void UiProgressSink::advanceBytes(u64 offset)
{
    TransferStatus::setFileOffset(offset);

    // Throttle to ~64 frames per file: only render when the offset crosses into a
    // new 1/64 bucket. Tiny files (size 0) just render once.
    int bucket = mFileSize == 0 ? 0 : static_cast<int>((offset * 64) / mFileSize);
    if (bucket != mLastRendered) {
        mLastRendered = bucket;
        renderTransferFrame();
    }
}

void UiProgressSink::finishFile()
{
    TransferStatus::finishFile();
}

void UiProgressSink::end()
{
    TransferStatus::end();
}
