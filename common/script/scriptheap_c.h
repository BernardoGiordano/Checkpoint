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

#ifndef SCRIPTHEAP_C_H
#define SCRIPTHEAP_C_H

#include <stddef.h>

/* The C face of ScriptHeap, for picoc's stdlib (C). A script's malloc() and
 * free() are these, so an aborted run reclaims what the script allocated as
 * well as what the bindings handed it — see scriptheap.hpp for why the script's
 * own free() cannot be trusted to run. */

#ifdef __cplusplus
extern "C" {
#endif

void* ckpt_script_malloc(size_t size);
void* ckpt_script_calloc(size_t count, size_t size);
void* ckpt_script_realloc(void* ptr, size_t size);

/* Frees a block this heap owns. A pointer it does not own — a borrowed json
 * element, a double free — is ignored instead of reaching the real free(). */
void ckpt_script_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif
