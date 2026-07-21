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

#ifndef CHECKPOINT_API_H
#define CHECKPOINT_API_H

#include "picoc.h"

// Declarations of the native bindings scripts reach through <checkpoint.h>.
// Every binding has picoc's fixed signature; the prototypes scripts actually
// see live in the table in library_checkpoint.c, and the implementations are
// per-platform (3ds/source/script/checkpoint_api.cpp).

#ifdef __cplusplus
extern "C" {
#endif

#define CKPT_BINDING(name) void name(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)

/* titles (catalog index space = the Save list) */
CKPT_BINDING(ckpt_titles_count);
CKPT_BINDING(ckpt_title_find);
CKPT_BINDING(ckpt_title_id);
CKPT_BINDING(ckpt_title_name);
CKPT_BINDING(ckpt_title_product_code);
CKPT_BINDING(ckpt_title_is_cart);
CKPT_BINDING(ckpt_title_has_save);
CKPT_BINDING(ckpt_title_has_extdata);
CKPT_BINDING(ckpt_title_backup_path);

/* sd card */
CKPT_BINDING(ckpt_read_directory);
CKPT_BINDING(ckpt_delete_directory);
CKPT_BINDING(ckpt_sd_mkdirs);
CKPT_BINDING(ckpt_sd_exists);

/* save archives (kind: 0=save, 1=extdata; fixed table of 8 handles) */
CKPT_BINDING(ckpt_sav_open);
CKPT_BINDING(ckpt_sav_read);
CKPT_BINDING(ckpt_sav_write);
CKPT_BINDING(ckpt_sav_delete);
CKPT_BINDING(ckpt_sav_list);
CKPT_BINDING(ckpt_sav_commit);
CKPT_BINDING(ckpt_sav_close);

/* network */
CKPT_BINDING(ckpt_net_ip);
CKPT_BINDING(ckpt_web_get);

/* gui (block the script thread on the UI bridge) */
CKPT_BINDING(ckpt_gui_message);
CKPT_BINDING(ckpt_gui_confirm);
CKPT_BINDING(ckpt_gui_pick_one);
CKPT_BINDING(ckpt_gui_pick_many);
CKPT_BINDING(ckpt_gui_keyboard);
CKPT_BINDING(ckpt_gui_status);

/* json (platform-neutral, common/script/json_api.cpp) */
CKPT_BINDING(ckpt_json_new);
CKPT_BINDING(ckpt_json_parse);
CKPT_BINDING(ckpt_json_delete);
CKPT_BINDING(ckpt_json_is_valid);
CKPT_BINDING(ckpt_json_is_int);
CKPT_BINDING(ckpt_json_is_bool);
CKPT_BINDING(ckpt_json_is_string);
CKPT_BINDING(ckpt_json_is_array);
CKPT_BINDING(ckpt_json_is_object);
CKPT_BINDING(ckpt_json_get_int);
CKPT_BINDING(ckpt_json_get_bool);
CKPT_BINDING(ckpt_json_get_string);
CKPT_BINDING(ckpt_json_array_size);
CKPT_BINDING(ckpt_json_array_element);
CKPT_BINDING(ckpt_json_object_contains);
CKPT_BINDING(ckpt_json_object_element);
CKPT_BINDING(ckpt_json_object_key);

/* misc */
CKPT_BINDING(ckpt_script_log);
CKPT_BINDING(ckpt_selected_title);

/* not a binding: ScriptRunner calls this after every run (whatever the exit
 * path — normal return, script exit(), parse error longjmp) so a script that
 * forgot sav_close never leaks an open archive into the next run. */
void ckpt_sav_close_all(void);

/* not bindings: the script kill switch (common/script/scriptabort.c). The
 * main thread requests an abort and picoc's per-statement hook fails the run
 * at the next statement the script executes; long-running bindings (web_get)
 * poll ckpt_script_abort_requested() themselves to bail out early. */
void ckpt_script_abort_request(void);
int ckpt_script_abort_requested(void);
void ckpt_script_abort_reset(void);

/* not a binding (per-platform): the worker calls this once at entry to drop
 * its own scheduler priority just below the main thread. A syscall-free script
 * (e.g. a pure `while (1)` compute loop) never yields its core, so if it ran at
 * or above the main thread's priority it would starve the loop that samples the
 * hold-B abort — the request would never be raised and the kill switch above
 * could never fire. Running one step below main lets the UI thread always
 * preempt long enough to poll input. */
void ckpt_script_lower_priority(void);

#ifdef __cplusplus
}
#endif

#endif
