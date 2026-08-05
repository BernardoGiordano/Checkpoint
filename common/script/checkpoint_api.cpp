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

// Native implementations of the <checkpoint.h> script API — one copy for both
// consoles. Everything the two platforms disagree about sits behind ScriptHost
// (scripthost.hpp): the catalog index space and the save archive handle table.
//
// All of these run on the script worker thread: gui_* park it on the
// ScriptUiBridge, and nothing here may trigger a catalog refresh. A binding
// must close every RAII scope before calling back into picoc — ProgramFail
// longjmps to the run's exit point, so it is only ever called before C++
// objects holding resources exist. Arguments therefore come out of ScriptArgs
// (scriptargs.hpp) in one block at the top of each binding, before any local
// that owns anything: every accessor validates, and validation fails the run.

#include "common.hpp"
#include "httpcall.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "scriptargs.hpp"
#include "scriptconsole.hpp"
#include "scriptheap.hpp"
#include "scripthost.hpp"
#include "scriptrunner.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {

    ScriptHost& host(void)
    {
        return ScriptHost::get();
    }

    // The three argument kinds ScriptArgs cannot check on its own, because
    // what makes them valid lives behind ScriptHost. Each fails the script on a
    // bad value (longjmp; called before any local C++ object exists in the
    // binding), naming the argument the way every other check does.

    // A catalog index (ScriptHost::titleCount: the Save list plus the
    // extdata-only titles) — a script gets one from
    // titles_count()/title_find().
    int titleIndexArg(const ScriptArgs& args, int i)
    {
        const int idx   = args.num(i);
        const int count = host().titleCount();
        if (idx < 0 || idx >= count) {
            args.fail("argument %d is title index %d, out of range (%d titles)", i + 1, idx, count);
        }
        return idx;
    }

    HostTitle titleArg(const ScriptArgs& args, int i)
    {
        const int idx = titleIndexArg(args, i);
        HostTitle title;
        host().titleAt(idx, title);
        return title;
    }

    // An open save-archive handle: a stale one (closed, or from a previous run)
    // is as much a script bug as an out-of-range index.
    int savArg(const ScriptArgs& args, int i)
    {
        const int handle = args.num(i);
        if (!host().savValid(handle)) {
            args.fail("argument %d is %d, not an open save handle", i + 1, handle);
        }
        return handle;
    }

    std::string idToHex(uint64_t id)
    {
        return StringUtils::format("%016llX", (unsigned long long)id);
    }

    // mkdir wants an "sdmc:"-prefixed POSIX path (matching the boot bootstrap);
    // stat/fopen accept the bare one.
    std::string sdmcPrefixed(const std::string& path)
    {
        return path.rfind("sdmc:", 0) == 0 ? path : "sdmc:" + path;
    }

    ScriptUiBridge& bridge(void)
    {
        return ScriptRunner::get().bridge();
    }

    // Layout must match the registered "struct directory { int count; char** files; }".
    struct dirData {
        int count;
        char** files;
    };

    dirData* makeDirData(const std::vector<std::string>& names)
    {
        ScriptHeap& heap = ScriptHeap::get();
        dirData* ret     = (dirData*)heap.alloc(sizeof(dirData));
        if (ret) {
            ret->count = (int)names.size();
            ret->files = names.empty() ? nullptr : (char**)heap.alloc(sizeof(char*) * names.size());
            if (ret->files) {
                for (size_t i = 0; i < names.size(); i++) {
                    ret->files[i] = (char*)strToRet(names[i]);
                }
            }
            else {
                ret->count = 0;
            }
        }
        return ret;
    }
}

/* ---- titles ---------------------------------------------------------- */

void ckpt_titles_count(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = host().titleCount();
}

void ckpt_title_find(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "title_find");
    const uint64_t id         = strtoull(args.str(0), nullptr, 16);
    ReturnValue->Val->Integer = host().titleIndexOf(id);
}

void ckpt_title_id(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_id"), 0);
    ReturnValue->Val->Pointer = strToRet(idToHex(title.id));
}

