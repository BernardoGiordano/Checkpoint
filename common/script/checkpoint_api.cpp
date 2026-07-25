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
// objects holding resources exist.

#include "common.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "scriptconsole.hpp"
#include "scriptheap.hpp"
#include "scripthost.hpp"
#include "scriptrunner.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
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

    // Fails the script on a bad index (longjmp; called before any local C++
    // object exists in the binding).
    void checkTitleIndex(struct ParseState* Parser, int idx)
    {
        if (idx < 0 || idx >= host().titleCount()) {
            ProgramFail(Parser, "title index %d out of range", idx);
        }
    }

    HostTitle titleAt(struct ParseState* Parser, int idx)
    {
        checkTitleIndex(Parser, idx);
        HostTitle title;
        host().titleAt(idx, title);
        return title;
    }

    // Same rule as above: fails the script on a stale/invalid handle, so call
    // it before any local C++ object exists in the binding.
    int savAt(struct ParseState* Parser, int handle)
    {
        if (!host().savValid(handle)) {
            ProgramFail(Parser, "invalid save handle %d", handle);
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
    const uint64_t id = strtoull((char*)Param[0]->Val->Pointer, nullptr, 16);
    const int count   = host().titleCount();
    int found         = -1;
    for (int i = 0; i < count && found < 0; i++) {
        HostTitle title;
        if (host().titleAt(i, title) && title.id == id) {
            found = i;
        }
    }
    ReturnValue->Val->Integer = found;
}

void ckpt_title_id(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(idToHex(title.id));
}

void ckpt_title_name(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(title.name);
}

void ckpt_title_product_code(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(title.productCode);
}

void ckpt_title_is_cart(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.isCart ? 1 : 0;
}

void ckpt_title_has_save(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.hasSave ? 1 : 0;
}

void ckpt_title_has_extdata(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    HostTitle title           = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.hasExtdata ? 1 : 0;
}

void ckpt_title_backup_path(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int kind = Param[1]->Val->Integer;
    if (kind != 0 && kind != 1) {
        ProgramFail(Parser, "backup kind %d must be 0 (save) or 1 (extdata)", kind);
    }
    checkTitleIndex(Parser, Param[0]->Val->Integer);

    // "" stays "": that is how a script is told the platform has no backup of
    // that kind, and "/" would read as the SD root.
    std::string path = host().titleBackupPath(Param[0]->Val->Integer, kind);
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    ReturnValue->Val->Pointer = strToRet(path);
}

/* ---- sd card --------------------------------------------------------- */

void ckpt_read_directory(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    std::string dir = (char*)Param[0]->Val->Pointer;
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
    dirData* dir = (dirData*)Param[0]->Val->Pointer;
    if (dir) {
        ScriptHeap& heap = ScriptHeap::get();
        for (int i = 0; i < dir->count; i++) {
            heap.release(dir->files[i]);
        }
        heap.release(dir->files);
        heap.release(dir);
    }
}

void ckpt_sd_mkdirs(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const std::string path = sdmcPrefixed((char*)Param[0]->Val->Pointer);

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
    struct stat st;
    ReturnValue->Val->Integer = stat((char*)Param[0]->Val->Pointer, &st) == 0 ? 1 : 0;
}

/* ---- save archives ----------------------------------------------------- */

void ckpt_sav_open(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int kind = Param[1]->Val->Integer;
    if (kind != 0 && kind != 1) {
        ProgramFail(Parser, "save kind %d must be 0 (save) or 1 (extdata)", kind);
    }
    checkTitleIndex(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = host().savOpen(Param[0]->Val->Integer, kind);
}

void ckpt_sav_open_shared(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // A shared archive belongs to the console, not a title, so it is keyed by
    // id instead of a catalog index. The id crosses the boundary as a 16-hex
    // string like a title id (picoc has no reliable 64-bit ints); what its
    // halves mean is the platform's business.
    const uint64_t id         = strtoull((char*)Param[0]->Val->Pointer, nullptr, 16);
    ReturnValue->Val->Integer = host().savOpenShared(id);
}

void ckpt_sav_read(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int handle = savAt(Parser, Param[0]->Val->Integer);
    char** out       = (char**)Param[2]->Val->Pointer;
    int* outSize     = (int*)Param[3]->Val->Pointer;
    *out             = nullptr;
    *outSize         = 0;

    ReturnValue->Val->Integer = host().savRead(handle, (char*)Param[1]->Val->Pointer, out, outSize);
}

void ckpt_sav_write(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int handle = savAt(Parser, Param[0]->Val->Integer);
    const int size   = Param[3]->Val->Integer;
    if (size < 0) {
        ProgramFail(Parser, "sav_write size must not be negative");
    }
    ReturnValue->Val->Integer = host().savWrite(handle, (char*)Param[1]->Val->Pointer, Param[2]->Val->Pointer, (size_t)size);
}

void ckpt_sav_delete(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int handle          = savAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = host().savDelete(handle, (char*)Param[1]->Val->Pointer);
}

void ckpt_sav_list(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int handle = savAt(Parser, Param[0]->Val->Integer);

    // Returned entries are archive-absolute like read_directory's; folders get
    // a trailing '/'.
    std::string prefix = (char*)Param[1]->Val->Pointer;
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
    const int handle          = savAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = host().savCommit(handle);
}

void ckpt_sav_close(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Lenient on purpose: closing an already-closed or bogus handle is a no-op
    // so cleanup paths in scripts can close unconditionally.
    host().savClose(Param[0]->Val->Integer);
}

void ckpt_sav_close_all(void)
{
    host().savCloseAll();
}

/* ---- network ----------------------------------------------------------- */

namespace {
    size_t curlWriteToString(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        ((std::string*)userdata)->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // Nonzero aborts the transfer: a script being aborted mustn't sit in a
    // download it can't reach the per-statement abort hook from.
    int curlAbortOnScriptCancel(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return ckpt_script_abort_requested();
    }

    // "\n"-separated "Key: Value" lines into a curl slist ("" => none). Empty
    // lines and a trailing newline are skipped.
    struct curl_slist* headerSlist(const char* headers)
    {
        struct curl_slist* hl = nullptr;
        if (headers && headers[0]) {
            std::string all(headers);
            size_t start = 0;
            while (start < all.size()) {
                const size_t nl  = all.find('\n', start);
                std::string line = all.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                if (!line.empty()) {
                    hl = curl_slist_append(hl, line.c_str());
                }
                if (nl == std::string::npos) {
                    break;
                }
                start = nl + 1;
            }
        }
        return hl;
    }

    // web_upload_file progress: drives the console's reserved innermost bar, so
    // a script uploading N files gets a byte-level bar under its own file-level
    // bar without writing any progress code around the call. Also the abort
    // seam — the per-statement kill switch cannot reach a script parked in curl.
    struct UploadProgress {
        int lastPct = -1;
    };
    int curlUploadProgress(void* p, curl_off_t, curl_off_t, curl_off_t ultotal, curl_off_t ulnow)
    {
        if (ckpt_script_abort_requested()) {
            return 1;
        }
        UploadProgress* up = (UploadProgress*)p;
        if (up && ultotal > 0) {
            const int pct = (int)((ulnow * 100) / ultotal);
            if (pct != up->lastPct) {
                if (up->lastPct < 0) {
                    // First callback: the total is only known once curl has it.
                    // The label names the *phase*, never the script's status
                    // text: a script that also drives an item bar would
                    // otherwise show the same string on two stacked bars and
                    // read as a duplicate. "zip" / "unzip" in zip_api.cpp are
                    // the same convention.
                    ScriptConsole::get().beginIo("upload", (long long)ultotal);
                }
                up->lastPct = pct;
                ScriptConsole::get().setIo((long long)ulnow);
            }
        }
        return 0;
    }
}

void ckpt_net_ip(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strToRet(getConsoleIP());
}

void ckpt_web_get(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char** out   = (char**)Param[0]->Val->Pointer;
    int* outSize = (int*)Param[1]->Val->Pointer;
    char* url    = (char*)Param[2]->Val->Pointer;
    *out         = nullptr;
    *outSize     = 0;

    // Lazy so curl costs nothing until a script actually fetches. Single script
    // thread + one run at a time, so no init race.
    static bool curlReady = false;
    if (!curlReady) {
        curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
    CURL* curl = curlReady ? curl_easy_init() : nullptr;
    if (!curl) {
        ReturnValue->Val->Integer = -1;
        return;
    }

    std::string data;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlAbortOnScriptCancel);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Checkpoint-curl");
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 300L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

    const CURLcode code = curl_easy_perform(curl);
    long status         = 0;
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        Logging::warning("[script] web_get '{}' failed: {}", url, curl_easy_strerror(code));
        ReturnValue->Val->Integer = -((int)code + 100);
        return;
    }

    // Run-scoped copy (scriptheap.hpp), NUL-terminated past the length the
    // script gets: an abort mid-script no longer strands the whole body.
    char* buf = (char*)strToRet(data);
    if (!buf) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    *out                      = buf;
    *outSize                  = (int)data.size();
    ReturnValue->Val->Integer = (int)status;
}

void ckpt_web_request(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* method  = (char*)Param[0]->Val->Pointer;
    const char* url     = (char*)Param[1]->Val->Pointer;
    const char* headers = (char*)Param[2]->Val->Pointer;
    const char* body    = (char*)Param[3]->Val->Pointer;
    const int bodySize  = Param[4]->Val->Integer;
    char** out          = (char**)Param[5]->Val->Pointer;
    int* outSize        = (int*)Param[6]->Val->Pointer;
    char** respHeaders  = (char**)Param[7]->Val->Pointer;
    *out                = nullptr;
    *outSize            = 0;
    if (respHeaders) {
        *respHeaders = nullptr;
    }

    static bool curlReady = false;
    if (!curlReady) {
        curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
    CURL* curl = curlReady ? curl_easy_init() : nullptr;
    if (!curl) {
        ReturnValue->Val->Integer = -1;
        return;
    }

    struct curl_slist* hl = headerSlist(headers);
    std::string data, rhdr;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method); // GET/POST/PUT/PATCH/DELETE
    if (hl) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hl);
    }
    if (bodySize > 0 && body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodySize);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rhdr);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlAbortOnScriptCancel);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Checkpoint-curl");
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 300L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

    const CURLcode code = curl_easy_perform(curl);
    long status         = 0;
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    if (hl) {
        curl_slist_free_all(hl);
    }
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        Logging::warning("[script] web_request {} '{}' failed: {}", method, url, curl_easy_strerror(code));
        ReturnValue->Val->Integer = -((int)code + 100);
        return;
    }

    // Run-scoped copy (scriptheap.hpp), NUL-terminated past the length the
    // script gets: an abort mid-script no longer strands the whole body.
    char* buf = (char*)strToRet(data);
    if (!buf) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    *out     = buf;
    *outSize = (int)data.size();
    if (respHeaders) {
        *respHeaders = (char*)strToRet(rhdr);
    }
    ReturnValue->Val->Integer = (int)status;
}

