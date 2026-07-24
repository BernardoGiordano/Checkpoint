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

#ifndef SCRIPTCONSOLE_C_H
#define SCRIPTCONSOLE_C_H

#include <stdio.h>

/* The C face of ScriptConsole, for picoc's stdlib and platform layer (both C).
 * Everything a script prints goes through one of these two, so the log pane,
 * the app log and the run's Outcome all see the same bytes. */

#ifdef __cplusplus
extern "C" {
#endif

/* Raw output. `len` is a byte count, not a terminator-delimited length. */
void ckpt_console_write(const char* data, int len);

/* An unbuffered stream whose writes land in the console. Script printf() and
 * friends print here instead of to the real stdout, which is nowhere on either
 * console. Never NULL: degrades to stdout if the stream cannot be created. */
FILE* ckpt_console_stdout(void);

#ifdef __cplusplus
}
#endif

#endif
