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

#ifndef TITLECACHE_HPP
#define TITLECACHE_HPP

#include "iconstore.hpp"
#include "title.hpp"
#include <3ds.h>
#include <cstddef>

// Serialization of a Title to/from the on-SD title cache. The fixed binary
// layout (field offsets, entry size) is declared exactly once here. encode/decode are the
// inverse of each other and are the only code that knows the byte format.
namespace TitleCache {
    // Size of one serialized title entry, in bytes.
    constexpr std::size_t ENTRY_SIZE = 5341;

    // Writes one entry (ENTRY_SIZE bytes) at dst, including the CTR icon pulled
    // fresh from the title's SMDH. Zero-fills the entry first.
    void encode(u8* dst, Title& title);

    // Reads one entry at src into a fully-formed Title, storing its icon bytes
    // into `icons` under the title's id.
    Title decode(const u8* src, IconStore& icons);

    // Peeks the title id of an entry without decoding the rest (used to dedup the
    // save and extdata caches, which share entries).
    u64 readId(const u8* src);
}

#endif // TITLECACHE_HPP
