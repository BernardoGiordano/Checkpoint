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
#include "gui.hpp"
#include "io.hpp"
#include "loader.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "server.hpp"
#include "util.hpp"
#include "json.hpp"
#include <3ds.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
    static const int TRANSFER_PORT = 8000;
    static const char* TEMP_ZIP_SEND = "sdmc:/3ds/Checkpoint/transfer_send.zip";
    static const char* TEMP_ZIP_RECV = "sdmc:/3ds/Checkpoint/transfer_recv.zip";

    std::string g_token;
    std::string g_receiverIp;
    int g_receiverPort = TRANSFER_PORT;
    bool g_receiverRunning = false;
    std::string g_receiverNotice;

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

    u32 computeCrc(FS_Archive arch, const std::u16string& path)
    {
        FSStream input(arch, path, FS_OPEN_READ);
        if (!input.good()) {
            return 0;
        }
        initCrc();
        u32 crc = 0xFFFFFFFFu;
        static const u32 kBuf = 0x4000;
        std::unique_ptr<u8[]> buf(new u8[kBuf]);
        while (!input.eof()) {
            u32 rd = input.read(buf.get(), kBuf);
            if (rd == 0) {
                break;
            }
            crc = updateCrc(crc, buf.get(), rd);
        }
        input.close();
        return crc ^ 0xFFFFFFFFu;
    }

    void collectFiles(FS_Archive arch, const std::u16string& root, const std::u16string& sub, std::vector<FileEntry>& out)
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
            if (items.folder(i)) {
                std::u16string nextSub = sub + name + StringUtils::UTF8toUTF16("/");
                collectFiles(arch, root, nextSub, out);
            }
            else {
                std::u16string abs = current + name;
                std::string rel = StringUtils::UTF16toUTF8(sub + name);
                FSStream input(arch, abs, FS_OPEN_READ);
                if (!input.good()) {
                    continue;
                }
                FileEntry entry;
                entry.absPath = abs;
                entry.relPath = rel;
                entry.size = input.size();
                input.close();
                out.push_back(entry);
            }
        }
    }

    void writeLe16(FSStream& out, u16 v)
    {
        u8 b[2] = { (u8)(v & 0xFF), (u8)((v >> 8) & 0xFF) };
        out.write(b, 2);
    }

    void writeLe32(FSStream& out, u32 v)
    {
        u8 b[4] = { (u8)(v & 0xFF), (u8)((v >> 8) & 0xFF), (u8)((v >> 16) & 0xFF), (u8)((v >> 24) & 0xFF) };
        out.write(b, 4);
    }

    bool writeZip(const std::u16string& root, const std::u16string& zipPath, std::vector<FileEntry>& files, u32& outZipSize)
    {
        files.clear();
        collectFiles(Archive::sdmc(), root, StringUtils::UTF8toUTF16(""), files);
        if (files.empty()) {
            return false;
        }

        for (auto& entry : files) {
            entry.crc = computeCrc(Archive::sdmc(), entry.absPath);
        }

        u32 total = 22; // end of central directory
        for (const auto& entry : files) {
            total += 30 + entry.relPath.size() + entry.size; // local header + data
            total += 46 + entry.relPath.size();              // central directory
        }
        outZipSize = total;

        FSStream output(Archive::sdmc(), zipPath, FS_OPEN_WRITE, total);
        if (!output.good()) {
            return false;
        }

        std::vector<ZipEntry> central;
        central.reserve(files.size());

        static const u32 kBuf = 0x4000;
        std::unique_ptr<u8[]> buf(new u8[kBuf]);

        for (const auto& entry : files) {
            ZipEntry centralEntry;
            centralEntry.name = entry.relPath;
            centralEntry.crc = entry.crc;
            centralEntry.size = entry.size;
            centralEntry.offset = output.offset();

            writeLe32(output, 0x04034b50);
            writeLe16(output, 20);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe16(output, 0);
            writeLe32(output, entry.crc);
            writeLe32(output, entry.size);
            writeLe32(output, entry.size);
            writeLe16(output, (u16)entry.relPath.size());
            writeLe16(output, 0);
            output.write(entry.relPath.data(), entry.relPath.size());

            FSStream input(Archive::sdmc(), entry.absPath, FS_OPEN_READ);
            if (!input.good()) {
                output.close();
                return false;
            }
            while (!input.eof()) {
                u32 rd = input.read(buf.get(), kBuf);
                if (rd == 0) {
                    break;
                }
                output.write(buf.get(), rd);
            }
            input.close();

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
            writeLe32(output, 0);
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
        size_t start = 0;
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
            u16 flags = readLe16(input);
            u16 compression = readLe16(input);
            readLe16(input);
            readLe16(input);
            u32 crc = readLe32(input);
            u32 compSize = readLe32(input);
            u32 uncompSize = readLe32(input);
            u16 nameLen = readLe16(input);
            u16 extraLen = readLe16(input);

            std::string name;
            name.resize(nameLen);
            if (nameLen > 0) {
                input.read(name.data(), nameLen);
            }
            if (extraLen > 0) {
                std::unique_ptr<u8[]> extra(new u8[extraLen]);
                input.read(extra.get(), extraLen);
            }

            if (compression != 0 || (flags & 0x08)) {
                outError = "Unsupported ZIP compression.";
                input.close();
                return false;
            }
            (void)crc;

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
            u32 remaining = compSize;
            while (remaining > 0) {
                u32 chunk = remaining > kBuf ? kBuf : remaining;
                u32 rd = input.read(buf.get(), chunk);
                if (rd == 0) {
                    break;
                }
                output.write(buf.get(), rd);
                remaining -= rd;

                g_transferBytesDone += rd;
            }
            output.close();
        }

        input.close();
        return true;
    }

    std::string headerValue(const std::string& headers, const std::string& key)
    {
        std::string needle = key + ":";
        size_t pos = headers.find(needle);
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

    bool parseMultipart(const std::string& request, std::string& outMeta, std::string& outFile, std::string& outError)
    {
        size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            outError = "Bad request.";
            return false;
        }
        std::string headers = request.substr(0, headerEnd);
        std::string body = request.substr(headerEnd + 4);

        std::string contentType = headerValue(headers, "Content-Type");
        size_t bpos = contentType.find("boundary=");
        if (bpos == std::string::npos) {
            outError = "Missing boundary.";
            return false;
        }
        std::string boundary = contentType.substr(bpos + 9);
        if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
            boundary = boundary.substr(1, boundary.size() - 2);
        }

        std::string boundaryMarker = "--" + boundary;
        size_t pos = body.find(boundaryMarker);
        while (pos != std::string::npos) {
            pos += boundaryMarker.size();
            if (pos + 2 <= body.size() && body.compare(pos, 2, "--") == 0) {
                break;
            }
            if (pos + 2 <= body.size() && body.compare(pos, 2, "\r\n") == 0) {
                pos += 2;
            }

            size_t partHeaderEnd = body.find("\r\n\r\n", pos);
            if (partHeaderEnd == std::string::npos) {
                break;
            }
            std::string partHeader = body.substr(pos, partHeaderEnd - pos);
            size_t dataStart = partHeaderEnd + 4;
            size_t nextBoundary = body.find("\r\n" + boundaryMarker, dataStart);
            if (nextBoundary == std::string::npos) {
                break;
            }
            size_t dataEnd = nextBoundary;
            if (dataEnd >= 2 && body.compare(dataEnd - 2, 2, "\r\n") == 0) {
                dataEnd -= 2;
            }
            std::string partData = body.substr(dataStart, dataEnd - dataStart);

            if (partHeader.find("name=\"meta\"") != std::string::npos) {
                outMeta = partData;
            }
            else if (partHeader.find("name=\"file\"") != std::string::npos) {
                outFile = partData;
            }

            pos = nextBoundary + 2;
        }

        if (outMeta.empty() || outFile.empty()) {
            outError = "Incomplete form data.";
            return false;
        }
        return true;
    }

    Server::HttpResponse handleInfo(const std::string&, const std::string&)
    {
        nlohmann::json info;
        info["device"] = "3DS";
        info["version"] = StringUtils::format("%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
        info["token"] = g_token;
        info["maxUploadBytes"] = 0;
        info["freeSpaceBytes"] = 0;
        return {200, "application/json", info.dump()};
    }

    Server::HttpResponse handleUpload(const std::string&, const std::string& requestData)
    {
        auto cleanup = []() {
            g_isTransferringFile = false;
            g_transferIsNetwork  = false;
        };
        size_t headerEnd = requestData.find("\r\n\r\n");
        std::string headers = headerEnd == std::string::npos ? requestData : requestData.substr(0, headerEnd);
        std::string token = headerValue(headers, "X-CP-Token");
        if (token != g_token) {
            cleanup();
            return {403, "application/json", "{\"ok\":false,\"error\":\"Invalid token\"}"};
        }

        std::string metaJson;
        std::string fileData;
        std::string error;
        if (!parseMultipart(requestData, metaJson, fileData, error)) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Bad upload\"}"};
        }

        auto meta = nlohmann::json::parse(metaJson, nullptr, false);
        if (meta.is_discarded()) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Invalid meta\"}"};
        }

        std::string dataType = meta.value("dataType", "save");
        std::string titleId = meta.value("titleId", "");
        std::string titleName = meta.value("titleName", "Unknown");
        std::string backupName = meta.value("backupName", "");
        if (backupName.empty()) {
            backupName = "Received_" + DateTime::dateTimeStr();
        }

        std::u16string tempZipPath = StringUtils::UTF8toUTF16(TEMP_ZIP_RECV);
        FSStream output(Archive::sdmc(), tempZipPath, FS_OPEN_WRITE, (u32)fileData.size());
        if (!output.good()) {
            cleanup();
            return {500, "application/json", "{\"ok\":false,\"error\":\"Failed to store ZIP\"}"};
        }
        if (!fileData.empty()) {
            output.write(fileData.data(), (u32)fileData.size());
        }
        output.close();

        std::u16string basePath = StringUtils::UTF8toUTF16(dataType == "extdata" ? "/3ds/Checkpoint/extdata/" : "/3ds/Checkpoint/saves/");

        std::u16string destRoot;
        bool foundTitle = false;
        u64 tid = 0;
        if (!titleId.empty()) {
            tid = strtoull(titleId.c_str(), nullptr, 16);
        }
        if (tid != 0) {
            for (int i = 0; i < TitleLoader::getTitleCount(); i++) {
                Title t;
                TitleLoader::getTitle(t, i);
                if (t.id() == tid) {
                    destRoot = (dataType == "extdata") ? t.extdataPath() : t.savePath();
                    foundTitle = true;
                    break;
                }
            }
        }

        if (!foundTitle) {
            std::string safeName = titleName.empty() ? "Unknown" : titleName;
            std::string folder = safeName;
            if (!titleId.empty()) {
                folder = titleId + " " + safeName;
            }
            destRoot = basePath + StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(folder.c_str()));
            if (!io::directoryExists(Archive::sdmc(), destRoot)) {
                io::createDirectory(Archive::sdmc(), destRoot);
            }
            g_receiverNotice = "Aviso: titulo desconocido. Backup guardado en carpeta generica.";
            Logging::warning("Received backup for unknown title {} (stored under {}).", titleId, StringUtils::UTF16toUTF8(destRoot));
        }

        std::u16string backupRoot = destRoot + StringUtils::UTF8toUTF16("/") + StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(backupName.c_str())) +
            StringUtils::UTF8toUTF16("/");
        if (io::directoryExists(Archive::sdmc(), backupRoot)) {
            io::deleteFolderRecursively(Archive::sdmc(), backupRoot);
        }
        io::createDirectory(Archive::sdmc(), backupRoot);

        std::string extractError;
        bool ok = extractZip(tempZipPath, backupRoot, extractError);
        FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, tempZipPath.data()));

        cleanup();

        if (!ok) {
            return {500, "application/json", "{\"ok\":false,\"error\":\"Extract failed\"}"};
        }

        if (foundTitle) {
            TitleLoader::refreshDirectories(tid);
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["savedPath"] = StringUtils::UTF16toUTF8(backupRoot);
        return {200, "application/json", resp.dump()};
    }

    bool sendAll(int sock, const void* data, size_t len)
    {
        const u8* ptr = static_cast<const u8*>(data);
        size_t sent = 0;
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

    if (!g_receiverRunning) {
        srand((unsigned int)osGetTime());
        int pin = 1000 + (rand() % 9000);
        g_token = StringUtils::format("%04d", pin);
        g_receiverIp = Server::getAddress();
        g_receiverNotice.clear();
        size_t pos = g_receiverIp.find("://");
        if (pos != std::string::npos) {
            g_receiverIp = g_receiverIp.substr(pos + 3);
        }
        pos = g_receiverIp.find(":");
        if (pos != std::string::npos) {
            g_receiverIp = g_receiverIp.substr(0, pos);
        }
        g_receiverPort = TRANSFER_PORT;

        Server::registerHandler("/transfer/info", handleInfo);
        Server::registerHandler("/transfer/upload", handleUpload);
        g_receiverRunning = true;
    }

    return true;
}

