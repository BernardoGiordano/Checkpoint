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

#ifndef SCRIPTBAR_HPP
#define SCRIPTBAR_HPP

#include "scriptconsole.hpp"
#include <string>

// What a progress layer reads as. Only the numbers-to-text half: both targets
// place and paint their own bars, but they must agree on what a bar says, so a
// script author reasons about one contract instead of two.
//
// The rules, in one place because they are the thing that was inconsistent:
//   * an item bar says "3/15", not "20%" — a count is what the script counted,
//     and a percentage of fifteen titles helps nobody;
//   * a byte bar says "42%  1.4 MB/s" — bytes are worth a percentage, and the
//     rate is the only figure that tells the user whether to keep waiting;
//   * an idle bar still says its label, and keeps the counts it reached, so a
//     row never sits on screen as an unexplained empty track.
namespace ScriptBar {
    // Filled part of the track, 0…1. Zero when there is no known total: the
    // caller draws that as an empty track (or its own indeterminate style).
    float fraction(const ScriptConsole::Layer& layer);

    // The right-aligned figure under the track: counts, percentage, rate.
    // Empty when the bar has nothing honest to report yet.
    std::string rightText(const ScriptConsole::Layer& layer);

    // "1.4 MB", "927 KB". Exposed because the log tile formats sizes too.
    std::string bytes(long long n);
}

#endif