void ckpt_title_name(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_name"), 0);
    ReturnValue->Val->Pointer = strToRet(title.name);
}

void ckpt_title_product_code(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_product_code"), 0);
    ReturnValue->Val->Pointer = strToRet(title.productCode);
}

void ckpt_title_is_cart(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_is_cart"), 0);
    ReturnValue->Val->Integer = title.isCart ? 1 : 0;
}

void ckpt_title_has_save(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_has_save"), 0);
    ReturnValue->Val->Integer = title.hasSave ? 1 : 0;
}

void ckpt_title_has_extdata(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleArg(ScriptArgs(Parser, Param, NumArgs, "title_has_extdata"), 0);
    ReturnValue->Val->Integer = title.hasExtdata ? 1 : 0;
}

void ckpt_title_backup_path(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "title_backup_path");
    const int kind = args.numInRange(1, 0, 1); // 0 = save, 1 = extdata
    const int idx  = titleIndexArg(args, 0);

    // "" stays "": that is how a script is told the platform has no backup of
    // that kind, and "/" would read as the SD root.
    std::string path = host().titleBackupPath(idx, kind);
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    ReturnValue->Val->Pointer = strToRet(path);
}

/* ---- sd card --------------------------------------------------------- */

void ckpt_read_directory(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    std::string dir = ScriptArgs(Parser, Param, NumArgs, "read_directory").str(0);
    // Drop trailing slashes before joining so entries never contain "//": a dir
    // from title_backup_path ends in '/', and "dir//name" opens here (this call)
    // but the FS rejects the empty component when a later opendir/stat (e.g.
    // zip_dir's collect) walks the returned path — the symptom was an empty zip.
    while (dir.size() > 1 && dir.back() == '/') {
        dir.pop_back();
    }
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (struct dirent* ent = readdir(d)) {
            const std::string name = ent->d_name;
            if (name != "." && name != "..") {
                names.push_back(dir + "/" + name);
            }
        }
        closedir(d);
    }

    ReturnValue->Val->Pointer = makeDirData(names);
}

void ckpt_delete_directory(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Through ScriptHeap, not free(): makeDirData allocated these, so the heap
    // still owns them and a raw free() would leave it holding dangling keys to
    // free again at the end of the run.
    //
    // NULL is accepted — sav_list answers NULL on error and a cleanup path
    // should be able to hand that straight back — but anything else must be a
    // struct the heap handed out, because the loop below walks it. Same rule as
    // json_delete: a pointer this heap does not own is a script mistake, and
    // this is the only place that can still say where it was made.
    const ScriptArgs args(Parser, Param, NumArgs, "delete_directory");
    dirData* dir = (dirData*)args.ptrOrNull(0);
    if (dir == nullptr) {
        return;
    }
    ScriptHeap& heap = ScriptHeap::get();
    if (!heap.owns(dir)) {
        args.fail("argument 1 is not a directory from read_directory/sav_list");
    }

    for (int i = 0; i < dir->count; i++) {
        heap.release(dir->files[i]);
    }
    heap.release(dir->files);
    heap.release(dir);
}

