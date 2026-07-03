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

#include "transfer.hpp"
#include "common.hpp"
#include "directory.hpp"
#include "fsstream.hpp"
#include "io.hpp"
#include "json.hpp"
#include "loader.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "transferstatus.hpp"
#include "util.hpp"
#include <3ds.h>
#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
    static const int TRANSFER_PORT   = 8000;
    static const char* TEMP_ZIP_RECV = "/3ds/Checkpoint/transfer_recv.zip";

    std::string g_token;
    std::string g_receiverIp;
    int g_receiverPort     = TRANSFER_PORT;
    bool g_receiverRunning = false;
    std::atomic<bool> g_pendingRefresh{false};
    std::atomic<bool> g_receiverCompleted{false};
    // Guards the receiver state shared UI<->network thread: g_token (read by the
    // server thread in handleUpload, written by the UI thread), g_receiverIp,
    // g_receiverPort, g_receiverRunning, and g_receiverNotice /
    // g_receiverCompletedName (written by handleUpload, read by the UI thread).
    // Pass an empty string to clear the notice/name. TransferReceiver (4.1) is the
    // real home for this state.
    std::mutex g_receiverMutex;
    std::string g_receiverNotice;
    std::string g_receiverCompletedName;

    void setReceiverNotice(const std::string& notice)
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        g_receiverNotice = notice;
    }

    void setReceiverCompletedName(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        g_receiverCompletedName = name;
    }

    struct FileEntry {
        std::u16string absPath;
        std::string relPath;
        u32 size;
        u32 crc;
    };

    struct ZipEntry {
        std::string name;
        u32 crc;
        u32 size;
        u32 offset;
        bool isDirectory;
    };

    u32 crcTable[256];
    bool crcInit = false;

    void initCrc(void)
    {
        if (crcInit) {
            return;
        }
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            crcTable[i] = c;
        }
        crcInit = true;
    }

    u32 updateCrc(u32 crc, const u8* data, size_t len)
    {
        u32 c = crc;
        for (size_t i = 0; i < len; ++i) {
            c = crcTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
        }
        return c;
    }

    void collectFiles(FS_Archive arch, const std::u16string& root, const std::u16string& sub, std::vector<FileEntry>& out,
        std::vector<std::string>* outDirs = nullptr)
    {
        std::u16string current = root + sub;
        if (current.empty() || current.back() != u'/') {
            current += StringUtils::UTF8toUTF16("/");
        }
        Directory items(arch, current);
        if (!items.good()) {
            return;
        }
        for (size_t i = 0, sz = items.size(); i < sz; i++) {
            std::u16string name = items.entry(i);
            if (name == u"." || name == u"..") {
                continue;
            }
            if (items.folder(i)) {
                std::u16string nextSub = sub + name + StringUtils::UTF8toUTF16("/");
                if (outDirs != nullptr) {
                    outDirs->push_back(StringUtils::UTF16toUTF8(nextSub));
                }
                collectFiles(arch, root, nextSub, out, outDirs);
            }
            else {
                std::u16string abs = current + name;
                std::string rel    = StringUtils::UTF16toUTF8(sub + name);
                FSStream input(arch, abs, FS_OPEN_READ);
                if (!input.good()) {
                    continue;
                }
                FileEntry entry;
                entry.absPath = abs;
                entry.relPath = rel;
                entry.size    = input.size();
                input.close();
                out.push_back(entry);
            }
        }
    }

    u32 totalFileBytes(const std::vector<FileEntry>& files)
    {
        u32 total = 0;
        for (const auto& entry : files) {
            total += entry.size;
        }
        return total;
    }

    void writeLe16(FSStream& out, u16 v)
    {
        u8 b[2] = {(u8)(v & 0xFF), (u8)((v >> 8) & 0xFF)};
        out.write(b, 2);
    }

    void writeLe32(FSStream& out, u32 v)
    {
        u8 b[4] = {(u8)(v & 0xFF), (u8)((v >> 8) & 0xFF), (u8)((v >> 16) & 0xFF), (u8)((v >> 24) & 0xFF)};
        out.write(b, 4);
    }

    bool writeZip(const std::u16string& root, const std::u16string& zipPath, std::vector<FileEntry>& files, u32& outZipSize, std::string& outError)
    {
        files.clear();
        std::vector<std::string> dirs;
        collectFiles(Archive::sdmc(), root, StringUtils::UTF8toUTF16(""), files, &dirs);
        if (files.empty() && dirs.empty()) {
            outError = "No files or folders found to package.";
            return false;
        }

        u32 total = 22; // end of central directory
        for (const auto& dir : dirs) {
            total += 30 + dir.size(); // local header
            total += 46 + dir.size(); // central directory
        }
        for (const auto& entry : files) {
            total += 30 + entry.relPath.size() + entry.size; // local header + data
            total += 46 + entry.relPath.size();              // central directory
        }
        outZipSize = total;

        FSStream output(Archive::sdmc(), zipPath, FS_OPEN_WRITE, total);
        if (!output.good()) {
            outError = StringUtils::format("Cannot create package file (0x%08lX).", output.result());
            return false;
        }

        std::vector<ZipEntry> central;
        central.reserve(dirs.size() + files.size());

        static const u32 kBuf = 0x4000;
        std::unique_ptr<u8[]> buf(new u8[kBuf]);
        initCrc();

        for (const auto& dir : dirs) {
            ZipEntry centralEntry;
            centralEntry.name        = dir;
            centralEntry.crc         = 0;
            centralEntry.size        = 0;
            centralEntry.offset      = output.offset();
            centralEntry.isDirectory = true;

            writeLe32(output, 0x04034b50);
            writeLe16(output, 20);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe32(output, 0);
            writeLe32(output, 0);
            writeLe32(output, 0);
            writeLe16(output, (u16)dir.size());
            writeLe16(output, 0);
            output.write(dir.data(), dir.size());

            central.push_back(centralEntry);
        }

        for (const auto& entry : files) {
            ZipEntry centralEntry;
            centralEntry.name        = entry.relPath;
            centralEntry.crc         = 0;
            centralEntry.size        = entry.size;
            centralEntry.offset      = output.offset();
            centralEntry.isDirectory = false;

            // The CRC is computed during the single streaming pass below and
            // backfilled into this local-header field afterwards, so each file
            // is read only once instead of once for the CRC and once to stream.
            u32 crcFieldOffset = centralEntry.offset + 14;

            writeLe32(output, 0x04034b50);
            writeLe16(output, 20);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe32(output, 0); // CRC placeholder, backfilled after streaming
            writeLe32(output, entry.size);
            writeLe32(output, entry.size);
            writeLe16(output, (u16)entry.relPath.size());
            writeLe16(output, 0);
            output.write(entry.relPath.data(), entry.relPath.size());

            FSStream input(Archive::sdmc(), entry.absPath, FS_OPEN_READ);
            if (!input.good()) {
                output.close();
                outError = StringUtils::format("Cannot read source file for packaging (0x%08lX).", input.result());
                return false;
            }
            u32 crc = 0xFFFFFFFFu;
            while (!input.eof()) {
                u32 rd = input.read(buf.get(), kBuf);
                if (rd == 0) {
                    break;
                }
                crc = updateCrc(crc, buf.get(), rd);
                output.write(buf.get(), rd);
                TransferStatus::addBytesDone(rd);
            }
            input.close();
            crc ^= 0xFFFFFFFFu;

            // Backfill the real CRC into the local header, then resume appending.
            u32 endOffset = output.offset();
            output.offset(crcFieldOffset);
            writeLe32(output, crc);
            output.offset(endOffset);

            centralEntry.crc = crc;
            central.push_back(centralEntry);
        }

        u32 centralOffset = output.offset();
        for (const auto& entry : central) {
            writeLe32(output, 0x02014b50);
            writeLe16(output, 20);
            writeLe16(output, 20);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe32(output, entry.crc);
            writeLe32(output, entry.size);
            writeLe32(output, entry.size);
            writeLe16(output, (u16)entry.name.size());
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe32(output, entry.isDirectory ? (((u32)FS_ATTRIBUTE_DIRECTORY) << 16) : 0);
            writeLe32(output, entry.offset);
            output.write(entry.name.data(), entry.name.size());
        }

        u32 centralSize = output.offset() - centralOffset;
        writeLe32(output, 0x06054b50);
        writeLe16(output, 0);
        writeLe16(output, 0);
        writeLe16(output, (u16)central.size());
        writeLe16(output, (u16)central.size());
        writeLe32(output, centralSize);
        writeLe32(output, centralOffset);
        writeLe16(output, 0);

        output.close();
        return true;
    }

    u16 readLe16(FSStream& input)
    {
        u8 b[2];
        if (input.read(b, 2) != 2) {
            return 0;
        }
        return (u16)(b[0] | (b[1] << 8));
    }

    u32 readLe32(FSStream& input)
    {
        u8 b[4];
        if (input.read(b, 4) != 4) {
            return 0;
        }
        return (u32)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    }

    bool ensureDirectoryPath(const std::u16string& base, const std::string& relPath)
    {
        std::u16string current = base;
        size_t start           = 0;
        while (true) {
            size_t pos = relPath.find('/', start);
            if (pos == std::string::npos) {
                break;
            }
            std::string part = relPath.substr(start, pos - start);
            if (!part.empty()) {
                current += StringUtils::UTF8toUTF16(part.c_str());
                if (!io::directoryExists(Archive::sdmc(), current)) {
                    io::createDirectory(Archive::sdmc(), current);
                }
                current += StringUtils::UTF8toUTF16("/");
            }
            start = pos + 1;
        }
        return true;
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

    bool extractZip(const std::u16string& zipPath, const std::u16string& destRoot, std::string& outError)
    {
        FSStream input(Archive::sdmc(), zipPath, FS_OPEN_READ);
        if (!input.good()) {
            outError = "Failed to open received ZIP.";
            return false;
        }

        while (!input.eof()) {
            u32 sig = readLe32(input);
            if (sig != 0x04034b50) {
                break;
            }
            u16 version = readLe16(input);
            (void)version;
            u16 flags       = readLe16(input);
            u16 compression = readLe16(input);
            readLe16(input);
            readLe16(input);
            u32 crc        = readLe32(input);
            u32 compSize   = readLe32(input);
            u32 uncompSize = readLe32(input);
            u16 nameLen    = readLe16(input);
            u16 extraLen   = readLe16(input);

            std::string name;
            name.resize(nameLen);
            if (nameLen > 0) {
                if (input.read(name.data(), nameLen) != nameLen) {
                    outError = "Corrupted ZIP header.";
                    input.close();
                    return false;
                }
            }
            if (extraLen > 0) {
                std::unique_ptr<u8[]> extra(new u8[extraLen]);
                if (input.read(extra.get(), extraLen) != extraLen) {
                    outError = "Corrupted ZIP header.";
                    input.close();
                    return false;
                }
            }

            if (compression != 0 || (flags & 0x08)) {
                outError = "Unsupported ZIP compression.";
                input.close();
                return false;
            }

            if (!isSafeZipRelativePath(name)) {
                outError = "Invalid ZIP entry path.";
                input.close();
                return false;
            }

            if (!name.empty() && name.back() == '/') {
                std::u16string dirPath = destRoot + StringUtils::UTF8toUTF16(name.c_str());
                if (!io::directoryExists(Archive::sdmc(), dirPath)) {
                    io::createDirectory(Archive::sdmc(), dirPath);
                }
                continue;
            }

            ensureDirectoryPath(destRoot, name);
            std::u16string outPath = destRoot + StringUtils::UTF8toUTF16(name.c_str());
            FSStream output(Archive::sdmc(), outPath, FS_OPEN_WRITE, uncompSize);
            if (!output.good()) {
                outError = "Failed to write extracted file.";
                input.close();
                return false;
            }

            static const u32 kBuf = 0x4000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            initCrc();
            u32 computedCrc = 0xFFFFFFFFu;
            u32 remaining   = compSize;
            while (remaining > 0) {
                u32 chunk = remaining > kBuf ? kBuf : remaining;
                u32 rd    = input.read(buf.get(), chunk);
                if (rd == 0) {
                    outError = "Corrupted ZIP payload.";
                    output.close();
                    input.close();
                    return false;
                }
                computedCrc = updateCrc(computedCrc, buf.get(), rd);
                output.write(buf.get(), rd);
                remaining -= rd;

                TransferStatus::addBytesDone(rd);
            }
            if (remaining != 0) {
                outError = "Corrupted ZIP payload.";
                output.close();
                input.close();
                return false;
            }
            output.close();

            // Verify the extracted data against the CRC stored in the ZIP entry,
            // so a corrupted or truncated transfer is rejected instead of being
            // written out as-is.
            computedCrc ^= 0xFFFFFFFFu;
            if (computedCrc != crc) {
                outError = "Checksum mismatch in received file.";
                input.close();
                return false;
            }
        }

        input.close();
        return true;
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

    // Parses the multipart body without copying it. The (small) meta part is
    // returned by value, but the (potentially large) file part is returned as an
    // [offset, length) range into the original request buffer so it can be written
    // straight to disk without duplicating it in memory.
    bool parseMultipart(const std::string& request, std::string& outMeta, size_t& outFileOffset, size_t& outFileLen, std::string& outError)
    {
        outMeta.clear();
        outFileOffset = 0;
        outFileLen    = 0;
        bool haveFile = false;

        size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            outError = "Bad request.";
            return false;
        }
        std::string headers = request.substr(0, headerEnd);
        size_t bodyStart    = headerEnd + 4;

        std::string contentType = headerValue(headers, "Content-Type");
        size_t bpos             = contentType.find("boundary=");
        if (bpos == std::string::npos) {
            outError = "Missing boundary.";
            return false;
        }
        std::string boundary = contentType.substr(bpos + 9);
        if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
            boundary = boundary.substr(1, boundary.size() - 2);
        }

        std::string boundaryMarker     = "--" + boundary;
        std::string nextBoundaryMarker = "\r\n" + boundaryMarker;
        size_t pos                     = request.find(boundaryMarker, bodyStart);
        while (pos != std::string::npos) {
            pos += boundaryMarker.size();
            if (pos + 2 <= request.size() && request.compare(pos, 2, "--") == 0) {
                break;
            }
            if (pos + 2 <= request.size() && request.compare(pos, 2, "\r\n") == 0) {
                pos += 2;
            }

            size_t partHeaderEnd = request.find("\r\n\r\n", pos);
            if (partHeaderEnd == std::string::npos) {
                break;
            }
            std::string partHeader = request.substr(pos, partHeaderEnd - pos);
            size_t dataStart       = partHeaderEnd + 4;
            size_t nextBoundary    = request.find(nextBoundaryMarker, dataStart);
            if (nextBoundary == std::string::npos) {
                break;
            }
            // nextBoundary points at the CRLF that precedes the delimiter, so the
            // part data is exactly [dataStart, nextBoundary).
            size_t dataLen = nextBoundary - dataStart;

            if (partHeader.find("name=\"meta\"") != std::string::npos) {
                outMeta = request.substr(dataStart, dataLen);
            }
            else if (partHeader.find("name=\"file\"") != std::string::npos) {
                outFileOffset = dataStart;
                outFileLen    = dataLen;
                haveFile      = true;
            }

            pos = nextBoundary + 2;
        }

        if (outMeta.empty() || !haveFile) {
            outError = "Incomplete form data.";
            return false;
        }
        return true;
    }

    Server::HttpResponse handleInfo(const std::string&, const std::string&)
    {
        // Note: the PIN token is intentionally NOT exposed here. It must only ever
        // be shown on the receiver's screen and entered manually on the sender, so
        // that a device on the same network cannot read it and authenticate itself.
        nlohmann::json info;
        info["device"]         = "3DS";
        info["version"]        = StringUtils::format("%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
        info["maxUploadBytes"] = 0;
        info["freeSpaceBytes"] = 0;
        return {200, "application/json", info.dump()};
    }

    // Constant-time comparison so a wrong PIN can't be narrowed down by timing
    // how far the comparison got before it failed.
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

    Server::HttpResponse handleUpload(const std::string&, const std::string& requestData)
    {
        auto cleanup        = []() { TransferStatus::end(); };
        size_t headerEnd    = requestData.find("\r\n\r\n");
        std::string headers = headerEnd == std::string::npos ? requestData : requestData.substr(0, headerEnd);
        std::string token   = headerValue(headers, "X-CP-Token");
        std::string expectedToken;
        {
            std::lock_guard<std::mutex> lock(g_receiverMutex);
            expectedToken = g_token;
        }
        if (!constantTimeEquals(token, expectedToken)) {
            cleanup();
            return {403, "application/json", "{\"ok\":false,\"error\":\"Invalid token\"}"};
        }

        std::string metaJson;
        size_t fileOffset = 0;
        size_t fileLen    = 0;
        std::string error;
        if (!parseMultipart(requestData, metaJson, fileOffset, fileLen, error)) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Bad upload\"}"};
        }
        const char* fileData = requestData.data() + fileOffset;

        auto meta = nlohmann::json::parse(metaJson, nullptr, false);
        if (meta.is_discarded()) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Invalid meta\"}"};
        }

        std::string dataType   = meta.value("dataType", "save");
        std::string titleId    = meta.value("titleId", "");
        std::string titleName  = meta.value("titleName", "Unknown");
        std::string backupName = meta.value("backupName", "");
        bool isZip             = meta.value("isZip", false);
        setReceiverNotice("");
        if (backupName.empty()) {
            backupName = "Received_" + DateTime::dateTimeStr();
        }

        std::u16string basePath = StringUtils::UTF8toUTF16(dataType == "extdata" ? "/3ds/Checkpoint/extdata/" : "/3ds/Checkpoint/saves/");

        std::u16string destRoot;
        bool foundTitle   = false;
        bool mappedByName = false;
        u64 tid           = 0;
        if (!titleId.empty()) {
            tid = strtoull(titleId.c_str(), nullptr, 16);
        }
        if (tid != 0) {
            Title t;
            if (TitleCatalog::get().getTitleById(t, tid)) {
                destRoot   = (dataType == "extdata") ? t.extdataPath() : t.savePath();
                foundTitle = true;
            }
        }
        if (!foundTitle && !titleName.empty()) {
            Title t;
            if (TitleCatalog::get().getTitleByName(t, titleName)) {
                destRoot     = (dataType == "extdata") ? t.extdataPath() : t.savePath();
                foundTitle   = true;
                mappedByName = true;
                setReceiverNotice("Warning: title ID mismatch. Backup mapped by title name.");
                Logging::warning("Title ID {} not found, mapped by title name '{}'.", titleId, titleName);
            }
        }

        if (!foundTitle) {
            std::string safeName = titleName.empty() ? "Unknown" : titleName;
            std::string folder   = safeName;
            if (!titleId.empty()) {
                folder = titleId + " " + safeName;
            }
            destRoot = basePath + StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(folder.c_str()));
            if (!io::directoryExists(Archive::sdmc(), destRoot)) {
                io::createDirectory(Archive::sdmc(), destRoot);
            }
            setReceiverNotice("Warning: unknown title. Stored in:\n" + StringUtils::UTF16toUTF8(destRoot));
            Logging::warning("Received backup for unknown title {} (stored under {}).", titleId, StringUtils::UTF16toUTF8(destRoot));
        }

        std::u16string backupRoot = destRoot + StringUtils::UTF8toUTF16("/") +
                                    StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(backupName.c_str())) +
                                    StringUtils::UTF8toUTF16("/");
        if (io::directoryExists(Archive::sdmc(), backupRoot)) {
            io::deleteFolderRecursively(Archive::sdmc(), backupRoot);
        }
        io::createDirectory(Archive::sdmc(), backupRoot);

        std::u16string outputPath;
        if (isZip) {
            outputPath = StringUtils::UTF8toUTF16(TEMP_ZIP_RECV);
            if (io::fileExists(Archive::sdmc(), outputPath)) {
                FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, outputPath.data()));
            }
        }
        else {
            std::string fileName = meta.value("fileName", "");
            if (fileName.empty()) {
                fileName = "received.bin";
            }
            std::u16string safeFileName  = StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(fileName.c_str()));
            std::string safeFileNameUtf8 = StringUtils::UTF16toUTF8(safeFileName);
            if (safeFileNameUtf8.empty()) {
                safeFileNameUtf8 = "received.bin";
                safeFileName     = StringUtils::UTF8toUTF16("received.bin");
            }
            ensureDirectoryPath(backupRoot, safeFileNameUtf8);
            outputPath = backupRoot + safeFileName;
        }

        FSStream output(Archive::sdmc(), outputPath, FS_OPEN_WRITE, (u32)fileLen);
        if (!output.good()) {
            cleanup();
            std::string message = StringUtils::format("Failed to store file (0x%08lX).", output.result());
            return {500, "application/json", "{\"ok\":false,\"error\":\"" + message + "\"}"};
        }
        if (fileLen > 0) {
            output.write(fileData, (u32)fileLen);
        }
        output.close();

        if (isZip) {
            TransferStatus::beginNetwork("Extracting package", (u64)fileLen);

            std::string extractError;
            bool extracted = extractZip(outputPath, backupRoot, extractError);
            FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, outputPath.data()));

            if (!extracted) {
                cleanup();
                std::string message = extractError.empty() ? "Failed to extract package." : extractError;
                return {500, "application/json", "{\"ok\":false,\"error\":\"" + message + "\"}"};
            }

            TransferStatus::setBytesDone((u64)fileLen);
        }

        cleanup();

        if (!mappedByName && foundTitle) {
            setReceiverNotice("");
        }
        setReceiverCompletedName(backupName);
        g_receiverCompleted.store(true);
        g_pendingRefresh.store(true);

        nlohmann::json resp;
        resp["ok"]        = true;
        resp["savedPath"] = StringUtils::UTF16toUTF8(backupRoot);
        return {200, "application/json", resp.dump()};
    }

    bool sendAll(int sock, const void* data, size_t len)
    {
        const u8* ptr = static_cast<const u8*>(data);
        size_t sent   = 0;
        while (sent < len) {
            int rc = send(sock, ptr + sent, len - sent, 0);
            if (rc <= 0) {
                return false;
            }
            sent += rc;
        }
        return true;
    }
}

