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

#ifndef SLEEPGUARD_HPP
#define SLEEPGUARD_HPP

// Keeps the console awake across an operation the user is not touching the pad
// during. A backup, a restore or a network transfer can run for minutes with no
// input at all, and the console's inactivity timer does not care that work is in
// flight: it sleeps, which suspends the process mid-copy — on the save
// filesystem, with a journal half committed.
//
// Only auto-sleep is held off. The OS is still free to dim the backlight after
// its usual idle delay, which is the behaviour asked for: the screen goes dark
// and stays readable, it just never turns itself off.
//
// hold() and release() are idempotent and safe from any thread; the state is
// level-triggered, not counted, so overlapping operations (a batch that also
// sends over the network) hold it once and release once. The setting lives in
// the applet session, so it dies with the process even if release() never runs.
namespace SleepGuard {
    void hold();
    void release();
}

#endif // SLEEPGUARD_HPP