void ckpt_sd_mkdirs(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const std::string path = sdmcPrefixed(ScriptArgs(Parser, Param, NumArgs, "sd_mkdirs").str(0));

    // mkdir -p: create every component; existing ones fail harmlessly, and the
    // final stat is the actual success check.
    for (size_t pos = path.find('/', strlen("sdmc:/")); pos != std::string::npos; pos = path.find('/', pos + 1)) {
        mkdir(path.substr(0, pos).c_str(), 0777);
    }
    mkdir(path.c_str(), 0777);

    struct stat st;
    ReturnValue->Val->Integer = (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

void ckpt_sd_exists(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* path = ScriptArgs(Parser, Param, NumArgs, "sd_exists").str(0);
    struct stat st;
    ReturnValue->Val->Integer = stat(path, &st) == 0 ? 1 : 0;
}

/* ---- save archives ----------------------------------------------------- */

void ckpt_sav_open(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_open");
    const int kind            = args.numInRange(1, 0, 1); // 0 = save, 1 = extdata
    const int idx             = titleIndexArg(args, 0);
    ReturnValue->Val->Integer = host().savOpen(idx, kind);
}

void ckpt_sav_open_shared(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // A shared archive belongs to the console, not a title, so it is keyed by
    // id instead of a catalog index. The id crosses the boundary as a 16-hex
    // string like a title id (picoc has no reliable 64-bit ints); what its
    // halves mean is the platform's business.
    const uint64_t id         = strtoull(ScriptArgs(Parser, Param, NumArgs, "sav_open_shared").str(0), nullptr, 16);
    ReturnValue->Val->Integer = host().savOpenShared(id);
}

void ckpt_sav_read(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_read");
    const int handle = savArg(args, 0);
    const char* path = args.str(1);
    char** out       = args.outStr(2);
    int* outSize     = args.outInt(3);
    *out             = nullptr;
    *outSize         = 0;

    ReturnValue->Val->Integer = host().savRead(handle, path, out, outSize);
}

void ckpt_sav_write(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_write");
    const int handle  = savArg(args, 0);
    const char* path  = args.str(1);
    const size_t size = args.byteCount(3);
    // The data pointer only has to be real when there are bytes to read from
    // it; a zero-length write of NULL is a legitimate way to truncate.
    const void* data = size > 0 ? args.ptr(2) : args.ptrOrNull(2);

    ReturnValue->Val->Integer = host().savWrite(handle, path, data, size);
}

void ckpt_sav_delete(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_delete");
    const int handle          = savArg(args, 0);
    ReturnValue->Val->Integer = host().savDelete(handle, args.str(1));
}

void ckpt_sav_mkdir(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_mkdir");
    const int handle          = savArg(args, 0);
    ReturnValue->Val->Integer = host().savMkdir(handle, args.str(1));
}

void ckpt_sav_rmdir(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_rmdir");
    const int handle          = savArg(args, 0);
    ReturnValue->Val->Integer = host().savRmdir(handle, args.str(1));
}

void ckpt_sav_rename(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_rename");
    const int handle          = savArg(args, 0);
    const char* from          = args.str(1);
    ReturnValue->Val->Integer = host().savRename(handle, from, args.str(2));
}

void ckpt_sav_list(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "sav_list");
    const int handle = savArg(args, 0);

    // Returned entries are archive-absolute like read_directory's; folders get
    // a trailing '/'.
    std::string prefix = args.str(1);
    if (prefix.empty() || prefix[0] != '/') {
        prefix = "/" + prefix;
    }
    if (prefix.back() != '/') {
        prefix += '/';
    }

    std::vector<HostDirEntry> entries;
    if (!host().savList(handle, prefix, entries)) {
        ReturnValue->Val->Pointer = nullptr;
        return;
    }

    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const HostDirEntry& entry : entries) {
        names.push_back(prefix + entry.name + (entry.folder ? "/" : ""));
    }
    ReturnValue->Val->Pointer = makeDirData(names);
}

void ckpt_sav_commit(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int handle          = savArg(ScriptArgs(Parser, Param, NumArgs, "sav_commit"), 0);
    ReturnValue->Val->Integer = host().savCommit(handle);
}

void ckpt_sav_close(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Lenient on purpose: closing an already-closed or bogus handle is a no-op
    // so cleanup paths in scripts can close unconditionally — hence num() and
    // not savArg().
    host().savClose(ScriptArgs(Parser, Param, NumArgs, "sav_close").num(0));
}

void ckpt_sav_close_all(void)
{
    host().savCloseAll();
}

/* ---- network ----------------------------------------------------------- */

