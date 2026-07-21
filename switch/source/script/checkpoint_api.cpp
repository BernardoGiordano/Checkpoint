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

// Native implementations of the <checkpoint.h> script API, Switch flavour. All
// of these run on the script worker thread. TitleCatalog reads are safe here
// because MainScreen ignores every catalog-restructuring input while a script
// runs (same gate policy as TransferJob), so no writer can race them. gui_*
// park the thread on the ScriptUiBridge. A binding must close every RAII scope
// before calling back into picoc — ProgramFail longjmps to the run's exit
// point, so it is only ever called before C++ objects holding resources exist.
//
// Semantic differences from the 3DS build, dictated by the platform:
// - the catalog index space is the current user's raw title list (account
//   saves plus the BCAT/Device/System singletons appended to it);
// - there is no extdata: kind 1 is rejected by sav_open, title_has_extdata
//   returns 0 and title_backup_path(idx, 1) returns "";
// - saves are directory trees mounted as fsdev devices, not FS archives:
//   each sav handle is its own "scrN:" mount and sav_commit is
//   fsdevCommitDevice, with no secure-value business;
// - product codes and game carts do not apply: title_product_code returns ""
//   and title_is_cart returns 0.

#include "filesystem.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "scriptrunner.hpp"
#include "title.hpp"
#include "titlecatalog.hpp"
#include "util.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <dirent.h>
#include <string>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    // Strings returned to a script are plain malloc'd copies:
    // the interpreter treats them as ordinary char* and they live until the
    // process (or the script) frees them — acceptable for script-sized data.
    void* strToRet(const std::string& str)
    {
        char* ret = (char*)malloc(str.size() + 1);
        if (ret) {
            memcpy(ret, str.c_str(), str.size() + 1);
        }
        return (void*)ret;
    }

    int titleCount(void)
    {
        return (int)TitleCatalog::get().getTitleCount(g_currentUId);
    }

    // Copies the idx-th title of the current user's raw list — the catalog
    // index space every titles_* binding shares. Fails the script on a bad
    // index (longjmp; called before any local C++ object exists in the
    // binding).
    Title titleAt(struct ParseState* Parser, int idx)
    {
        if (idx < 0 || idx >= titleCount()) {
            ProgramFail(Parser, "title index %d out of range", idx);
        }
        Title title;
        TitleCatalog::get().getTitle(title, g_currentUId, (size_t)idx);
        return title;
    }

    std::string idToHex(u64 id)
    {
        return StringUtils::format("%016llX", (unsigned long long)id);
    }

    // mkdir/stat/fopen all accept the "sdmc:"-prefixed POSIX path on Switch;
    // normalize bare "/..." script paths onto it (matching the boot bootstrap).
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
        dirData* ret = (dirData*)malloc(sizeof(dirData));
        if (ret) {
            ret->count = (int)names.size();
            ret->files = names.empty() ? nullptr : (char**)malloc(sizeof(char*) * names.size());
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

    // sav_* handle table. Each open slot is its own fsdev mount ("scr0:" ..
    // "scr7:"), so slots never collide with each other or with the "save:"
    // device the backup/restore worker uses (which cannot run concurrently
    // anyway); ckpt_sav_close_all wipes the table after every run.
    constexpr int MAX_SAV_HANDLES = 8;
    struct SavSlot {
        bool live = false;
        char dev[8]; // "scr0" .. "scr7"
    };
    SavSlot savSlots[MAX_SAV_HANDLES];

    // Fails the script on a stale/invalid handle (longjmp; call it before any
    // local C++ object exists in the binding).
    SavSlot& savAt(struct ParseState* Parser, int h)
    {
        if (h < 0 || h >= MAX_SAV_HANDLES || !savSlots[h].live) {
            ProgramFail(Parser, "invalid save handle %d", h);
        }
        return savSlots[h];
    }

    void savClose(SavSlot& slot)
    {
        if (slot.live) {
            fsdevUnmountDevice(slot.dev);
            slot.live = false;
        }
    }

    // Script paths are archive-absolute ("/file.bin"); build the full stdio
    // path on the slot's device, tolerating a missing leading slash.
    std::string devPath(const SavSlot& slot, const char* path)
    {
        std::string p = path;
        if (p.empty() || p[0] != '/') {
            p = "/" + p;
        }
        return std::string(slot.dev) + ":" + p;
    }
}

/* ---- titles ---------------------------------------------------------- */

