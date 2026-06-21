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
#include "gui.hpp"
#include "main.hpp"
#include "util.hpp"

// Renders a single frame so the transfer progress modal keeps refreshing while a
// long, blocking copy is running on the main thread.
static void renderTransferFrame()
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g_screen->drawTop();
    C2D_SceneBegin(g_bottom);
    g_screen->drawBottom();
    Gui::frameEnd();
}

void UiProgressSink::begin(const std::string& mode, size_t totalFiles)
{
    transferSetMode(mode);
    g_transferMode       = mode;
    g_copyCount          = 0;
    g_copyTotal          = totalFiles;
    g_isTransferringFile = true;
}

void UiProgressSink::startFile(const std::u16string& name, u32 size)
{
    g_currentFile       = name;
    g_currentFileSize   = size;
    g_currentFileOffset = 0;
    mFileSize           = size;
    mLastRendered       = -1;
    renderTransferFrame();
}

void UiProgressSink::advanceBytes(u32 offset)
{
    g_currentFileOffset = offset;

    // Throttle to ~64 frames per file: only render when the offset crosses into a
    // new 1/64 bucket. Tiny files (size 0) just render once.
    int bucket = mFileSize == 0 ? 0 : static_cast<int>((static_cast<u64>(offset) * 64) / mFileSize);
    if (bucket != mLastRendered) {
        mLastRendered = bucket;
        renderTransferFrame();
    }
}

void UiProgressSink::finishFile()
{
    g_copyCount++;
}

void UiProgressSink::end()
{
    g_isTransferringFile = false;
}