void ckpt_web_upload_file(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* method   = (char*)Param[0]->Val->Pointer;
    const char* url      = (char*)Param[1]->Val->Pointer;
    const char* headers  = (char*)Param[2]->Val->Pointer;
    const char* filePath = (char*)Param[3]->Val->Pointer;
    char** out           = (char**)Param[4]->Val->Pointer;
    int* outSize         = (int*)Param[5]->Val->Pointer;
    char** respHeaders   = (char**)Param[6]->Val->Pointer;
    *out                 = nullptr;
    *outSize             = 0;
    if (respHeaders) {
        *respHeaders = nullptr;
    }

    // The body is the file's bytes, streamed by curl's default fread reader — it
    // never enters the interpreter heap, so a multi-MB save is fine.
    FILE* f = fopen(filePath, "rb");
    if (!f) {
        Logging::warning("[script] web_upload_file can't open '{}'", filePath);
        ReturnValue->Val->Integer = -1;
        return;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    rewind(f);
    if (len < 0) {
        fclose(f);
        ReturnValue->Val->Integer = -1;
        return;
    }

    static bool curlReady = false;
    if (!curlReady) {
        curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
    CURL* curl = curlReady ? curl_easy_init() : nullptr;
    if (!curl) {
        fclose(f);
        ReturnValue->Val->Integer = -1;
        return;
    }

    struct curl_slist* hl = headerSlist(headers);
    UploadProgress prog;
    std::string data, rhdr;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);            // streamed request body
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method); // "PUT" for the resumable session
    if (hl) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hl);
    }
    curl_easy_setopt(curl, CURLOPT_READDATA, f);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlWriteToString);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &rhdr);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlUploadProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Checkpoint-curl");
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 300L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

    const CURLcode code = curl_easy_perform(curl);
    long status         = 0;
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    if (hl) {
        curl_slist_free_all(hl);
    }
    curl_easy_cleanup(curl);
    fclose(f);
    ScriptConsole::get().endIo();

    if (code != CURLE_OK) {
        Logging::warning("[script] web_upload_file {} '{}' failed: {}", method, url, curl_easy_strerror(code));
        ReturnValue->Val->Integer = -((int)code + 100);
        return;
    }

    // Run-scoped copy (scriptheap.hpp), NUL-terminated past the length the
    // script gets: an abort mid-script no longer strands the whole body.
    char* buf = (char*)strToRet(data);
    if (!buf) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    *out     = buf;
    *outSize = (int)data.size();
    if (respHeaders) {
        *respHeaders = (char*)strToRet(rhdr);
    }
    ReturnValue->Val->Integer = (int)status;
}

