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

#include "transferprotocol.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>

#if defined(__aarch64__)
#include <arm_acle.h>
#endif

namespace TransferProto {

#if defined(__aarch64__)
    // Hardware CRC32 on ARMv8. devkitA64 builds with -march=armv8-a+crc, so the
    // checksum is effectively free instead of a bytewise table loop.
    uint32_t updateCrc(uint32_t crc, const uint8_t* data, size_t len)
    {
        uint32_t c = crc;
        while (len >= 8) {
            uint64_t v;
            std::memcpy(&v, data, 8);
            c = __crc32d(c, v);
            data += 8;
            len -= 8;
        }
        while (len > 0) {
            c = __crc32b(c, *data++);
            len--;
        }
        return c;
    }
#else
    namespace {
        uint32_t crcTable[8][256];
        bool crcInit = false;

        void initCrc(void)
        {
            if (crcInit) {
                return;
            }
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int j = 0; j < 8; ++j) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                crcTable[0][i] = c;
            }
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = crcTable[0][i];
                for (int t = 1; t < 8; ++t) {
                    c              = crcTable[0][c & 0xFFu] ^ (c >> 8);
                    crcTable[t][i] = c;
                }
            }
            crcInit = true;
        }
    }

    // Slicing-by-8 CRC32: 8 bytes per iteration instead of 1. On the 268MHz
    // ARM11 the bytewise loop is slow enough to rival the SD card, so this
    // keeps the checksum off the transfer's critical path.
    uint32_t updateCrc(uint32_t crc, const uint8_t* data, size_t len)
    {
        initCrc();
        uint32_t c = crc;
        while (len >= 8) {
            uint32_t lo = (uint32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24)) ^ c;
            uint32_t hi = (uint32_t)(data[4] | (data[5] << 8) | (data[6] << 16) | ((uint32_t)data[7] << 24));
            c           = crcTable[7][lo & 0xFFu] ^ crcTable[6][(lo >> 8) & 0xFFu] ^ crcTable[5][(lo >> 16) & 0xFFu] ^ crcTable[4][lo >> 24] ^
                crcTable[3][hi & 0xFFu] ^ crcTable[2][(hi >> 8) & 0xFFu] ^ crcTable[1][(hi >> 16) & 0xFFu] ^ crcTable[0][hi >> 24];
            data += 8;
            len -= 8;
        }
        for (size_t i = 0; i < len; ++i) {
            c = crcTable[0][(c ^ data[i]) & 0xFFu] ^ (c >> 8);
        }
        return c;
    }
