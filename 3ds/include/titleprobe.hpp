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

#include "title.hpp"

// The live-IO producer of a Title value: reads the SMDH / legacy header,
// probes save & extdata accessibility through SaveDataSource, ensures the
// on-SD backup directories exist, and populates the given Title.
//
// Symmetric with TitleCache::decode, the other Title producer (which rebuilds a
// Title from the cache with no IO). Both live outside the Title value; Title no
// longer carries its own filesystem-touching constructor.
namespace TitleProbe {
    // Populate `title` by probing the title `id` on `media`/`card`. Returns true
    // when at least one backup facet (save, raw GBA, or extdata) is accessible
    // and its backup directory could be created.
    bool probe(Title& title, u64 id, FS_MediaType media, FS_CardType card);
}

#endif // TITLEPROBE_HPP
