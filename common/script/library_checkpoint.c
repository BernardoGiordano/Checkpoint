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
    // malloc'd (NUL-terminated for convenience) and stays NULL/0 on failure
    // (-3 = out of memory, -4 = short read, i.e. the file could not be read
    // whole — never a partial buffer). sav_list returns full paths,
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
    // failure (-1 = curl unavailable, -2 = the response did not fit in memory,
    // -(CURLcode+100) = transfer error); the out buffer is malloc'd and
    // NUL-terminated, out/outSize are NULL/0 on failure. -2 is the one worth
    // retrying with a smaller request. net_ip returns "0.0.0.0" with no network.
    { ckpt_net_ip,             "char* net_ip(void);" },
    { ckpt_web_get,            "int web_get(char** out, int* outSize, char* url);" },
    // General HTTP for scripts that need methods/headers/bodies web_get can't do
    // (e.g. OAuth + Drive REST). method reaches curl verbatim, so beyond
    // "GET"/"POST"/"PUT"/"PATCH"/"DELETE" a server's own verb works too — the
    // webdav script needs "MKCOL" and "PROPFIND";
    // headers is "\n"-separated "Key: Value" lines ("" for none); body/bodySize
    // is the request body ("" / 0 for none, small form/JSON payloads only). out
    // is the malloc'd NUL-terminated response body (NULL/0 on failure) and
    // respHeaders the malloc'd raw response header block ("" if none) — pass a
    // valid char** for both; the script frees them. Returns the HTTP status, or
    // negative like web_get (-1 = curl unavailable, -2 = out of memory,
    // -(CURLcode+100) = transfer).
    { ckpt_web_request,        "int web_request(char* method, char* url, char* headers, char* body, int bodySize, char** out, int* outSize, char** respHeaders);" },
    // Like web_request but the request body is a file's bytes streamed straight
    // from SD (never through the interpreter heap), for a multi-MB upload such as
    // the Google Drive resumable PUT. method/headers/out/outSize/respHeaders/return
    // match web_request, plus -3 = filePath could not be opened or sized (the
    // file, not the network). While it runs it drives the innermost progress
    // bar itself (bytes sent), and hold-B aborts it.
    { ckpt_web_upload_file,    "int web_upload_file(char* method, char* url, char* headers, char* filePath, char** out, int* outSize, char** respHeaders);" },
    // Percent-encode a string for form bodies / query params (malloc'd).
    { ckpt_url_encode,         "char* url_encode(char* s);" },
    // Value of one header key in a raw response header block ("" if absent),
    // e.g. the resumable-upload Location. Case-sensitive key match.
    { ckpt_http_header_value,  "char* http_header_value(char* headers, char* key);" },
    // Store-only zip of a directory tree into one file on SD (0 ok, <0 error:
    // -2 = cancelled via hold-B). unzip is the inverse. Both reuse the same
    // TransferProto zip framing (CRC + path-safety) the wireless transfer uses,
    // over stdio so one copy serves both consoles; the bytes never enter the
    // interpreter heap. zipName entries carry '/'-separated relative paths.
    { ckpt_zip_dir,            "int zip_dir(char* srcDir, char* outZipPath);" },
    { ckpt_unzip,              "int unzip(char* zipPath, char* outDir);" },
    // Sealed storage for a credential a script has to keep between runs (an
    // OAuth refresh token, an API key). device_seal encrypts with AES-256-GCM
    // under a key mixed from material only this console's services can answer
    // for — which is never written to the SD card — plus, when passphrase is not
    // "", a PBKDF2 stretch of that passphrase. The blob is binary and carries
    // its own salt/nonce/tag: write it to SD as-is and hand it back verbatim.
    //
    // What that is worth, exactly: an SD card read on a PC, an SD image, or a
    // config folder the user shares carries nothing usable, and the blob does
    // not travel to another console. It is NOT protection from other homebrew on
    // the same console — nothing on either console isolates homebrew, and
    // Checkpoint is open source, so the console-bound half is reproducible by
    // anyone who reads common/script/seal_api.cpp. Only the passphrase half is a
    // real boundary. Never tell a user otherwise.
    //
    // Both return 0 on success, or: -1 = not a sealed blob / nothing to seal,
    // -2 = blob from a newer Checkpoint, -3 = out of memory, -4 = no console key
    // source and no passphrase either, -5 = wrong passphrase, wrong console or
    // tampered blob, -6 = the console would not produce a salt/nonce. out is
    // malloc'd (the script frees it) and stays NULL/0 on failure — a failed
    // unseal never yields plaintext, so garbage can't be mistaken for config.
    { ckpt_device_seal,        "int device_seal(char* plain, int plainSize, char* passphrase, char** out, int* outSize);" },
    { ckpt_device_unseal,      "int device_unseal(char* blob, int blobSize, char* passphrase, char** out, int* outSize);" },
    // 1 if unsealing this blob needs a passphrase, 0 if not, -1 if it is not a
    // sealed blob: what to ask the user for before calling device_unseal.
    { ckpt_seal_needs_passphrase, "int seal_needs_passphrase(char* blob, int blobSize);" },
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
    // Progress bars: the log pane's companion. None of these block — a copy
    // loop can report every chunk without ever waiting for a frame. Layers nest,
    // outermost layer 0 (up to 3). progress_begin resets the
    // layer to 0/total and drops every deeper layer, so beginning the next outer
    // item cannot leave a stale inner bar behind. total <= 0 means "unknown":
    // the bar renders indeterminate and shows the raw count. Long native calls
    // (web_upload_file, zip_dir, unzip) drive an extra innermost bar themselves.
    { ckpt_progress_begin,     "void progress_begin(int layer, char* label, int total);" },
    { ckpt_progress_set,       "void progress_set(int layer, int done);" },
    { ckpt_progress_label,     "void progress_label(int layer, char* label);" },
    { ckpt_progress_end,       "void progress_end(int layer);" },
    // What that innermost bar says while no native call is running: the stage
    // the script is in ("preparing", "creating the folder"). Setting it also
    // puts the bar on screen, so the row is labelled from the first stage
    // rather than appearing blank when a zip starts.
    { ckpt_progress_note,      "void progress_note(char* text);" },
    { ckpt_progress_clear,     "void progress_clear(void);" },
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
    // One line to both the app log (kept for bug reports) and the live console
    // pane the user is watching. printf() reaches the pane only.
    { ckpt_script_log,         "void script_log(char* msg);" },
    { ckpt_selected_title,     "char* selected_title(void);" },
    // The app's SD root ("/3ds/Checkpoint" on 3DS, "sdmc:/switch/Checkpoint" on
    // Switch — each in the form that platform's script-side fopen/stat wants), so
    // one cross-platform script can build its config/temp paths without spelling a
    // per-console prefix. malloc'd; the script frees it.
    { ckpt_app_root,           "char* app_root(void);" },
    { NULL, NULL }
};
// clang-format on

void PlatformLibraryInit(Picoc* pc)
{
    IncludeRegister(pc, "checkpoint.h", &CheckpointSetupFunc, &CheckpointFunctions[0],
        "struct directory { int count; char** files; };"
        "struct JSON { void* dummy; };");
}
