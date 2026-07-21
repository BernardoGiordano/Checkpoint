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

// Native implementations of the <checkpoint.h> script API, 3DS flavour. All of
// these run on the script worker thread: TitleCatalog reads are safe (recursive
// mutex since arch-review S4), gui_* park the thread on the ScriptUiBridge, and
// nothing here may trigger a catalog refresh. A binding must close every RAII
// scope before calling back into picoc — ProgramFail longjmps to the run's exit
// point, so it is only ever called before C++ objects holding resources exist.

#include "backuptarget.hpp"
#include "common.hpp"
#include "directory.hpp"
#include "fsstream.hpp"
#include "loader.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "scriptrunner.hpp"
#include "title.hpp"
#include "util.hpp"
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
        return TitleCatalog::get().getTitleCount(BackupKind::Save);
    }

    // Copies the idx-th title of the Save list — the catalog index space every
    // titles_* binding shares. Fails the script on a bad index (longjmp; called
    // before any local C++ object exists in the binding).
    Title titleAt(struct ParseState* Parser, int idx)
    {
        if (idx < 0 || idx >= titleCount()) {
            ProgramFail(Parser, "title index %d out of range", idx);
        }
        Title title;
        TitleCatalog::get().getTitle(title, idx, BackupKind::Save);
        return title;
    }

    std::string idToHex(u64 id)
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

    // sav_* handle table. Slots hold the opened archive plus what commit means
    // for it; ckpt_sav_close_all wipes it after every run.
    struct SavSlot {
        ArchiveHandle arch;
        bool commitable = false; // CTR save archive: commit + secure-value fix
        u32 uniqueId    = 0;     // for the secure-value fix
    };
    constexpr int MAX_SAV_HANDLES = 8;
    SavSlot savSlots[MAX_SAV_HANDLES];

    // Fails the script on a stale/invalid handle (longjmp; call it before any
    // local C++ object exists in the binding).
    SavSlot& savAt(struct ParseState* Parser, int h)
    {
        if (h < 0 || h >= MAX_SAV_HANDLES || !savSlots[h].arch) {
            ProgramFail(Parser, "invalid save handle %d", h);
        }
        return savSlots[h];
    }

    // Script paths are archive-absolute; tolerate a missing leading slash.
    std::u16string archivePath(const char* path)
    {
        std::string p = path;
        if (p.empty() || p[0] != '/') {
            p = "/" + p;
        }
        return StringUtils::UTF8toUTF16(p.c_str());
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
        TitleCatalog::get().getTitle(title, i, BackupKind::Save);
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
    ReturnValue->Val->Pointer = strToRet(title.longDescription());
}

void ckpt_title_product_code(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Pointer = strToRet(title.productCode);
}

void ckpt_title_is_cart(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.mediaType() == MEDIATYPE_GAME_CARD ? 1 : 0;
}

void ckpt_title_has_save(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.accessibleSave() ? 1 : 0;
}

void ckpt_title_has_extdata(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    ReturnValue->Val->Integer = title.accessibleExtdata() ? 1 : 0;
}

void ckpt_title_backup_path(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const int kind = Param[1]->Val->Integer;
    if (kind != 0 && kind != 1) {
        ProgramFail(Parser, "backup kind %d must be 0 (save) or 1 (extdata)", kind);
    }
    Title title               = titleAt(Parser, Param[0]->Val->Integer);
    BackupTarget target       = title.backup(kind == 0 ? BackupKind::Save : BackupKind::Extdata);
    ReturnValue->Val->Pointer = strToRet(StringUtils::UTF16toUTF8(target.rootPath()) + "/");
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

    int slot = -1;
    for (int i = 0; i < MAX_SAV_HANDLES && slot < 0; i++) {
        if (!savSlots[i].arch) {
            slot = i;
        }
    }
    if (slot < 0) {
        ReturnValue->Val->Integer = -2;
        return;
    }

    // Only regular CTR saves and extdata are file-level archives; GBA VC
    // (FSPXI raw), DSiWare (TWL FAT) and SPI cart saves are not reachable here.
    if (kind == 0) {
        const bool spiCart = title.mediaType() == MEDIATYPE_GAME_CARD && title.cardType() != CARD_CTR;
        if (!title.accessibleSave() || title.isGBAVC() || title.isDSiWare() || spiCart) {
            ReturnValue->Val->Integer = -1;
            return;
        }
    }
    else if (!title.accessibleExtdata()) {
        ReturnValue->Val->Integer = -1;
        return;
    }

    Result res           = 0;
    ArchiveHandle handle = title.backup(kind == 0 ? BackupKind::Save : BackupKind::Extdata).open(res);
    if (!handle) {
        ReturnValue->Val->Integer = R_FAILED(res) ? (int)res : -1;
        return;
    }

    savSlots[slot].arch       = std::move(handle);
    savSlots[slot].commitable = kind == 0;
    savSlots[slot].uniqueId   = title.uniqueId();
    ReturnValue->Val->Integer = slot;
}

