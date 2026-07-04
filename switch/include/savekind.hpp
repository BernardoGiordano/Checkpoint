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

#ifndef SAVEKIND_HPP
#define SAVEKIND_HPP

#include "title.hpp"
#include <array>
#include <switch.h>

// One ordered row per save kind the UI cycles through, in UI order (the
// saveTypeFilter_t value is the row index). The single place that answers, per
// kind: its filter-button label, its empty-list message, and which
// FsSaveDataType it filters to.
//
// Per-kind *colour* does not exist: every filter button shares the same
// active/inactive colour pair, so SaveKind carries no Color and no SDL
// dependency. SaveDataSource is the IO-side sibling, keyed by FsSaveDataType:
// "how to draw a kind" (SaveKind) vs "how to open a kind" (SaveDataSource).
struct SaveKind {
    saveTypeFilter_t filter;
    const char* buttonLabel; // single glyph shown large on the rail button ("A", "B", "D", "S")
    const char* railLabel;   // small kind name stacked under it ("USER", "BCAT", "DEVICE", "SYSTEM")
    const char* emptyMsg;
    u8 saveDataType;

    // The four rows in UI order; all()[k].filter == saveTypeFilter_t(k).
    static const std::array<SaveKind, 4>& all();

    // Row for this filter.
    static const SaveKind& of(saveTypeFilter_t filter);

    // The next filter in the cycle ((filter + 1) % 4).
    static saveTypeFilter_t next(saveTypeFilter_t filter);
};

#endif // SAVEKIND_HPP
