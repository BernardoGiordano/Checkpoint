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

#ifndef OUTCOMEMESSAGES_HPP
#define OUTCOMEMESSAGES_HPP

#include "io.hpp"
#include "transfer.hpp"
#include <string>

// The single home of the stage-enum → user-facing message mapping. io and
// transfer report *where* an operation failed; the strings live here so every
// screen shows the same words for the same failure. `res` refines the stage
// for the few synthetic results that carry actionable advice (GBA saves).
namespace OutcomeMessages {
    std::string backupError(io::BackupStage stage, const std::string& dataType, Result res);
    std::string restoreError(io::BackupStage stage, const std::string& dataType, Result res);
    std::string sendError(const Transfer::SendOutcome& outcome);
}

#endif
