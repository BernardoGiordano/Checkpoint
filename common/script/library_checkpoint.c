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

#include "checkpoint_api.h"
#include "interpreter.h"

// picoc calls this once per interpreter to register everything that is not part
// of its own stdlib. Checkpoint's native API is one include, "checkpoint.h": the
// function table below plus the struct definitions scripts need to name.

void CheckpointSetupFunc(Picoc* pc)
{
    (void)pc;
}

// clang-format off
struct LibraryFunction CheckpointFunctions[] =
{
    // titles: referenced by catalog index; ids cross the boundary as
    // 16-hex-uppercase strings (picoc has no reliable 64-bit integers)
    { ckpt_titles_count,       "int titles_count(void);" },
    { ckpt_title_find,         "int title_find(char* idHex);" },
    { ckpt_title_id,           "char* title_id(int idx);" },
    { ckpt_title_name,         "char* title_name(int idx);" },
    { ckpt_title_product_code, "char* title_product_code(int idx);" },
    { ckpt_title_is_cart,      "int title_is_cart(int idx);" },
    { ckpt_title_has_save,     "int title_has_save(int idx);" },
    { ckpt_title_has_extdata,  "int title_has_extdata(int idx);" },
    { ckpt_title_backup_path,  "char* title_backup_path(int idx, int kind);" },
    // save archives (kind: 0=save, 1=extdata). sav_open returns a handle >= 0,
    // -1 for an unsupported title/kind (GBA VC, DSiWare, SPI cart saves),
    // -2 when all 8 handles are taken, or a negative FS Result. Paths are
    // archive-absolute ("/file.bin"). sav_read/sav_write/sav_delete/sav_commit
    // return 0 on success or a negative Result; sav_read's out buffer is
    // malloc'd (NUL-terminated for convenience). sav_list returns full paths,
    // folders with a trailing '/', NULL on error; free with delete_directory.
    // sav_commit also clears the title's secure value (as restore does);
    // it is a no-op on extdata.
    { ckpt_sav_open,           "int sav_open(int titleIdx, int kind);" },
    // Opens a console-wide shared-extdata archive (not owned by any title, so it
    // is keyed by id rather than a catalog index): e.g. the Home Menu shared
    // extdata "00048000F000000B" that holds Play Coins. The id is a 16-hex string
    // like a title id; its low 32 bits are the extdata id, the high 32 the archive
    // magic. Returns a handle usable with the sav_* calls below (-2 = no free
    // handle, negative Result = open failed). commit is a no-op on it.
    { ckpt_sav_open_shared,    "int sav_open_shared(char* extdataIdHex);" },
    { ckpt_sav_read,           "int sav_read(int h, char* path, char** out, int* outSize);" },
    { ckpt_sav_write,          "int sav_write(int h, char* path, char* data, int size);" },
    { ckpt_sav_delete,         "int sav_delete(int h, char* path);" },
    { ckpt_sav_list,           "struct directory* sav_list(int h, char* path);" },
    { ckpt_sav_commit,         "int sav_commit(int h);" },
    { ckpt_sav_close,          "void sav_close(int h);" },
    // network. web_get returns the HTTP status code, or a negative value on
    // failure (-1 = curl unavailable, -(CURLcode+100) = transfer error); the
    // out buffer is malloc'd and NUL-terminated, out/outSize are NULL/0 on
    // failure. net_ip returns "0.0.0.0" with no network.
    { ckpt_net_ip,             "char* net_ip(void);" },
    { ckpt_web_get,            "int web_get(char** out, int* outSize, char* url);" },
    // sd card (plus full picoc stdio: fopen("/3ds/...", ...) works)
    { ckpt_read_directory,     "struct directory* read_directory(char* dir);" },
    { ckpt_delete_directory,   "void delete_directory(struct directory* dir);" },
    { ckpt_sd_mkdirs,          "int sd_mkdirs(char* path);" },
    { ckpt_sd_exists,          "int sd_exists(char* path);" },
    // gui (all block the script until the user answers)
    { ckpt_gui_message,        "void gui_message(char* text);" },
    { ckpt_gui_confirm,        "int gui_confirm(char* text);" },
    { ckpt_gui_pick_one,       "int gui_pick_one(char* prompt, char** items, int count);" },
    { ckpt_gui_pick_many,      "int gui_pick_many(char* prompt, char** items, int count, int* selected);" },
    { ckpt_gui_keyboard,       "void gui_keyboard(char* out, char* hint, int maxChars);" },
    // On-screen numeric keypad constrained to [min, max]: the keyboard itself
    // rejects out-of-range input. Returns the entered value, or -1 if cancelled
    // (so pass a min >= 0 to keep the sentinel unambiguous).
    { ckpt_gui_numpad,         "int gui_numpad(char* prompt, int min, int max);" },
    { ckpt_gui_status,         "void gui_status(char* text);" },
    // json (nlohmann wrappers; struct JSON* is an opaque handle)
    { ckpt_json_new,             "struct JSON* json_new(void);" },
    { ckpt_json_parse,           "void json_parse(struct JSON* obj, char* data);" },
    { ckpt_json_delete,          "void json_delete(struct JSON* obj);" },
    { ckpt_json_is_valid,        "int json_is_valid(struct JSON* obj);" },
    { ckpt_json_is_int,          "int json_is_int(struct JSON* obj);" },
    { ckpt_json_is_bool,         "int json_is_bool(struct JSON* obj);" },
    { ckpt_json_is_string,       "int json_is_string(struct JSON* obj);" },
    { ckpt_json_is_array,        "int json_is_array(struct JSON* obj);" },
    { ckpt_json_is_object,       "int json_is_object(struct JSON* obj);" },
    { ckpt_json_get_int,         "int json_get_int(struct JSON* obj);" },
    { ckpt_json_get_bool,        "int json_get_bool(struct JSON* obj);" },
    { ckpt_json_get_string,      "char* json_get_string(struct JSON* obj);" },
    { ckpt_json_array_size,      "int json_array_size(struct JSON* obj);" },
    { ckpt_json_array_element,   "struct JSON* json_array_element(struct JSON* obj, int index);" },
    { ckpt_json_object_contains, "int json_object_contains(struct JSON* obj, char* name);" },
    { ckpt_json_object_element,  "struct JSON* json_object_element(struct JSON* obj, char* name);" },
    { ckpt_json_object_key,      "char* json_object_key(struct JSON* obj, int index);" },
    // misc
    { ckpt_script_log,         "void script_log(char* msg);" },
    { ckpt_selected_title,     "char* selected_title(void);" },
    { NULL, NULL }
};
// clang-format on

void PlatformLibraryInit(Picoc* pc)
{
    IncludeRegister(pc, "checkpoint.h", &CheckpointSetupFunc, &CheckpointFunctions[0],
        "struct directory { int count; char** files; };"
        "struct JSON { void* dummy; };");
}