bool Transfer::startReceiver(std::string& outError)
{
    if (!Server::isRunning()) {
        outError = "HTTP server not available.";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        if (g_receiverRunning) {
            return true;
        }
    }

    srand((unsigned int)osGetTime());
    int pin           = 1000 + (rand() % 9000);
    std::string token = StringUtils::format("%04d", pin);
    std::string ip    = Server::getAddress();
    setReceiverNotice("");
    setReceiverCompletedName("");
    g_receiverCompleted.store(false);
    size_t pos = ip.find("://");
    if (pos != std::string::npos) {
        ip = ip.substr(pos + 3);
    }
    pos = ip.find(":");
    if (pos != std::string::npos) {
        ip = ip.substr(0, pos);
    }

    // Publish token/ip/port before the handler is reachable, so an upload that
    // arrives the instant it registers validates against the real token.
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        g_token        = token;
        g_receiverIp   = ip;
        g_receiverPort = TRANSFER_PORT;
    }

    Server::registerHandler("/transfer/info", handleInfo);
    Server::registerHandler("/transfer/upload", handleUpload);

    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        g_receiverRunning = true;
    }

    return true;
}

void Transfer::stopReceiver(void)
{
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        if (!g_receiverRunning) {
            return;
        }
    }
    Server::unregisterHandler("/transfer/info");
    Server::unregisterHandler("/transfer/upload");
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        g_receiverRunning = false;
    }
}

