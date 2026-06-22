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

#ifndef TITLEPROBE_HPP
#define TITLEPROBE_HPP

#include "iconstore.hpp"
#include "title.hpp"
#include <switch.h>

// The live-IO producer of a Title value. From one FsSaveDataInfo it resolves the
// title's name/author (or, for system saves, mounts-tests and labels it), stores
// its icon through the injected IconStore, ensures the on-SD backup directory
// exists, and populates the given Title via its no-IO init.
//
// This is the single place that knows how a Title comes into existence from a
// live save entry: the four Account/BCAT/Device/System paths that loadTitles
// used to spell out as three near-identical copy-paste blocks plus a fourth in
// the system loop now share one body. Symmetric with the 3DS TitleProbe.
namespace TitleProbe {
    // Populate `dst` from `info`. `nsacd` is a caller-owned scratch buffer reused
    // across the scan (the control-data struct is large; allocating it per entry
    // would be wasteful). Returns true when the entry yields a usable Title:
    // false when it is filtered out, has no control data, or (system) won't mount.
    bool probe(Title& dst, const FsSaveDataInfo& info, IconStore& icons, NsApplicationControlData* nsacd);
}

#endif // TITLEPROBE_HPP