void ckpt_url_encode(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* s = (char*)Param[0]->Val->Pointer;

    static bool curlReady = false;
    if (!curlReady) {
        curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
    CURL* curl = curlReady ? curl_easy_init() : nullptr;
    if (!curl) {
        ReturnValue->Val->Pointer = strToRet("");
        return;
    }
    char* enc                 = curl_easy_escape(curl, s ? s : "", 0);
    ReturnValue->Val->Pointer = strToRet(enc ? enc : "");
    if (enc) {
        curl_free(enc);
    }
    curl_easy_cleanup(curl);
}

/* ---- gui -------------------------------------------------------------- */

void ckpt_gui_message(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    UiRequest req;
    req.kind   = UiRequest::Kind::Message;
    req.prompt = (char*)Param[0]->Val->Pointer;
    bridge().request(std::move(req));
}

void ckpt_gui_confirm(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    UiRequest req;
    req.kind                  = UiRequest::Kind::Confirm;
    req.prompt                = (char*)Param[0]->Val->Pointer;
    ReturnValue->Val->Integer = bridge().request(std::move(req)).confirmed ? 1 : 0;
}

void ckpt_gui_pick_one(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char** items    = (char**)Param[1]->Val->Pointer;
    const int count = Param[2]->Val->Integer;

    UiRequest req;
    req.kind   = UiRequest::Kind::PickOne;
    req.prompt = (char*)Param[0]->Val->Pointer;
    for (int i = 0; i < count; i++) {
        req.items.push_back(items[i]);
    }
    ReturnValue->Val->Integer = bridge().request(std::move(req)).index;
}

void ckpt_gui_pick_many(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    char** items    = (char**)Param[1]->Val->Pointer;
    const int count = Param[2]->Val->Integer;
    int* selected   = (int*)Param[3]->Val->Pointer;

    UiRequest req;
    req.kind   = UiRequest::Kind::PickMany;
    req.prompt = (char*)Param[0]->Val->Pointer;
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
    char* out          = (char*)Param[0]->Val->Pointer;
    const int maxChars = Param[2]->Val->Integer;
    if (maxChars <= 0) {
        ProgramFail(Parser, "gui_keyboard maxChars must be positive");
    }

    UiRequest req;
    req.kind     = UiRequest::Kind::Keyboard;
    req.prompt   = (char*)Param[1]->Val->Pointer;
    req.maxChars = maxChars;

    // maxChars is the out buffer's size, terminator included (PKSM semantics).
    UiResponse resp = bridge().request(std::move(req));
    snprintf(out, (size_t)maxChars, "%s", resp.text.c_str());
}

void ckpt_gui_numpad(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int min = Param[1]->Val->Integer;
    const int max = Param[2]->Val->Integer;
    if (max < min) {
        ProgramFail(Parser, "gui_numpad max %d is below min %d", max, min);
    }

    UiRequest req;
    req.kind                  = UiRequest::Kind::Numpad;
    req.prompt                = (char*)Param[0]->Val->Pointer;
    req.numMin                = min;
    req.numMax                = max;
    ReturnValue->Val->Integer = bridge().request(std::move(req)).index;
}

void ckpt_gui_status(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    bridge().setStatus((char*)Param[0]->Val->Pointer);
}

/* ---- misc -------------------------------------------------------------- */

void ckpt_script_log(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const char* msg = (const char*)Param[0]->Val->Pointer;
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
