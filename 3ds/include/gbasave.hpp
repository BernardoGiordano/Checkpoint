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

#ifndef GBASAVE_HPP
#define GBASAVE_HPP

#include "progress.hpp"
#include <3ds.h>
#include <string>

namespace GbaSave {
    // The container has no initialized slot yet (the game was never saved on
    // this console): there is no header to describe the save, so nothing can
    // be backed up or restored. The UI should suggest launching the game once.
    inline constexpr Result resNoSave = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_FS, RD_NO_DATA);
    // The backup file is neither a bare GBA save of a valid size nor a
    // recognizable AGBSAVE container dump.
    inline constexpr Result resBadBackup = MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_FS, RD_INVALID_SIZE);
    // The backup's save size does not match the title's configured save type.
    inline constexpr Result resSizeMismatch = MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_FS, RD_INVALID_COMBINATION);

    // Exports the newest slot's bare save data into `dstPath` on `sdmc`.
    // Reports through `sink` (startFile/advanceBytes/finishFile; the caller
    // owns begin/end) and honors sink.cancelled() like the other copy loops.
    Result backup(FSPXI_Archive arch, FS_Archive sdmc, const std::u16string& dstPath, ProgressSink& sink);

    // Restores the backup at `srcPath` on `sdmc` (bare save or legacy
    // full-container dump) into the title's container, refreshing the CMAC.
    Result restore(FSPXI_Archive arch, FS_Archive sdmc, const std::u16string& srcPath, ProgressSink& sink);
}

#endif
