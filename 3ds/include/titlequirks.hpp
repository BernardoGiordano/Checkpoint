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

#ifndef TITLEQUIRKS_HPP
#define TITLEQUIRKS_HPP

#include <3ds.h>

// Per-game special-casing, gathered into one place. These are hardcoded tables
// keyed by title id — system titles Checkpoint should never back up, extdata-id
// remaps that don't follow the usual `lowId >> 8` rule, and the activity-log
// ids.
namespace TitleQuirks {
    // System titles (manuals, browsers, updates, known garbage) that should be
    // excluded from the title list regardless of user configuration.
    bool isSystemExcluded(u64 id);

    // The extdata archive id for a title. Usually `lowId >> 8`, but a handful of
    // games store extdata under a different id; this returns the correct one.
    u32 extdataIdFor(u64 id);
}

#endif // TITLEQUIRKS_HPP