namespace {
    // Every web_* binding is argument marshalling over Http::perform
    // (httpcall.hpp), which owns the option set, the abort polling, the upload
    // progress bar and the allocation-safe write callback. This turns one
    // response into the script's (return code, out buffer, out size) triple.
    int webResult(const char* what, const char* url, const Http::Response& res, char** out, int* outSize, char** respHeaders)
    {
        switch (res.result) {
            case Http::Result::Ok:
                break;
            case Http::Result::TransferFailed:
                Logging::warning("[script] {} '{}' failed: {}", what, url, Http::strerror(res.code));
                return -((int)res.code + 100);
            case Http::Result::OutOfMemory:
                Logging::warning("[script] {} '{}' ran out of memory for the response", what, url);
                return -2;
            default:
                return -1;
        }

        // Run-scoped copy (scriptheap.hpp), NUL-terminated past the length the
        // script gets: an abort mid-script no longer strands the whole body.
        // A failed copy is the same "it did not fit" the script must retry
        // smaller, so it reports as -2 rather than as "no network stack".
        char* buf = (char*)strToRet(res.body);
        if (!buf) {
            Logging::warning("[script] {} '{}': no room for a {}-byte response body", what, url, res.body.size());
            return -2;
        }
        *out     = buf;
        *outSize = (int)res.body.size();
        if (respHeaders) {
            *respHeaders = (char*)strToRet(res.headers);
        }
        return (int)res.httpStatus;
    }
}

void ckpt_net_ip(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strToRet(getConsoleIP());
}

void ckpt_web_get(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "web_get");
    char** out      = args.outStr(0);
    int* outSize    = args.outInt(1);
    const char* url = args.str(2);
    *out            = nullptr;
    *outSize        = 0;

    Http::Request req;
    req.url = url;

    ReturnValue->Val->Integer = webResult("web_get", url, Http::perform(req), out, outSize, nullptr);
}

void ckpt_web_request(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "web_request");
    const char* method  = args.str(0);
    const char* url     = args.str(1);
    const char* headers = args.strOr(2, "");
    const int bodySize  = (int)args.byteCount(4);
    const char* body    = bodySize > 0 ? args.str(3) : args.strOr(3, "");
    char** out          = args.outStr(5);
    int* outSize        = args.outInt(6);
    // respHeaders stays optional: the prototype asks for a valid char**, but a
    // script that does not care about the response headers may pass NULL.
    char** respHeaders = args.outStrOrNull(7);
    *out               = nullptr;
    *outSize           = 0;
    if (respHeaders) {
        *respHeaders = nullptr;
    }

    Http::Request req;
    req.method         = method; // GET/POST/PUT/PATCH/DELETE
    req.url            = url;
    req.headers        = headers;
    req.body           = body;
    req.bodySize       = bodySize;
    req.captureHeaders = respHeaders != nullptr;

    ReturnValue->Val->Integer = webResult("web_request", url, Http::perform(req), out, outSize, respHeaders);
}

void ckpt_web_upload_file(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "web_upload_file");
    const char* method   = args.str(0);
    const char* url      = args.str(1);
    const char* headers  = args.strOr(2, "");
    const char* filePath = args.str(3);
    char** out           = args.outStr(4);
    int* outSize         = args.outInt(5);
    char** respHeaders   = args.outStrOrNull(6);
    *out                 = nullptr;
    *outSize             = 0;
    if (respHeaders) {
        *respHeaders = nullptr;
    }

    Http::Request req;
    req.method     = method; // "PUT" for the resumable session
    req.url        = url;
    req.headers    = headers;
    req.uploadPath = filePath;
    // A redirect would re-send the body without rewinding the stream.
    req.followRedirects = false;
    req.captureHeaders  = respHeaders != nullptr;

    const Http::Response res = Http::perform(req);
    if (res.result == Http::Result::FileError) {
        Logging::warning("[script] web_upload_file can't open '{}'", filePath);
        ReturnValue->Val->Integer = -3; // the file, not the network: a script can retry a different path
        return;
    }
    ReturnValue->Val->Integer = webResult("web_upload_file", url, res, out, outSize, respHeaders);
}

void ckpt_url_encode(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* s             = ScriptArgs(Parser, Param, NumArgs, "url_encode").str(0);
    ReturnValue->Val->Pointer = strToRet(Http::encode(s));
}