bool Transfer::receiverRunning(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_receiverRunning;
}

bool Transfer::consumePendingRefresh(void)
{
    return g_pendingRefresh.exchange(false);
}

std::string Transfer::receiverToken(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_token;
}

std::string Transfer::receiverIp(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_receiverIp;
}

int Transfer::receiverPort(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_receiverPort;
}

std::string Transfer::receiverNotice(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_receiverNotice;
}

bool Transfer::receiverHasCompleted(void)
{
    return g_receiverCompleted.load();
}

std::string Transfer::receiverCompletedName(void)
{
    std::lock_guard<std::mutex> lock(g_receiverMutex);
    return g_receiverCompletedName;
}

void Transfer::clearReceiverNotice(void)
{
    setReceiverNotice("");
}

void Transfer::clearReceiverCompletion(void)
{
    setReceiverCompletedName("");
    g_receiverCompleted.store(false);
}

Transfer::SendOutcome Transfer::sendBackup(const Title& title, const std::u16string& backupPath, const std::string& backupName,
    const std::string& dataType, const std::string& ip, u16 port, const std::string& token)
{
    // Every exit path must clear the transfer modal and remove the temp zip;
    // scope guards make that hold for each early return below.
    struct StatusGuard {
        ~StatusGuard() { TransferStatus::end(); }
    } statusGuard;

    struct ZipGuard {
        std::u16string path;
        bool armed = false;
        ~ZipGuard()
        {
            if (armed) {
                FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
            }
        }
    } zipGuard;

    std::vector<FileEntry> files;
    std::vector<std::string> dirs;
    collectFiles(Archive::sdmc(), backupPath, StringUtils::UTF8toUTF16(""), files, &dirs);
    if (files.empty() && dirs.empty()) {
        return SendOutcome{false, SendStage::EmptyBackup, ""};
    }

    bool isZip = files.size() != 1 || !dirs.empty();
    std::u16string payloadPath;
    std::string payloadName;
    u32 payloadSize = 0;

    if (isZip) {
        TransferStatus::beginNetwork("Preparing backup package", totalFileBytes(files));

        std::vector<FileEntry> zipEntries;
        u32 zipSize            = 0;
        std::string zipName    = StringUtils::format("/3ds/Checkpoint/transfer_send_%s.zip", DateTime::dateTimeStr().c_str());
        std::u16string zipPath = StringUtils::UTF8toUTF16(zipName.c_str());

        if (io::directoryExists(Archive::sdmc(), zipPath)) {
            io::deleteFolderRecursively(Archive::sdmc(), zipPath);
        }
        if (io::fileExists(Archive::sdmc(), zipPath)) {
            FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, zipPath.data()));
        }
        zipGuard.path  = zipPath;
        zipGuard.armed = true;
        std::string zipError;
        if (!writeZip(backupPath, zipPath, zipEntries, zipSize, zipError)) {
            return SendOutcome{false, SendStage::Zip, ""};
        }
        payloadPath = zipPath;
        payloadName = "backup.zip";
        payloadSize = zipSize;
    }
    else {
        const FileEntry& entry = files.front();
        payloadPath            = entry.absPath;
        payloadName            = entry.relPath;
        payloadSize            = entry.size;
    }

    TransferStatus::beginNetwork("Sending backup", payloadSize);

    nlohmann::json meta;
    meta["titleId"]        = StringUtils::format("%016llX", title.id());
    meta["titleName"]      = title.shortDescription();
    meta["dataType"]       = dataType;
    meta["backupName"]     = backupName;
    meta["isZip"]          = isZip;
    meta["fileBytesTotal"] = payloadSize;
    meta["fileName"]       = payloadName;
    meta["timestamp"]      = DateTime::logDateTime();

    std::string metaStr  = meta.dump();
    std::string boundary = StringUtils::format("----checkpoint-boundary-%llu", (unsigned long long)osGetTime());

    std::string partMeta = "--" + boundary +
                           "\r\n"
                           "Content-Disposition: form-data; name=\"meta\"\r\n"
                           "Content-Type: application/json\r\n\r\n" +
                           metaStr + "\r\n";

    std::string fileName = payloadName;
    size_t slashPos      = fileName.find_last_of('/');
    if (slashPos != std::string::npos) {
        fileName = fileName.substr(slashPos + 1);
    }
    if (fileName.empty()) {
        fileName = "backup.bin";
    }
    std::string partFileHeader = "--" + boundary +
                                 "\r\n"
                                 "Content-Disposition: form-data; name=\"file\"; filename=\"" +
                                 fileName +
                                 "\"\r\n"
                                 "Content-Type: application/octet-stream\r\n\r\n";

    std::string partEnd = "\r\n--" + boundary + "--\r\n";

    u32 contentLength = partMeta.size() + partFileHeader.size() + payloadSize + partEnd.size();

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return SendOutcome{false, SendStage::Socket, ""};
    }

    struct SockGuard {
        int fd;
        ~SockGuard() { close(fd); }
    } sockGuard{sock};

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return SendOutcome{false, SendStage::Resolve, ""};
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        return SendOutcome{false, SendStage::Connect, ""};
    }

    std::string header = StringUtils::format("POST /transfer/upload HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n", ip.c_str(), port);
    header += StringUtils::format("X-CP-Token: %s\r\n", token.c_str());
    header += StringUtils::format("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    header += StringUtils::format("Content-Length: %u\r\n\r\n", contentLength);

    bool ok = sendAll(sock, header.data(), header.size()) && sendAll(sock, partMeta.data(), partMeta.size()) &&
              sendAll(sock, partFileHeader.data(), partFileHeader.size());

    if (ok) {
        FSStream input(Archive::sdmc(), payloadPath, FS_OPEN_READ);
        if (!input.good()) {
            ok = false;
        }
        else {
            static const u32 kBuf = 0x4000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            while (!input.eof()) {
                u32 rd = input.read(buf.get(), kBuf);
                if (rd == 0) {
                    break;
                }
                if (!sendAll(sock, buf.get(), rd)) {
                    ok = false;
                    break;
                }
                TransferStatus::addBytesDone(rd);
            }
            input.close();
        }
    }

    if (ok) {
        ok = sendAll(sock, partEnd.data(), partEnd.size());
    }

    std::string response;
    {
        char responseBuf[512];
        while (true) {
            int rc = recv(sock, responseBuf, sizeof(responseBuf), 0);
            if (rc <= 0) {
                break;
            }
            response.append(responseBuf, (size_t)rc);
        }
    }

    if (!ok) {
        return SendOutcome{false, SendStage::Send, ""};
    }

    bool httpOk = response.rfind("HTTP/1.1 200", 0) == 0 || response.rfind("HTTP/1.0 200", 0) == 0;
    if (!httpOk) {
        std::string detail;
        if (!response.empty()) {
            size_t bodyPos = response.find("\r\n\r\n");
            if (bodyPos != std::string::npos && bodyPos + 4 < response.size()) {
                std::string body = response.substr(bodyPos + 4);
                auto j           = nlohmann::json::parse(body, nullptr, false);
                if (!j.is_discarded() && j.contains("error") && j["error"].is_string()) {
                    detail = j["error"].get<std::string>();
                }
            }
            if (detail.empty()) {
                size_t lineEnd = response.find("\r\n");
                detail         = lineEnd == std::string::npos ? response : response.substr(0, lineEnd);
            }
        }
        return SendOutcome{false, SendStage::Response, detail};
    }

    return SendOutcome{true, SendStage::Response, ""};
}