void ckpt_titles_count(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = titleCount();
}

void ckpt_title_find(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const u64 id    = strtoull((char*)Param[0]->Val->Pointer, nullptr, 16);
    const int count = titleCount();
    int found       = -1;
    for (int i = 0; i < count && found < 0; i++) {
        Title title;
        TitleCatalog::get().getTitle(title, g_currentUId, (size_t)i);
        if (title.id() == id) {
            found = i;
        }
    }
    ReturnValue->Val->Integer = found;
}

void ckpt_title_id(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(idToHex(title.id()));
}

void ckpt_title_name(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(title.displayName());
}

void ckpt_title_product_code(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Switch titles have no product code; validate the index anyway so a bad
    // one fails identically on both platforms.
    titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet("");
}

void ckpt_title_is_cart(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = 0;
}

void ckpt_title_has_save(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Every catalog entry on Switch *is* an installed save.
    titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = 1;
}

void ckpt_title_has_extdata(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = 0;
}

void ckpt_title_backup_path(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int kind = Param[1]->Val->Integer;
    if (kind != 0 && kind != 1) {
        ProgramFail(Parser, "backup kind %d must be 0 (save) or 1 (extdata)", kind);
    }
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(kind == 0 ? title.path() + "/" : "");
}

/* ---- sd card --------------------------------------------------------- */

void ckpt_read_directory(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const std::string dir = (char*)Param[0]->Val->Pointer;
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
    dirData* dir = (dirData*)Param[0]->Val->Pointer;
    if (dir) {
        for (int i = 0; i < dir->count; i++) {
            free(dir->files[i]);
        }
        free(dir->files);
        free(dir);
    }
}

void ckpt_sd_mkdirs(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const std::string path = sdmcPrefixed((char*)Param[0]->Val->Pointer);

    // mkdir -p: create every component; existing ones fail harmlessly, and the
    // final stat is the actual success check.
    for (size_t pos = path.find('/', strlen("sdmc:/")); pos != std::string::npos; pos = path.find('/', pos + 1)) {
        mkdir(path.substr(0, pos).c_str(), 777);
    }
    mkdir(path.c_str(), 777);

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
    Title title = titleAt(Parser, Param[0]->Val->Integer);

    if (kind != 0) {
        ReturnValue->Val->Integer = -1; // no extdata on Switch
        return;
    }

    int slot = -1;
    for (int i = 0; i < MAX_SAV_HANDLES && slot < 0; i++) {
        if (!savSlots[i].live) {
            slot = i;
        }
    }
    if (slot < 0) {
        ReturnValue->Val->Integer = -2;
        return;
    }

    // Mount the save on the slot's own device name (the SaveDataSource::mount
    // path hardcodes "save:", which belongs to the backup/restore worker).
    FsFileSystem fileSystem;
    Result res = 0;
    switch (title.saveDataType()) {
        case FsSaveDataType_Bcat:
            res = FileSystem::mountBcatSave(&fileSystem, title.id());
            break;
        case FsSaveDataType_Device:
            res = FileSystem::mountDeviceSave(&fileSystem, title.id());
            break;
        case FsSaveDataType_System:
            res = FileSystem::mountSystemSave(&fileSystem, title.id(), title.saveDataSpaceId());
            break;
        default:
            res = FileSystem::mountSave(&fileSystem, title.id(), title.userId());
            break;
    }
    if (R_FAILED(res)) {
        ReturnValue->Val->Integer = (int)res;
        return;
    }

    snprintf(savSlots[slot].dev, sizeof(savSlots[slot].dev), "scr%d", slot);
    if (fsdevMountDevice(savSlots[slot].dev, fileSystem) == -1) {
        fsFsClose(&fileSystem);
        ReturnValue->Val->Integer = -1;
        return;
    }

    savSlots[slot].live       = true;
    ReturnValue->Val->Integer = slot;
}

void ckpt_sav_open_shared(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Shared extdata is a 3DS concept (e.g. the Home Menu Play Coin archive);
    // the Switch has no console-wide extdata, so this is always unsupported.
    ReturnValue->Val->Integer = -1;
}