/* ---- gui -------------------------------------------------------------- */

void ckpt_gui_message(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* text = ScriptArgs(Parser, Param, NumArgs, "gui_message").str(0);

    UiRequest req;
    req.kind   = UiRequest::Kind::Message;
    req.prompt = text;
    bridge().request(std::move(req));
}

void ckpt_gui_confirm(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* text = ScriptArgs(Parser, Param, NumArgs, "gui_confirm").str(0);

    UiRequest req;
    req.kind                  = UiRequest::Kind::Confirm;
    req.prompt                = text;
    ReturnValue->Val->Integer = bridge().request(std::move(req)).confirmed ? 1 : 0;
}

void ckpt_gui_pick_one(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "gui_pick_one");
    const char* prompt = args.str(0);
    const int count    = args.count(2);
    char* const* items = args.strArray(1, count);

    UiRequest req;
    req.kind   = UiRequest::Kind::PickOne;
    req.prompt = prompt;
    for (int i = 0; i < count; i++) {
        req.items.push_back(items[i]);
    }
    ReturnValue->Val->Integer = bridge().request(std::move(req)).index;
}

void ckpt_gui_pick_many(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "gui_pick_many");
    const char* prompt = args.str(0);
    const int count    = args.count(2);
    char* const* items = args.strArray(1, count);
    // The same count sizes the selection array: it is both an in and an out
    // parameter, so a short one would be written past as well as read past.
    int* selected = args.intArray(3, count);

    UiRequest req;
    req.kind   = UiRequest::Kind::PickMany;
    req.prompt = prompt;
    for (int i = 0; i < count; i++) {
        req.items.push_back(items[i]);
        req.preselected.push_back(selected[i] != 0);
    }

    UiResponse resp = bridge().request(std::move(req));
    if (resp.confirmed) {
        for (int i = 0; i < count && i < (int)resp.selected.size(); i++) {
            selected[i] = resp.selected[i] ? 1 : 0;
        }
    }
    ReturnValue->Val->Integer = resp.confirmed ? 1 : 0;
}

void ckpt_gui_keyboard(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "gui_keyboard");
    // maxChars is the out buffer's size, terminator included (PKSM semantics),
    // so it is the buffer's length as well as the input limit.
    const int maxChars = args.num(2);
    char* out          = args.outBuf(0, maxChars);
    const char* hint   = args.strOr(1, "");

    UiRequest req;
    req.kind     = UiRequest::Kind::Keyboard;
    req.prompt   = hint;
    req.maxChars = maxChars;

    UiResponse resp = bridge().request(std::move(req));
    snprintf(out, (size_t)maxChars, "%s", resp.text.c_str());
}

void ckpt_gui_numpad(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "gui_numpad");
    const char* prompt = args.str(0);
    const int min      = args.num(1);
    const int max      = args.num(2);
    if (max < min) {
        args.fail("argument 3 is a max of %d, below the min of %d", max, min);
    }

    UiRequest req;
    req.kind                  = UiRequest::Kind::Numpad;
    req.prompt                = prompt;
    req.numMin                = min;
    req.numMax                = max;
    ReturnValue->Val->Integer = bridge().request(std::move(req)).index;
}

void ckpt_gui_status(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    bridge().setStatus(ScriptArgs(Parser, Param, NumArgs, "gui_status").strOr(0, ""));
}

/* ---- misc -------------------------------------------------------------- */

void ckpt_script_log(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* msg = ScriptArgs(Parser, Param, NumArgs, "script_log").strOr(0, "");
    // Both destinations on purpose: the app log survives the run for a bug
    // report, the console pane is what the user is watching while it happens.
    Logging::info("[script] {}", msg);
    ScriptConsole::get().log(msg);
}

void ckpt_selected_title(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strToRet(ScriptRunner::get().selectedTitle());
}

void ckpt_app_root(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strToRet(Paths::checkpointRoot());
}

void ckpt_script_lower_priority(void)
{
    host().lowerPriority();
}
