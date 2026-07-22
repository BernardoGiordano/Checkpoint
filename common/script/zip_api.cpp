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

// zip_dir / unzip: the store-only zip bindings scripts use to package a backup
// folder into one file before uploading it (and to unpack a downloaded one).
// Both wrap the exact TransferProto framing the wireless transfer and tools/chlink
// interoperate with — same CRC and path-safety guarantees — but over plain stdio
// seams, so one copy serves 3DS and Switch. The file bytes stream through these
// FILE* seams and never enter the interpreter heap, so a multi-MB save is fine on
// picoc's ~32 KB heap. Scripts already open bare SD paths ("/3ds/..." / "/switch/...")
// with fopen/opendir on both consoles, so no path prefixing is needed here.

#include "transferprotocol.hpp"
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    // hold-B abort: the script kill switch the per-statement hook can't reach
    // while a long zip/unzip runs, so the framing loops poll it between chunks.
    bool scriptCancelled(void)
    {
        return ckpt_script_abort_requested() != 0;
    }

    // ---- stdio-backed TransferProto seams --------------------------------

    struct FileSink : TransferProto::ByteSink {
        FILE* f;
        explicit FileSink(FILE* file) : f(file) {}
        bool sendAll(const void* data, size_t len) override { return fwrite(data, 1, len, f) == len; }
    };

    struct StdFileReader : TransferProto::FileReader {
        FILE* f = nullptr;
        bool open(const std::string& absPath) override
        {
            f = fopen(absPath.c_str(), "rb");
            return f != nullptr;
        }
        size_t read(void* dst, size_t n) override { return f != nullptr ? fread(dst, 1, n, f) : 0; }
        void close() override
        {
            if (f != nullptr) {
                fclose(f);
                f = nullptr;
            }
        }
    };

    struct StdByteReader : TransferProto::ByteReader {
        FILE* f;
        explicit StdByteReader(FILE* file) : f(file) {}
        size_t read(void* dst, size_t n) override { return f != nullptr ? fread(dst, 1, n, f) : 0; }
    };

    // Writes extracted entries under `root`, creating parent directories as it
    // goes. mkdir on an existing path fails harmlessly; the fopen is the real
    // success check.
    struct DirExtractSink : TransferProto::ExtractSink {
        std::string root; // always ends with '/'
        FILE* out = nullptr;
        explicit DirExtractSink(std::string r) : root(std::move(r))
        {
            if (root.empty() || root.back() != '/') {
                root += '/';
            }
        }
        ~DirExtractSink() override
        {
            if (out != nullptr) {
                fclose(out);
            }
        }
        void ensureParents(const std::string& relPath)
        {
            std::string current = root;
            size_t start        = 0;
            for (size_t pos = relPath.find('/', start); pos != std::string::npos; pos = relPath.find('/', start)) {
                current += relPath.substr(start, pos - start);
                mkdir(current.c_str(), 0777);
                current += '/';
                start = pos + 1;
            }
        }
        bool makeDir(const std::string& relPath) override
        {
            ensureParents(relPath);
            std::string full = root + relPath;
            if (!full.empty() && full.back() == '/') {
                full.pop_back();
            }
            mkdir(full.c_str(), 0777);
            return true;
        }
        bool beginFile(const std::string& relPath, uint32_t) override
        {
            ensureParents(relPath);
            out = fopen((root + relPath).c_str(), "wb");
            return out != nullptr;
        }
        bool writeFile(const void* data, size_t n) override { return out != nullptr && fwrite(data, 1, n, out) == n; }
        void endFile() override
        {
            if (out != nullptr) {
                fclose(out);
                out = nullptr;
            }
        }
    };

    // Recursively gather files (with '/'-separated relative paths) and the dir
    // list sendZipStream needs. Returns false if any regular file exceeds the
    // store-only entry limit or carries an unsafe relative path — the whole zip
    // must refuse rather than emit a truncated/traversing entry.
    bool collect(const std::string& root, const std::string& sub, std::vector<TransferProto::SendFile>& files, std::vector<std::string>& dirs)
    {
        std::string current = root;
        if (!current.empty() && current.back() != '/') {
            current += '/';
        }
        current += sub;
        DIR* d = opendir(current.c_str());
        if (d == nullptr) {
            return true; // unreadable/empty dir contributes nothing
        }
        bool ok = true;
        while (struct dirent* ent = readdir(d)) {
            const std::string name = ent->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            const std::string full = current + name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                const std::string nextSub = sub + name + "/";
                dirs.push_back(nextSub);
                if (!collect(root, nextSub, files, dirs)) {
                    ok = false;
                }
            }
            else {
                if ((uint64_t)st.st_size > TransferProto::kZipMaxSize) {
                    ok = false;
                    continue;
                }
                TransferProto::SendFile entry;
                entry.absPath = full;
                entry.relPath = sub + name;
                entry.size    = (uint32_t)st.st_size;
                if (!TransferProto::isSafeZipRelativePath(entry.relPath)) {
                    ok = false;
                    continue;
                }
                files.push_back(entry);
            }
        }
        closedir(d);
        return ok;
    }

    int zipDir(const char* srcDir, const char* outZipPath)
    {
        std::vector<TransferProto::SendFile> files;
        std::vector<std::string> dirs;
        if (!collect(srcDir, "", files, dirs)) {
            return -1;
        }
        if (!TransferProto::zipStreamSize(files, dirs)) {
            return -1; // exceeds the 4 GB store-only ceiling (no save comes close)
        }

        FILE* f = fopen(outZipPath, "wb");
        if (f == nullptr) {
            return -1;
        }
        FileSink sink(f);
        StdFileReader reader;
        bool wasCancelled = false;
        const bool ok     = TransferProto::sendZipStream(sink, files, dirs, reader, scriptCancelled, nullptr, wasCancelled);
        fclose(f);
        if (!ok) {
            remove(outZipPath);
            return wasCancelled ? -2 : -1;
        }
        return 0;
    }

    int unzipTo(const char* zipPath, const char* outDir)
    {
        FILE* f = fopen(zipPath, "rb");
        if (f == nullptr) {
            return -1;
        }
        fseek(f, 0, SEEK_END);
        const long len = ftell(f);
        rewind(f);
        if (len < 0) {
            fclose(f);
            return -1;
        }
        StdByteReader reader(f);
        DirExtractSink sink(outDir);
        std::string err;
        const bool ok = TransferProto::extractZip(reader, (uint64_t)len, sink, scriptCancelled, nullptr, err);
        fclose(f);
        return ok ? 0 : -1;
    }
}

void ckpt_zip_dir(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    (void)Parser;
    (void)NumArgs;
    ReturnValue->Val->Integer = zipDir((char*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer);
}

void ckpt_unzip(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    (void)Parser;
    (void)NumArgs;
    ReturnValue->Val->Integer = unzipTo((char*)Param[0]->Val->Pointer, (char*)Param[1]->Val->Pointer);
}
