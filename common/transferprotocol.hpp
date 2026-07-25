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

#ifndef TRANSFERPROTOCOL_HPP
#define TRANSFERPROTOCOL_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Wire-protocol core shared by the 3DS and Switch transfer implementations and
// the chlink PC CLI. Everything here is pure byte/string logic — no filesystem,
// no sockets, no platform types — so a fix to the framing, CRC, path-safety or
// auth rules lands once and applies to both consoles. The per-target
// transfer.cpp files own the file+socket IO and call into these.
namespace TransferProto {
    // Zip central-directory record accumulated while a store-only archive is
    // streamed out; consumed when the central directory is written.
    struct ZipEntry {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
        bool isDirectory;
    };

    // CRC32 with the zip/zlib polynomial (0xEDB88320). Hardware intrinsic on
    // ARMv8 (Switch), slicing-by-8 software table elsewhere (3DS ARM11); both
    // produce identical checksums. The software table self-initializes on first
    // use, so callers never have to prime it.
    uint32_t updateCrc(uint32_t crc, const uint8_t* data, size_t len);

    // Append a little-endian integer to a byte buffer (zip fields are LE).
    void appendLe16(std::string& out, uint16_t v);
    void appendLe32(std::string& out, uint32_t v);

    // One file queued for the send path. Paths are UTF-8; the 3DS adapter
    // converts `absPath` to UTF-16 when it opens the file, so the shared code
    // never sees a platform string type.
    struct SendFile {
        std::string absPath;
        std::string relPath;
        uint32_t size;
    };

    // Largest size a store-only zip can frame. There are no zip64 records in the
    // store-only archives the console senders emit, so a single entry or the
    // whole stream exceeding 0xFFFFFFFF cannot be represented — the send path
    // must refuse rather than let the size wrap to u32 and desync framing.
    static constexpr uint64_t kZipMaxSize = 0xFFFFFFFFull;

    // Exact byte size of the store-only zip that sendZipStream emits, used as the
    // streamed HTTP Content-Length. Store-only entries make this deterministic.
    // Computed in u64 and returns nullopt when the total exceeds kZipMaxSize, so
    // an oversized backup fails cleanly instead of wrapping (mirrors the refusal
    // in tools/chlink/zipwriter.go). Each SendFile.size must already be within
    // kZipMaxSize — the adapter's file collector enforces the per-entry bound.
    std::optional<uint64_t> zipStreamSize(const std::vector<SendFile>& files, const std::vector<std::string>& dirs);

    // Rejects a zip entry name that is absolute, contains a backslash or colon,
    // or traverses out of the destination via a ".." component. Applied to every
    // received name before it touches the filesystem.
    bool isSafeZipRelativePath(const std::string& relPath);

    // Turns the announced name of a single-file (non-zip) payload into a bare
    // file name safe to create under the destination folder: directory
    // components and the FAT-forbidden characters go, everything else — the
    // extension above all — survives. A received "00000001.sav" must keep its
    // dot, or the restore path, which opens exactly that name, cannot find it.
    // Returns "" when nothing usable is left; the caller picks a fallback name.
    std::string sanitizeFileName(const std::string& name);

    // Extracts an HTTP header value by key from a raw header block ("" if absent).
    std::string headerValue(const std::string& headers, const std::string& key);

    // Timing-safe compare so a wrong PIN/token can't be narrowed by measuring how
    // far the comparison ran before failing.
    bool constantTimeEquals(const std::string& a, const std::string& b);

    // Parses "ip:port"; nullopt on a missing colon, empty ip, or a port outside
    // [1, 65535].
    struct HostPort {
        std::string ip;
        uint16_t port;
    };
    std::optional<HostPort> parseTarget(const std::string& ipPort);

    // True iff `pin` is exactly 4 ASCII digits (the receiver token format).
    bool validPin(const std::string& pin);

    // ---- IO seam ----------------------------------------------------------
    // The framing algorithms below are pure protocol; every filesystem and
    // socket touch goes through one of these interfaces, implemented once per
    // target (FSStream/u16string on 3DS, FILE*/string on Switch) and — for
    // tests — against in-memory buffers.

    // Sequential byte source over the received multipart body. The adapter has
    // already positioned it at the file part's start; extractZip owns the
    // length accounting, so read() may return fewer bytes at EOF/error.
    struct ByteReader {
        virtual ~ByteReader()                    = default;
        virtual size_t read(void* dst, size_t n) = 0;
    };

    // Destination for extracted zip entries, addressed by UTF-8 relative path
    // under a destination root the adapter already holds. beginFile opens the
    // next file (creating parent directories); writeFile/endFile stream it.
    struct ExtractSink {
        virtual ~ExtractSink()                                                = default;
        virtual bool makeDir(const std::string& relPath)                      = 0;
        virtual bool beginFile(const std::string& relPath, uint32_t sizeHint) = 0;
        virtual bool writeFile(const void* data, size_t n)                    = 0;
        virtual void endFile()                                                = 0;
    };

    // Source of a backup's files while streaming the send zip, addressed by the
    // UTF-8 absolute path stored on each SendFile.
    struct FileReader {
        virtual ~FileReader()                         = default;
        virtual bool open(const std::string& absPath) = 0;
        virtual size_t read(void* dst, size_t n)      = 0;
        virtual void close()                          = 0;
    };

    // Byte sink for the send socket; the poll-gated sendAll lives in the adapter.
    struct ByteSink {
        virtual ~ByteSink()                                = default;
        virtual bool sendAll(const void* data, size_t len) = 0;
    };

    // Polled between chunks/files: return true to abort the transfer.
    using CancelFn = std::function<bool()>;
    // Called with each chunk's byte count so the UI can advance the progress bar.
    using ProgressFn = std::function<void(size_t)>;

    // ---- protocol algorithms over the seam --------------------------------

    // Locate the meta part (returned in outMeta) and the file part's byte range
    // within a multipart body, given a head window the adapter already read.
    // Assumes the layout the console senders emit: meta part first, then a
    // single file part that is the last part before the closing delimiter.
    bool parseMultipart(const std::string& head, uint64_t bodyLen, const std::string& boundary, std::string& outMeta, uint64_t& outFileOffset,
        uint64_t& outFileLen, std::string& outError);

    // Extract a store-only zip streamed through `in`, bounded to `limit` bytes.
    // Verifies each entry's CRC against the stored value (data descriptor or
    // local header). Returns false + outError on corruption, IO failure, or when
    // `cancelled()` fires. `onBytes` reports progress per data chunk.
    bool extractZip(ByteReader& in, uint64_t limit, ExtractSink& sink, const CancelFn& cancelled, const ProgressFn& onBytes, std::string& outError);

    // Stream the store-only zip for `files` + `dirs` into `out`, reading file
    // data through `src`. Total byte count matches zipStreamSize exactly, so the
    // caller's Content-Length holds. Sets `wasCancelled` and returns false if
    // `cancelled()` fired mid-stream; returns false without it on IO/send error.
    bool sendZipStream(ByteSink& out, const std::vector<SendFile>& files, const std::vector<std::string>& dirs, FileReader& src,
        const CancelFn& cancelled, const ProgressFn& onBytes, bool& wasCancelled);
}

#endif