#endif

    void appendLe16(std::string& out, uint16_t v)
    {
        out.push_back((char)(v & 0xFF));
        out.push_back((char)((v >> 8) & 0xFF));
    }

    void appendLe32(std::string& out, uint32_t v)
    {
        out.push_back((char)(v & 0xFF));
        out.push_back((char)((v >> 8) & 0xFF));
        out.push_back((char)((v >> 16) & 0xFF));
        out.push_back((char)((v >> 24) & 0xFF));
    }

    bool isSafeZipRelativePath(const std::string& relPath)
    {
        if (relPath.empty()) {
            return false;
        }
        if (relPath.front() == '/' || relPath.front() == '\\') {
            return false;
        }
        if (relPath.find('\\') != std::string::npos) {
            return false;
        }
        if (relPath.find(':') != std::string::npos) {
            return false;
        }

        size_t start = 0;
        while (start <= relPath.size()) {
            size_t pos       = relPath.find('/', start);
            size_t len       = (pos == std::string::npos) ? relPath.size() - start : pos - start;
            std::string part = relPath.substr(start, len);
            if (part == "..") {
                return false;
            }
            if (pos == std::string::npos) {
                break;
            }
            start = pos + 1;
        }

        return true;
    }

    std::string sanitizeFileName(const std::string& name)
    {
        // A payload name never legitimately carries a directory: a backup with
        // subfolders travels as a zip. Keep the basename and drop the rest.
        size_t slash     = name.find_last_of("/\\");
        std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);

        std::string out;
        out.reserve(base.size());
        for (unsigned char c : base) {
            // Control characters and the characters FAT rejects go; '.' and the
            // bytes of a multi-byte UTF-8 sequence stay.
            if (c < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                continue;
            }
            out.push_back((char)c);
        }

        // "." and ".." name directories, not files that can be created.
        if (out == "." || out == "..") {
            return "";
        }
        return out;
    }

    std::string headerValue(const std::string& headers, const std::string& key)
    {
        std::string needle = key + ":";
        size_t pos         = headers.find(needle);
        if (pos == std::string::npos) {
            return "";
        }
        pos += needle.size();
        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
            pos++;
        }
        size_t end = headers.find("\r\n", pos);
        if (end == std::string::npos) {
            end = headers.size();
        }
        return headers.substr(pos, end - pos);
    }

    bool constantTimeEquals(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) {
            return false;
        }
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= (unsigned char)(a[i] ^ b[i]);
        }
        return diff == 0;
    }

    std::optional<HostPort> parseTarget(const std::string& ipPort)
    {
        size_t colon = ipPort.find(':');
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        std::string ip = ipPort.substr(0, colon);
        int port       = atoi(ipPort.substr(colon + 1).c_str());
        if (ip.empty() || port <= 0 || port > 65535) {
            return std::nullopt;
        }
        return HostPort{std::move(ip), (uint16_t)port};
    }

    bool validPin(const std::string& pin)
    {
        return pin.size() == 4 && std::all_of(pin.begin(), pin.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    std::optional<uint64_t> zipStreamSize(const std::vector<SendFile>& files, const std::vector<std::string>& dirs)
    {
        uint64_t total = 22; // end of central directory
        for (const auto& dir : dirs) {
            total += 30 + dir.size(); // local header
            total += 46 + dir.size(); // central directory
        }
        for (const auto& entry : files) {
            total += 30 + entry.relPath.size() + (uint64_t)entry.size + 16; // local header + data + data descriptor
            total += 46 + entry.relPath.size();                             // central directory
        }
        if (total > kZipMaxSize) {
            return std::nullopt;
        }
        return total;
    }

    bool parseMultipart(const std::string& head, uint64_t bodyLen, const std::string& boundary, std::string& outMeta, uint64_t& outFileOffset,
        uint64_t& outFileLen, std::string& outError)
    {
        outMeta.clear();
        outFileOffset = 0;
        outFileLen    = 0;

        std::string boundaryMarker     = "--" + boundary;
        std::string nextBoundaryMarker = "\r\n" + boundaryMarker;

        // Meta part.
        size_t metaPos = head.find(boundaryMarker);
        if (metaPos == std::string::npos) {
            outError = "Missing boundary.";
            return false;
        }
        // File part header.
        size_t filePos = head.find("name=\"file\"");
        if (filePos == std::string::npos) {
            outError = "Incomplete form data.";
            return false;
        }
        // Meta content: between the meta part's header terminator and the boundary
        // that precedes the file part.
        size_t metaHeaderEnd = head.find("\r\n\r\n", metaPos);
        if (metaHeaderEnd == std::string::npos) {
            outError = "Incomplete form data.";
            return false;
        }
        size_t metaDataStart = metaHeaderEnd + 4;
        size_t metaDataEnd   = head.find(nextBoundaryMarker, metaDataStart);
        if (metaDataEnd == std::string::npos || metaDataEnd > filePos) {
            outError = "Incomplete form data.";
            return false;
        }
        outMeta = head.substr(metaDataStart, metaDataEnd - metaDataStart);

        // File data begins right after the file part's header terminator.
        size_t fileHeaderEnd = head.find("\r\n\r\n", filePos);
        if (fileHeaderEnd == std::string::npos) {
            outError = "Incomplete form data.";
            return false;
        }
        uint64_t fileDataStart = (uint64_t)fileHeaderEnd + 4;

        // The body ends with "\r\n--boundary--\r\n"; the file data is everything
        // between fileDataStart and that trailing delimiter.
        std::string trailer = "\r\n" + boundaryMarker + "--\r\n";
        if (bodyLen < fileDataStart + trailer.size()) {
            outError = "Incomplete form data.";
            return false;
        }
        outFileOffset = fileDataStart;
        outFileLen    = bodyLen - fileDataStart - trailer.size();
        return true;
    }

    bool extractZip(ByteReader& in, uint64_t limit, ExtractSink& sink, const CancelFn& cancelled, const ProgressFn& onBytes, std::string& outError)
    {
        uint64_t consumed = 0;
        // Reads through the raw source but never past the file part's limit; the
        // adapter's ByteReader knows nothing about framing, so the bound lives here.
        auto readInto = [&](void* dst, size_t n) -> size_t {
            if (consumed + n > limit) {
                n = (size_t)(limit - consumed);
            }
            if (n == 0) {
                return 0;
            }
            size_t rd = in.read(dst, n);
            consumed += rd;
            return rd;
        };

        static const size_t kBuf = 0x40000;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[kBuf]);

        bool ok = true;
        while (consumed + 4 <= limit) {
            uint8_t sigBuf[4];
            if (readInto(sigBuf, 4) != 4) {
                break;
            }
            uint32_t sig = (uint32_t)(sigBuf[0] | (sigBuf[1] << 8) | (sigBuf[2] << 16) | ((uint32_t)sigBuf[3] << 24));
            if (sig != 0x04034b50) {
                break;
            }
            uint8_t hdr[26];
            if (readInto(hdr, 26) != 26) {
                outError = "Corrupted ZIP header.";
                ok       = false;
                break;
            }
            uint16_t flags       = (uint16_t)(hdr[2] | (hdr[3] << 8));
            uint16_t compression = (uint16_t)(hdr[4] | (hdr[5] << 8));
            uint32_t crc         = (uint32_t)(hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | ((uint32_t)hdr[13] << 24));
            uint32_t compSize    = (uint32_t)(hdr[14] | (hdr[15] << 8) | (hdr[16] << 16) | ((uint32_t)hdr[17] << 24));
            uint32_t uncompSize  = (uint32_t)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
            uint16_t nameLen     = (uint16_t)(hdr[22] | (hdr[23] << 8));
            uint16_t extraLen    = (uint16_t)(hdr[24] | (hdr[25] << 8));

            std::string name;
            name.resize(nameLen);
            if (nameLen > 0 && readInto(&name[0], nameLen) != nameLen) {
                outError = "Corrupted ZIP header.";
                ok       = false;
                break;
            }
            if (extraLen > 0) {
                std::unique_ptr<uint8_t[]> extra(new uint8_t[extraLen]);
                if (readInto(extra.get(), extraLen) != extraLen) {
                    outError = "Corrupted ZIP header.";
                    ok       = false;
                    break;
                }
            }

            // Data-descriptor entries (flag bit 3) are accepted as long as the
            // local header still carries the real sizes, which is what the
            // console senders emit when streaming a zip without staging it.
            if (compression != 0) {
                outError = "Unsupported ZIP compression.";
                ok       = false;
                break;
            }
            if (!isSafeZipRelativePath(name)) {
                outError = "Invalid ZIP entry path.";
                ok       = false;
                break;
            }

            if (!name.empty() && name.back() == '/') {
                if (!sink.makeDir(name)) {
                    outError = "Failed to write extracted file.";
                    ok       = false;
                    break;
                }
                continue;
            }

            if (!sink.beginFile(name, uncompSize)) {
                outError = "Failed to write extracted file.";
                ok       = false;
                break;
            }

            uint32_t computedCrc = 0xFFFFFFFFu;
            uint32_t remaining   = compSize;
            bool fileOk          = true;
            while (remaining > 0) {
                if (cancelled && cancelled()) {
                    outError = "Transfer cancelled.";
                    fileOk   = false;
                    break;
                }
                uint32_t chunk = remaining > kBuf ? (uint32_t)kBuf : remaining;
                size_t rd      = readInto(buf.get(), chunk);
                if (rd == 0) {
                    outError = "Corrupted ZIP payload.";
                    fileOk   = false;
                    break;
                }
                computedCrc = updateCrc(computedCrc, buf.get(), rd);
                sink.writeFile(buf.get(), rd);
                remaining -= (uint32_t)rd;
                if (onBytes) {
                    onBytes(rd);
                }
            }
            sink.endFile();
            if (!fileOk) {
                ok = false;
                break;
            }

            // With flag bit 3 the local-header CRC field is zero and the real
            // CRC follows the data in a data descriptor (optionally prefixed
            // with its own signature).
            if (flags & 0x08) {
                uint8_t desc[16];
                if (readInto(desc, 4) != 4) {
                    outError = "Corrupted ZIP payload.";
                    ok       = false;
                    break;
                }
                uint32_t first = (uint32_t)(desc[0] | (desc[1] << 8) | (desc[2] << 16) | ((uint32_t)desc[3] << 24));
                if (first == 0x08074b50) {
                    if (readInto(desc + 4, 12) != 12) {
                        outError = "Corrupted ZIP payload.";
                        ok       = false;
                        break;
                    }
                    crc = (uint32_t)(desc[4] | (desc[5] << 8) | (desc[6] << 16) | ((uint32_t)desc[7] << 24));
                }
                else {
                    if (readInto(desc + 4, 8) != 8) {
                        outError = "Corrupted ZIP payload.";
                        ok       = false;
                        break;
                    }
                    crc = first;
                }
            }

            // Verify the extracted data against the CRC stored in the ZIP entry,
            // so a corrupted or truncated transfer is rejected instead of being
            // written out as-is.
            computedCrc ^= 0xFFFFFFFFu;
            if (computedCrc != crc) {
                outError = "Checksum mismatch in received file.";
                ok       = false;
                break;
            }
        }

        return ok;
    }

    bool sendZipStream(ByteSink& out, const std::vector<SendFile>& files, const std::vector<std::string>& dirs, FileReader& src,
        const CancelFn& cancelled, const ProgressFn& onBytes, bool& wasCancelled)
    {
        wasCancelled = false;

        std::vector<ZipEntry> central;
        central.reserve(dirs.size() + files.size());
        uint32_t offset = 0;

        auto sendChunk = [&](const void* data, size_t n) -> bool {
            if (!out.sendAll(data, n)) {
                return false;
            }
            offset += (uint32_t)n;
            if (onBytes) {
                onBytes(n);
            }
            return true;
        };

        for (const auto& dir : dirs) {
            ZipEntry centralEntry;
            centralEntry.name        = dir;
            centralEntry.crc         = 0;
            centralEntry.size        = 0;
            centralEntry.offset      = offset;
            centralEntry.isDirectory = true;

            std::string hdr;
            hdr.reserve(30 + dir.size());
            appendLe32(hdr, 0x04034b50);
            appendLe16(hdr, 20);
            appendLe16(hdr, 0);
            appendLe16(hdr, 0);
            appendLe16(hdr, 0);
            appendLe16(hdr, 0);
            appendLe32(hdr, 0);
            appendLe32(hdr, 0);
            appendLe32(hdr, 0);
            appendLe16(hdr, (uint16_t)dir.size());
            appendLe16(hdr, 0);
            hdr.append(dir);
            if (!sendChunk(hdr.data(), hdr.size())) {
                return false;
            }

            central.push_back(centralEntry);
        }

        static const size_t kBuf = 0x40000;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[kBuf]);

        for (const auto& entry : files) {
            ZipEntry centralEntry;
            centralEntry.name        = entry.relPath;
            centralEntry.crc         = 0;
            centralEntry.size        = entry.size;
            centralEntry.offset      = offset;
            centralEntry.isDirectory = false;

            std::string hdr;
            hdr.reserve(30 + entry.relPath.size());
            appendLe32(hdr, 0x04034b50);
            appendLe16(hdr, 20);
            appendLe16(hdr, 0x0008); // flag bit 3: CRC in the data descriptor
            appendLe16(hdr, 0);
            appendLe16(hdr, 0);
            appendLe16(hdr, 0);
            appendLe32(hdr, 0); // CRC, carried by the data descriptor instead
            appendLe32(hdr, entry.size);
            appendLe32(hdr, entry.size);
            appendLe16(hdr, (uint16_t)entry.relPath.size());
            appendLe16(hdr, 0);
            hdr.append(entry.relPath);
            if (!sendChunk(hdr.data(), hdr.size())) {
                return false;
            }

            if (!src.open(entry.absPath)) {
                return false;
            }
            uint32_t crc       = 0xFFFFFFFFu;
            uint32_t remaining = entry.size;
            while (remaining > 0) {
                if (cancelled && cancelled()) {
                    src.close();
                    wasCancelled = true;
                    return false;
                }
                uint32_t chunk = remaining > kBuf ? (uint32_t)kBuf : remaining;
                size_t rd      = src.read(buf.get(), chunk);
                if (rd == 0) {
                    // Short read: the promised sizes can no longer be met, so
                    // the transfer must fail rather than desync the stream.
                    src.close();
                    return false;
                }
                crc = updateCrc(crc, buf.get(), rd);
                if (!sendChunk(buf.get(), rd)) {
                    src.close();
                    return false;
                }
                remaining -= (uint32_t)rd;
            }
            src.close();
            crc ^= 0xFFFFFFFFu;

            std::string desc;
            desc.reserve(16);
            appendLe32(desc, 0x08074b50);
            appendLe32(desc, crc);
            appendLe32(desc, entry.size);
            appendLe32(desc, entry.size);
            if (!sendChunk(desc.data(), desc.size())) {
                return false;
            }

            centralEntry.crc = crc;
            central.push_back(centralEntry);
        }

        uint32_t centralOffset = offset;
        std::string tail;
        for (const auto& entry : central) {
            appendLe32(tail, 0x02014b50);
            appendLe16(tail, 20);
            appendLe16(tail, 20);
            appendLe16(tail, entry.isDirectory ? 0 : 0x0008);
            appendLe16(tail, 0);
            appendLe16(tail, 0);
            appendLe16(tail, 0);
            appendLe32(tail, entry.crc);
            appendLe32(tail, entry.size);
            appendLe32(tail, entry.size);
            appendLe16(tail, (uint16_t)entry.name.size());
            appendLe16(tail, 0);
            appendLe16(tail, 0);
            appendLe16(tail, 0);
            appendLe16(tail, 0);
            appendLe32(tail, entry.isDirectory ? 0x10 : 0); // MS-DOS directory attribute (matches chlink)
            appendLe32(tail, entry.offset);
            tail.append(entry.name);
        }

        uint32_t centralSize = (uint32_t)tail.size();
        appendLe32(tail, 0x06054b50);
        appendLe16(tail, 0);
        appendLe16(tail, 0);
        appendLe16(tail, (uint16_t)central.size());
        appendLe16(tail, (uint16_t)central.size());
        appendLe32(tail, centralSize);
        appendLe32(tail, centralOffset);
        appendLe16(tail, 0);

        return sendChunk(tail.data(), tail.size());
    }
}