void ckpt_sav_read(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot = savAt(Parser, Param[0]->Val->Integer);
    char** out    = (char**)Param[2]->Val->Pointer;
    int* outSize  = (int*)Param[3]->Val->Pointer;
    *out          = nullptr;
    *outSize      = 0;

    const std::string path = devPath(slot, (char*)Param[1]->Val->Pointer);
    FILE* f                = fopen(path.c_str(), "rb");
    if (!f) {
        ReturnValue->Val->Integer = -1;
        return;
    }

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    rewind(f);
    if (size < 0) {
        fclose(f);
        ReturnValue->Val->Integer = -1;
        return;
    }

    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        ReturnValue->Val->Integer = -3;
        return;
    }

    const size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(buf);
        ReturnValue->Val->Integer = -1;
        return;
    }

    buf[read]                 = '\0';
    *out                      = buf;
    *outSize                  = (int)read;
    ReturnValue->Val->Integer = 0;
}

void ckpt_sav_write(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot  = savAt(Parser, Param[0]->Val->Integer);
    const int size = Param[3]->Val->Integer;
    if (size < 0) {
        ProgramFail(Parser, "sav_write size must not be negative");
    }

    // Create/replace: drop any existing file first so a shrinking write can't
    // leave the old tail behind (fsdev truncates on "wb", but be explicit and
    // symmetric with the 3DS build).
    const std::string path = devPath(slot, (char*)Param[1]->Val->Pointer);
    remove(path.c_str());

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    const size_t written = size > 0 ? fwrite(Param[2]->Val->Pointer, 1, (size_t)size, f) : 0;
    fclose(f);
    ReturnValue->Val->Integer = written == (size_t)size ? 0 : -3;
}

void ckpt_sav_delete(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot             = savAt(Parser, Param[0]->Val->Integer);
    const std::string path    = devPath(slot, (char*)Param[1]->Val->Pointer);
    ReturnValue->Val->Integer = remove(path.c_str()) == 0 ? 0 : -1;
}

void ckpt_sav_list(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot = savAt(Parser, Param[0]->Val->Integer);

    // Returned entries are archive-absolute like read_directory's; folders get
    // a trailing '/'.
    std::string prefix = (char*)Param[1]->Val->Pointer;
    if (prefix.empty() || prefix[0] != '/') {
        prefix = "/" + prefix;
    }
    if (prefix.back() != '/') {
        prefix += '/';
    }

    DIR* d = opendir((std::string(slot.dev) + ":" + prefix).c_str());
    if (!d) {
        ReturnValue->Val->Pointer = nullptr;
        return;
    }

    std::vector<std::string> names;
    while (struct dirent* ent = readdir(d)) {
        const std::string name = ent->d_name;
        if (name != "." && name != "..") {
            names.push_back(prefix + name + (ent->d_type == DT_DIR ? "/" : ""));
        }
    }
    closedir(d);
    ReturnValue->Val->Pointer = makeDirData(names);
}

void ckpt_sav_commit(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot             = savAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = (int)fsdevCommitDevice(slot.dev);
}

void ckpt_sav_close(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Lenient on purpose: closing an already-closed or bogus handle is a no-op
    // so cleanup paths in scripts can close unconditionally.
    const int h = Param[0]->Val->Integer;
    if (h >= 0 && h < MAX_SAV_HANDLES) {
        savClose(savSlots[h]);
    }
}

void ckpt_sav_close_all(void)
{
    for (int i = 0; i < MAX_SAV_HANDLES; i++) {
        savClose(savSlots[i]);
    }
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
}

void ckpt_net_ip(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Same convention as the log server / FTP: gethostid() is the LAN address
    // in network byte order, lowest byte first when printed.
    const u32 ip = (u32)gethostid();
    ReturnValue->Val->Pointer =
        strToRet(StringUtils::format("%u.%u.%u.%u", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF)));
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

    char* buf = (char*)malloc(data.size() + 1);
    if (!buf) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    memcpy(buf, data.data(), data.size());
    buf[data.size()]          = '\0';
    *out                      = buf;
    *outSize                  = (int)data.size();
    ReturnValue->Val->Integer = (int)status;
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
    Logging::info("[script] {}", (char*)Param[0]->Val->Pointer);
}

void ckpt_selected_title(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Pointer = strToRet(ScriptRunner::get().selectedTitle());
}

void ckpt_script_lower_priority(void)
{
    // The worker was spawned at the main thread's priority (0x2C); +2 puts it a
    // step below so the UI thread always preempts to sample the hold-B abort,
    // even against a syscall-free compute loop that never yields the core.
    s32 prio = 0;
    if (R_SUCCEEDED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
        svcSetThreadPriority(CUR_THREAD_HANDLE, (u32)(prio + 2));
    }
}
