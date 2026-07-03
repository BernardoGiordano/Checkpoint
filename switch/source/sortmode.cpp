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

#include "sortmode.hpp"

const std::array<SortMode, SORT_MODES_COUNT>& SortMode::all()
{
    static const std::array<SortMode, SORT_MODES_COUNT> modes = {{
        {SORT_ALPHA, "A-Z", "alpha"},
        {SORT_LAST_PLAYED, "Last played", "last-played"},
        {SORT_MOST_BACKUPS, "Most backups", "most-backups"},
        {SORT_FAVORITES_FIRST, "Favorites first", "favorites-first"},
    }};
    return modes;
}

const SortMode& SortMode::of(sort_t mode)
{
    return all()[static_cast<size_t>(mode)];
}

sort_t SortMode::next(sort_t mode)
{
    return static_cast<sort_t>((static_cast<int>(mode) + 1) % SORT_MODES_COUNT);
}

sort_t SortMode::fromConfigKey(const std::string& key)
{
    for (const SortMode& m : all()) {
        if (key == m.configKey) {
            return m.mode;
        }
    }
    return SORT_ALPHA;
}
