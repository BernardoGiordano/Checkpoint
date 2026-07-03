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

#ifndef SORTMODE_HPP
#define SORTMODE_HPP

#include "title.hpp"
#include <array>
#include <string>

// One ordered row per sort_t value (mirrors SaveKind's shape). The single
// place that answers, per mode: its display label (shown by both the grid's
// X-button cycle and the Settings "Default sort" spinner — the two share one
// setting) and its config.json key. Configuration stores the key string, not
// the raw enum, so a future reordering of sort_t doesn't shift anyone's saved
// setting.
struct SortMode {
    sort_t mode;
    const char* label;
    const char* configKey;

    // The four rows in UI order; all()[k].mode == sort_t(k).
    static const std::array<SortMode, SORT_MODES_COUNT>& all();

    // Row for this mode.
    static const SortMode& of(sort_t mode);

    // The next mode in the cycle ((mode + 1) % SORT_MODES_COUNT).
    static sort_t next(sort_t mode);

    // Config (de)serialization. fromConfigKey falls back to SORT_ALPHA for an
    // unrecognized/missing key (e.g. an older config.json).
    static sort_t fromConfigKey(const std::string& key);
};

#endif // SORTMODE_HPP