void ckpt_sav_open_shared(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // A shared-extdata archive belongs to the console, not a title, so it is keyed
    // by id instead of a catalog index. The id crosses the boundary as a 16-hex
    // string like a title id (picoc has no reliable 64-bit ints): its low 32 bits
    // are the extdata id, its high 32 the archive magic (0x00048000 for the Home
    // Menu shared extdata that holds Play Coins).
    const u64 id = strtoull((char*)Param[0]->Val->Pointer, nullptr, 16);

    int slot = -1;
    for (int i = 0; i < MAX_SAV_HANDLES && slot < 0; i++) {
        if (!savSlots[i].arch) {
            slot = i;
        }
    }
    if (slot < 0) {
        ReturnValue->Val->Integer = -2;
        return;
    }

    FS_Archive archive;
    const u32 path[3] = {MEDIATYPE_NAND, (u32)id, (u32)(id >> 32)};
    Result res        = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, {PATH_BINARY, 0xC, path});
    if (R_FAILED(res)) {
        ReturnValue->Val->Integer = (int)res;
        return;
    }

    // Shared extdata is a file-level archive like ordinary extdata: it needs no
    // commit and no secure-value fix, so sav_commit is a no-op on this handle.
    savSlots[slot].arch       = ArchiveHandle::fromFs(archive);
    savSlots[slot].commitable = false;
    savSlots[slot].uniqueId   = 0;
    ReturnValue->Val->Integer = slot;
}

void ckpt_sav_read(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot = savAt(Parser, Param[0]->Val->Integer);
    char** out    = (char**)Param[2]->Val->Pointer;
    int* outSize  = (int*)Param[3]->Val->Pointer;
    *out          = nullptr;
    *outSize      = 0;

    FSStream stream(slot.arch.fs(), archivePath((char*)Param[1]->Val->Pointer), FS_OPEN_READ);
    if (!stream.good()) {
        const Result res = stream.result();
        stream.close();
        ReturnValue->Val->Integer = (int)res;
        return;
    }

    const u32 size = stream.size();
    char* buf      = (char*)malloc(size + 1);
    if (!buf) {
        stream.close();
        ReturnValue->Val->Integer = -3;
        return;
    }

    const u32 read   = stream.read(buf, size);
    const Result res = stream.result();
    stream.close();
    if (read != size && R_FAILED(res)) {
        free(buf);
        ReturnValue->Val->Integer = (int)res;
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

    // Create/replace: the create-on-open path keeps an existing file's size, so
    // drop it first (a missing file fails harmlessly).
    const std::u16string path = archivePath((char*)Param[1]->Val->Pointer);
    FSUSER_DeleteFile(slot.arch.fs(), fsMakePath(PATH_UTF16, path.data()));

    FSStream stream(slot.arch.fs(), path, FS_OPEN_WRITE, (u32)size);
    if (!stream.good()) {
        const Result res = stream.result();
        stream.close();
        ReturnValue->Val->Integer = (int)res;
        return;
    }

    const u32 written = stream.write(Param[2]->Val->Pointer, (u32)size);
    const Result res  = stream.result();
    stream.close();
    ReturnValue->Val->Integer = written == (u32)size ? 0 : (R_FAILED(res) ? (int)res : -3);
}

void ckpt_sav_delete(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot             = savAt(Parser, Param[0]->Val->Integer);
    const std::u16string path = archivePath((char*)Param[1]->Val->Pointer);
    ReturnValue->Val->Integer = (int)FSUSER_DeleteFile(slot.arch.fs(), fsMakePath(PATH_UTF16, path.data()));
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

    Directory items(slot.arch.fs(), StringUtils::UTF8toUTF16(prefix.c_str()));
    if (!items.good()) {
        ReturnValue->Val->Pointer = nullptr;
        return;
    }

    std::vector<std::string> names;
    for (size_t i = 0, sz = items.size(); i < sz; i++) {
        names.push_back(prefix + StringUtils::UTF16toUTF8(items.entry(i)) + (items.folder(i) ? "/" : ""));
    }
    ReturnValue->Val->Pointer = makeDirData(names);
}

void ckpt_sav_commit(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    SavSlot& slot = savAt(Parser, Param[0]->Val->Integer);
    if (!slot.commitable) {
        ReturnValue->Val->Integer = 0; // extdata needs no commit
        return;
    }

    Result res = FSUSER_ControlArchive(slot.arch.fs(), ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    if (R_SUCCEEDED(res)) {
        // Same epilogue as restore: drop the secure value so the game accepts
        // the modified save instead of flagging a rollback.
        u8 out;
        u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (slot.uniqueId << 8);
        res             = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
    }
    ReturnValue->Val->Integer = (int)res;
}

void ckpt_sav_close(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // Lenient on purpose: closing an already-closed or bogus handle is a no-op
    // so cleanup paths in scripts can close unconditionally.
    const int h = Param[0]->Val->Integer;
    if (h >= 0 && h < MAX_SAV_HANDLES) {
        savSlots[h] = SavSlot{};
    }
}

void ckpt_sav_close_all(void)
{
    for (int i = 0; i < MAX_SAV_HANDLES; i++) {
        savSlots[i] = SavSlot{};
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
    // The thread pool spawns the worker one step ABOVE the main thread (prio-1),
    // so a syscall-free compute loop would starve the UI thread outright; +2
    // lands it just below main, letting the UI thread always preempt to sample
    // the hold-B abort.
    s32 prio = 0;
    if (R_SUCCEEDED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
        svcSetThreadPriority(CUR_THREAD_HANDLE, prio + 2);
    }
}