void Transfer::stopReceiver(void)
{
    if (g_receiverRunning) {
        Server::unregisterHandler("/transfer/info");
        Server::unregisterHandler("/transfer/upload");
        g_receiverRunning = false;
    }
}

bool Transfer::receiverRunning(void)
{
    return g_receiverRunning;
}

std::string Transfer::receiverToken(void)
{
    return g_token;
}

std::string Transfer::receiverIp(void)
{
    return g_receiverIp;
}

int Transfer::receiverPort(void)
{
    return g_receiverPort;
}

std::string Transfer::receiverNotice(void)
{
    return g_receiverNotice;
}

void Transfer::clearReceiverNotice(void)
{
    g_receiverNotice.clear();
}

bool Transfer::sendBackup(const Title& title, const std::u16string& backupPath, const std::string& backupName, const std::string& dataType,
    const std::string& ip, u16 port, const std::string& token, std::string& outError)
{
    std::vector<FileEntry> files;
    u32 zipSize = 0;
    std::u16string zipPath = StringUtils::UTF8toUTF16(TEMP_ZIP_SEND);

    if (!writeZip(backupPath, zipPath, files, zipSize)) {
        outError = "Failed to create ZIP.";
        return false;
    }

    g_transferBytesTotal = zipSize;
    g_transferBytesDone = 0;
    g_transferMode = "Enviando";
    g_transferIsNetwork = true;
    g_isTransferringFile = true;

    nlohmann::json meta;
    meta["titleId"] = StringUtils::format("%016llX", title.id());
    meta["titleName"] = title.shortDescription();
    meta["dataType"] = dataType;
    meta["backupName"] = backupName;
    meta["zipBytesTotal"] = zipSize;
    meta["timestamp"] = DateTime::logDateTime();

    std::string metaStr = meta.dump();
    std::string boundary = "----checkpoint-boundary";

    std::string partMeta = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"meta\"\r\n"
        "Content-Type: application/json\r\n\r\n" +
        metaStr + "\r\n";

    std::string partFileHeader = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"backup.zip\"\r\n"
        "Content-Type: application/zip\r\n\r\n";

    std::string partEnd = "\r\n--" + boundary + "--\r\n";

    u32 contentLength = partMeta.size() + partFileHeader.size() + zipSize + partEnd.size();

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        g_isTransferringFile = false;
        g_transferIsNetwork = false;
        outError = "Failed to open socket.";
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        g_isTransferringFile = false;
        g_transferIsNetwork = false;
        outError = "Failed to connect.";
        return false;
    }

    std::string header = StringUtils::format("POST /transfer/upload HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n", ip.c_str(), port);
    header += StringUtils::format("X-CP-Token: %s\r\n", token.c_str());
    header += StringUtils::format("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    header += StringUtils::format("Content-Length: %u\r\n\r\n", contentLength);

    bool ok = sendAll(sock, header.data(), header.size()) && sendAll(sock, partMeta.data(), partMeta.size()) && sendAll(sock, partFileHeader.data(),
        partFileHeader.size());

    if (ok) {
        FSStream input(Archive::sdmc(), zipPath, FS_OPEN_READ);
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
                g_transferBytesDone += rd;

                C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
                g_screen->drawTop();
                C2D_SceneBegin(g_bottom);
                g_screen->drawBottom();
                Gui::frameEnd();
            }
            input.close();
        }
    }

    if (ok) {
        ok = sendAll(sock, partEnd.data(), partEnd.size());
    }

    char responseBuf[256];
    recv(sock, responseBuf, sizeof(responseBuf) - 1, 0);

    close(sock);

    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, zipPath.data()));

    g_isTransferringFile = false;
    g_transferIsNetwork = false;

    if (!ok) {
        outError = "Transfer failed.";
        return false;
    }

    return true;
}
