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
#include "transferstatus.hpp"

// UiProgressSink runs on the TransferJob worker thread: every call only mirrors
// figures into TransferStatus. It does not render — the main loop draws the
// transfer modal from TransferStatus::snapshot() every frame, so the UI keeps
// animating while the copy runs on the worker. The batch active flag is owned by
// TransferStatus::beginLocalBatch / end, raised and lowered by the TransferJob
// around the whole batch.
void UiProgressSink::begin(const std::string& mode, size_t totalFiles)
{
    // A backup sink carries a cancel flag; a restore sink does not. That is
    // exactly the "may this run be cancelled?" fact the modal needs.
    TransferStatus::beginLocalRun(mode, totalFiles, mCancelFlag != nullptr);
}

void UiProgressSink::startFile(const std::string& name, u64 size)
{
    TransferStatus::startFile(name, size);
}

void UiProgressSink::advanceBytes(u64 offset)
{
    TransferStatus::setFileOffset(offset);
}

void UiProgressSink::finishFile()
{
    TransferStatus::finishFile();
}

void UiProgressSink::end()
{
    // No-op: the batch active flag is lowered by the TransferJob once the whole
    // batch finishes, not after each save's file run.
}
