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
#include "configuration.hpp"
#include "directory.hpp"
#include "io.hpp"
#include "json.hpp"
#include "logging.hpp"
#include "server.hpp"
#include "titlecatalog.hpp"
#include "transferprotocol.hpp"
#include "transferstatus.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// The wire protocol (CRC, zip framing, path safety, HTTP header/auth helpers) is
// shared with the 3DS build and chlink; the definitions live in
// common/transferprotocol. This file owns only the Switch file+socket IO around it.
using namespace TransferProto;

namespace {
    constexpr int TRANSFER_PORT        = 8000;
    const char* CHECKPOINT_ROOT        = "sdmc:/switch/Checkpoint/";
    const char* SAVES_ROOT             = "sdmc:/switch/Checkpoint/saves/";
    const char* TEMP_UPLOAD            = "sdmc:/switch/Checkpoint/transfer_upload.tmp";
    const std::string TEMP_SEND_PREFIX = "transfer_send_";
    const std::string TEMP_SEND_SUFFIX = ".zip";

    std::string g_token;
    std::string g_receiverIp;
    int g_receiverPort     = TRANSFER_PORT;
    bool g_receiverRunning = false;
    std::atomic<bool> g_pendingRefresh{false};
    std::atomic<bool> g_receiverCompleted{false};
    std::atomic<u64> g_completedTitleId{0};
    // The PIN is the sole credential gating writes into the SD tree. Bound brute
    // force: after this many bad-token uploads the receiver shuts itself down.
    // Reset when the receiver is (re)armed in startReceiver.
    constexpr int MAX_AUTH_ATTEMPTS = 5;
    std::atomic<int> g_failedAuthAttempts{0};
    // Guards the receiver state shared UI<->server thread: g_token, g_receiverIp,
    // g_receiverPort, g_receiverRunning, and the notice/name strings.
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

    u64 fileSize(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            return 0;
        }
        return (u64)st.st_size;
    }

    // Returns false if any regular file exceeds the store-zip entry limit; such a
    // file cannot be framed (no zip64) and the whole send must refuse rather than
    // truncate the size to u32.
    bool collectFiles(const std::string& root, const std::string& sub, std::vector<SendFile>& out, std::vector<std::string>* outDirs = nullptr)
    {
        std::string current = root;
        if (!current.empty() && current.back() != '/') {
            current += "/";
        }
        current += sub;
        Directory items(current);
        if (!items.good()) {
            return true;
        }
        bool ok = true;
        for (size_t i = 0, sz = items.size(); i < sz; i++) {
            std::string name = items.entry(i);
            if (name == "." || name == "..") {
                continue;
            }
            if (items.folder(i)) {
                std::string nextSub = sub + name + "/";
                if (outDirs != nullptr) {
                    outDirs->push_back(nextSub);
                }
                if (!collectFiles(root, nextSub, out, outDirs)) {
                    ok = false;
                }
            }
            else {
                SendFile entry;
                entry.absPath = current + name;
                entry.relPath = sub + name;
                u64 sz64      = fileSize(entry.absPath);
                if (sz64 > TransferProto::kZipMaxSize) {
                    ok = false;
                    continue;
                }
                entry.size = (u32)sz64;
                out.push_back(entry);
            }
        }
        return ok;
    }

    void ensureDirectoryPath(const std::string& base, const std::string& relPath)
    {
        std::string current = base;
        size_t start        = 0;
        while (true) {
            size_t pos = relPath.find('/', start);
            if (pos == std::string::npos) {
                break;
            }
            std::string part = relPath.substr(start, pos - start);
            if (!part.empty()) {
                current += part;
                if (!io::directoryExists(current)) {
                    io::createDirectory(current);
                }
                current += "/";
            }
            start = pos + 1;
        }
    }

    // Reads a bounded head window off the front of the streamed body so
    // TransferProto::parseMultipart can locate the parts without scanning the
    // (large) payload.
    bool readBodyHead(const std::string& bodyPath, u64 bodyLen, std::string& outHead, std::string& outError)
    {
        FILE* f = fopen(bodyPath.c_str(), "rb");
        if (f == nullptr) {
            outError = "Failed to open upload body.";
            return false;
        }
        const size_t headWindow = 64 * 1024;
        size_t headLen          = (size_t)std::min<u64>(headWindow, bodyLen);
        outHead.resize(headLen);
        if (headLen > 0 && fread(&outHead[0], 1, headLen, f) != headLen) {
            fclose(f);
            outError = "Failed to read upload body.";
            return false;
        }
        fclose(f);
        return true;
    }

    // ByteReader over the streamed body, positioned at the file part's start.
    // TransferProto::extractZip owns the length accounting.
    struct BodyReader : TransferProto::ByteReader {
        FILE* input = nullptr;
        BodyReader(const std::string& path, u64 startOffset)
        {
            input = fopen(path.c_str(), "rb");
            if (input != nullptr && fseek(input, (long)startOffset, SEEK_SET) != 0) {
                fclose(input);
                input = nullptr;
            }
        }
        bool good() const { return input != nullptr; }
        ~BodyReader() override
        {
            if (input != nullptr) {
                fclose(input);
            }
        }
        size_t read(void* dst, size_t n) override { return input != nullptr ? fread(dst, 1, n, input) : 0; }
    };

    // ExtractSink writing under a destination root via FILE*.
    struct FileExtractSink : TransferProto::ExtractSink {
        std::string destRoot;
        FILE* output = nullptr;
        explicit FileExtractSink(std::string root) : destRoot(std::move(root)) {}
        bool makeDir(const std::string& relPath) override
        {
            std::string dirPath = destRoot + relPath;
            if (!io::directoryExists(dirPath)) {
                io::createDirectory(dirPath);
            }
            return true;
        }
        bool beginFile(const std::string& relPath, uint32_t) override
        {
            ensureDirectoryPath(destRoot, relPath);
            output = fopen((destRoot + relPath).c_str(), "wb");
            return output != nullptr;
        }
        bool writeFile(const void* data, size_t n) override
        {
            fwrite(data, 1, n, output);
            return true;
        }
        void endFile() override
        {
            if (output != nullptr) {
                fclose(output);
                output = nullptr;
            }
        }
    };

    // FileReader opening backup files via FILE* for the send.
    struct StdFileReader : TransferProto::FileReader {
        FILE* input = nullptr;
        bool open(const std::string& absPath) override
        {
            input = fopen(absPath.c_str(), "rb");
            return input != nullptr;
        }
        size_t read(void* dst, size_t n) override { return input != nullptr ? fread(dst, 1, n, input) : 0; }
        void close() override
        {
            if (input != nullptr) {
                fclose(input);
                input = nullptr;
            }
        }
    };

    Server::HttpResponse handleInfo(const std::string&, const std::string&)
    {
        // The PIN is intentionally NOT exposed here: it must only ever be shown on
        // the receiver's screen and typed on the sender, so a device on the same
        // network can't read it and authenticate itself.
        nlohmann::json info;
        info["device"]         = "Switch";
        info["version"]        = StringUtils::format("%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
        info["maxUploadBytes"] = 0; // 0 = unlimited: the body is streamed to SD, not buffered in RAM
        info["freeSpaceBytes"] = 0;
        return {200, "application/json", info.dump()};
    }

    // Streaming upload handler: the server has already written the raw multipart
    // body to `req.bodyPath`. We parse the meta out of the head, resolve the
    // destination title, then extract the ZIP (or copy the raw file) straight
    // from the body temp file's file-part byte range.
    Server::HttpResponse handleUpload(const Server::UploadRequest& req)
    {
        auto cleanup = []() { TransferStatus::end(); };

        std::string token = headerValue(req.headers, "X-CP-Token");
        std::string expectedToken;
        {
            std::lock_guard<std::mutex> lock(g_receiverMutex);
            expectedToken = g_token;
        }
        if (!constantTimeEquals(token, expectedToken)) {
            cleanup();
            int attempts = g_failedAuthAttempts.fetch_add(1) + 1;
            Logging::warning("Rejected upload with invalid token ({}/{} attempts).", attempts, MAX_AUTH_ATTEMPTS);
            if (attempts >= MAX_AUTH_ATTEMPTS) {
                setReceiverNotice("Too many invalid PIN attempts; receiver stopped.");
                Logging::warning("Too many invalid PIN attempts; stopping receiver.");
                Transfer::stopReceiver();
            }
            return {403, "application/json", "{\"ok\":false,\"error\":\"Invalid token\"}"};
        }

        std::string contentType = headerValue(req.headers, "Content-Type");
        size_t bpos             = contentType.find("boundary=");
        if (bpos == std::string::npos) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Missing boundary\"}"};
        }
        std::string boundary = contentType.substr(bpos + 9);
        if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
            boundary = boundary.substr(1, boundary.size() - 2);
        }

        std::string metaJson;
        u64 fileOffset = 0;
        u64 fileLen    = 0;
        std::string error;
        std::string head;
        if (!readBodyHead(req.bodyPath, req.bodyLength, head, error) ||
            !parseMultipart(head, req.bodyLength, boundary, metaJson, fileOffset, fileLen, error)) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Bad upload\"}"};
        }

        auto meta = nlohmann::json::parse(metaJson, nullptr, false);
        if (meta.is_discarded()) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Invalid meta\"}"};
        }

        std::string titleId    = meta.value("titleId", "");
        std::string titleName  = meta.value("titleName", "Unknown");
        std::string backupName = meta.value("backupName", "");
        bool isZip             = meta.value("isZip", false);
        setReceiverNotice("");
        if (backupName.empty()) {
            backupName = "Received_" + DateTime::dateTimeStr();
        }

        std::string destRoot;
        bool foundTitle   = false;
        bool mappedByName = false;
        u64 tid           = 0;
        u64 resolvedId    = 0;
        if (!titleId.empty()) {
            tid = strtoull(titleId.c_str(), nullptr, 16);
        }
        if (tid != 0) {
            Title t;
            if (TitleCatalog::get().getTitleById(t, tid)) {
                destRoot   = t.path();
                resolvedId = t.id();
                foundTitle = true;
            }
        }
        if (!foundTitle && !titleName.empty()) {
            Title t;
            if (TitleCatalog::get().getTitleByName(t, titleName)) {
                destRoot     = t.path();
                resolvedId   = t.id();
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
            destRoot = std::string(SAVES_ROOT) + StringUtils::removeForbiddenCharacters(folder);
            if (!io::directoryExists(destRoot)) {
                io::createDirectory(destRoot);
            }
            setReceiverNotice("Warning: unknown title. Stored in:\n" + destRoot);
            Logging::warning("Received backup for unknown title {} (stored under {}).", titleId, destRoot);
        }

        std::string backupRoot = destRoot + "/" + StringUtils::removeForbiddenCharacters(backupName) + "/";
        if (io::directoryExists(backupRoot)) {
            io::deleteFolderRecursively(backupRoot);
        }
        io::createDirectory(backupRoot);

        if (isZip) {
            TransferStatus::beginNetwork("Extracting package", fileLen);
            std::string extractError;
            BodyReader reader(req.bodyPath, fileOffset);
            FileExtractSink sink(backupRoot);
            bool extracted = reader.good() && extractZip(
                                                  reader, fileLen, sink, []() { return TransferStatus::cancelRequested(); },
                                                  [](size_t n) { TransferStatus::addBytesDone(n); }, extractError);
            if (!reader.good() && extractError.empty()) {
                extractError = "Failed to open received package.";
            }
            if (!extracted) {
                // A failed (or cancelled) extract leaves a half-populated backup
                // folder behind; remove it so the receiver never keeps a backup
                // it can't trust.
                io::deleteFolderRecursively(backupRoot);
                cleanup();
                std::string message = extractError.empty() ? "Failed to extract package." : extractError;
                nlohmann::json err;
                err["ok"]    = false;
                err["error"] = message;
                return {500, "application/json", err.dump()};
            }
            TransferStatus::setBytesDone(fileLen);
        }
        else {
            std::string safeFileName = sanitizeFileName(meta.value("fileName", ""));
            if (safeFileName.empty()) {
                safeFileName = "received.bin";
            }
            std::string outPath = backupRoot + safeFileName;

            FILE* src = fopen(req.bodyPath.c_str(), "rb");
            FILE* dst = fopen(outPath.c_str(), "wb");
            if (src == nullptr || dst == nullptr) {
                if (src) {
                    fclose(src);
                }
                if (dst) {
                    fclose(dst);
                }
                Logging::error("Failed to open {} to store the received file.", outPath);
                io::deleteFolderRecursively(backupRoot);
                cleanup();
                return {500, "application/json", "{\"ok\":false,\"error\":\"Failed to store file\"}"};
            }
            fseek(src, (long)fileOffset, SEEK_SET);
            static const size_t kBuf = 0x40000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            u64 remaining = fileLen;
            bool copyOk   = true;
            while (remaining > 0) {
                if (TransferStatus::cancelRequested()) {
                    copyOk = false;
                    break;
                }
                size_t chunk = remaining > kBuf ? kBuf : (size_t)remaining;
                size_t rd    = fread(buf.get(), 1, chunk, src);
                if (rd == 0) {
                    copyOk = false;
                    break;
                }
                fwrite(buf.get(), 1, rd, dst);
                remaining -= rd;
            }
            fclose(src);
            fclose(dst);
            if (!copyOk) {
                Logging::error("Failed to store the received file {} ({} of {} bytes missing).", outPath, remaining, (u64)fileLen);
                io::deleteFolderRecursively(backupRoot);
                cleanup();
                return {500, "application/json", "{\"ok\":false,\"error\":\"Failed to store file\"}"};
            }
            Logging::info("Received {} bytes into {}.", (u64)fileLen, outPath);
        }

        cleanup();

        if (!mappedByName && foundTitle) {
            setReceiverNotice("");
        }
        setReceiverCompletedName(backupName);
        g_completedTitleId.store(resolvedId);
        g_receiverCompleted.store(true);
        g_pendingRefresh.store(true);

        nlohmann::json resp;
        resp["ok"]        = true;
        resp["savedPath"] = backupRoot;
        return {200, "application/json", resp.dump()};
    }

    // A stalled receiver must not wedge the sender: the send runs on the
    // TransferJob worker and app exit is gated on !active(), so a peer that
    // accepts and then never reads/replies would hang recv()/send() and join()
    // forever. The 3DS SOC layer has no SO_RCVTIMEO (see server.cpp's pollRecv),
    // so we keep the socket non-blocking and bound every wait with poll().
    constexpr int NET_TIMEOUT_MS = 15000;

    // Waits up to timeoutMs for `events` on sock. Returns 1 if ready, 0 on
    // timeout, -1 on error.
    int pollSocket(int sock, short events, int timeoutMs)
    {
        struct pollfd pfd;
        pfd.fd      = sock;
        pfd.events  = events;
        pfd.revents = 0;
        int rc      = poll(&pfd, 1, timeoutMs);
        return rc > 0 ? 1 : rc;
    }

    bool sendAll(int sock, const void* data, size_t len)
    {
        const u8* ptr = static_cast<const u8*>(data);
        size_t sent   = 0;
        while (sent < len) {
            if (pollSocket(sock, POLLOUT, NET_TIMEOUT_MS) <= 0) {
                return false; // timeout or error: abandon rather than block forever
            }
            int rc = send(sock, ptr + sent, len - sent, 0);
            if (rc <= 0) {
                return false;
            }
            sent += rc;
        }
        return true;
    }

    // ByteSink over the poll-gated send socket for TransferProto::sendZipStream.
    struct SocketByteSink : TransferProto::ByteSink {
        int sock;
        explicit SocketByteSink(int s) : sock(s) {}
        bool sendAll(const void* data, size_t len) override { return ::sendAll(sock, data, len); }
    };
}

void Transfer::sweepTempFiles(void)
{
    if (io::fileExists(TEMP_UPLOAD)) {
        std::remove(TEMP_UPLOAD);
        Logging::info("Removed leftover {} from a previous run.", TEMP_UPLOAD);
    }

    Directory dir(CHECKPOINT_ROOT);
    if (!dir.good()) {
        return;
    }
    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            continue;
        }
        std::string name = dir.entry(i);
        if (name.size() > TEMP_SEND_PREFIX.size() + TEMP_SEND_SUFFIX.size() && name.compare(0, TEMP_SEND_PREFIX.size(), TEMP_SEND_PREFIX) == 0 &&
            name.compare(name.size() - TEMP_SEND_SUFFIX.size(), TEMP_SEND_SUFFIX.size(), TEMP_SEND_SUFFIX) == 0) {
            std::string path = std::string(CHECKPOINT_ROOT) + name;
            std::remove(path.c_str());
            Logging::info("Removed leftover {} from a previous run.", name);
        }
    }
}

bool Transfer::startReceiver(std::string& outError)
{
    {
        std::lock_guard<std::mutex> lock(g_receiverMutex);
        if (g_receiverRunning) {
            return true;
        }
    }
    if (Server::getAddress().empty()) {
        outError = "HTTP server not available.";
        return false;
    }

    g_failedAuthAttempts.store(0);
    // A PIN configured in Settings wins, so a PC-side script can be pointed at
    // the console without reading the screen first; anything not exactly 4
    // digits (including the empty default) falls back to a fresh random one.
    // A 4-digit PIN from srand(time) is trivially predictable, so that fallback
    // is seeded from the system CSPRNG, not the clock.
    std::string token = Configuration::getInstance().defaultReceivePin();
    if (!TransferProto::validPin(token)) {
        token = StringUtils::format("%04d", 1000 + (int)(randomGet64() % 9000));
    }
    std::string ip = Server::getAddress();
    setReceiverNotice("");
    setReceiverCompletedName("");
    g_receiverCompleted.store(false);
    g_completedTitleId.store(0);
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
    Server::registerUploadHandler("/transfer/upload", TEMP_UPLOAD, handleUpload);

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

u64 Transfer::consumeCompletedTitleId(void)
{
    return g_completedTitleId.exchange(0);
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

Transfer::SendOutcome Transfer::sendBackup(Title& title, const std::string& backupPath, const std::string& backupName, const std::string& dataType,
    const std::string& ip, u16 port, const std::string& token)
{
    // Every exit path must clear the transfer modal; the scope guard makes
    // that hold for each early return below.
    struct StatusGuard {
        ~StatusGuard() { TransferStatus::end(); }
    } statusGuard;

    std::vector<SendFile> files;
    std::vector<std::string> dirs;
    if (!collectFiles(backupPath, "", files, &dirs)) {
        return SendOutcome{false, SendStage::PayloadTooLarge, ""};
    }
    if (files.empty() && dirs.empty()) {
        return SendOutcome{false, SendStage::EmptyBackup, ""};
    }

    // Multi-file backups are zipped on the fly by sendZipStream — no staged
    // temp zip; the exact zip size is known upfront because entries are
    // store-only.
    bool isZip = files.size() != 1 || !dirs.empty();
    std::string payloadPath;
    std::string payloadName;
    u32 payloadSize = 0;

    if (isZip) {
        std::optional<u64> zipSize = zipStreamSize(files, dirs);
        if (!zipSize) {
            return SendOutcome{false, SendStage::PayloadTooLarge, ""};
        }
        payloadName = "backup.zip";
        payloadSize = (u32)*zipSize; // checked <= kZipMaxSize above
    }
    else {
        const SendFile& entry = files.front();
        payloadPath           = entry.absPath;
        payloadName           = entry.relPath;
        payloadSize           = entry.size;
    }

    TransferStatus::beginNetwork("Sending backup", payloadSize);

    nlohmann::json meta;
    meta["titleId"]        = StringUtils::format("%016llX", title.id());
    meta["titleName"]      = title.displayName();
    meta["dataType"]       = dataType;
    meta["backupName"]     = backupName;
    meta["isZip"]          = isZip;
    meta["fileBytesTotal"] = payloadSize;
    meta["fileName"]       = payloadName;
    meta["timestamp"]      = DateTime::logDateTime();

    std::string metaStr  = meta.dump();
    std::string boundary = StringUtils::format("----checkpoint-boundary-%llu", (unsigned long long)time(nullptr));

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

    u64 contentLength64 = (u64)partMeta.size() + partFileHeader.size() + payloadSize + partEnd.size();
    if (contentLength64 > TransferProto::kZipMaxSize) {
        return SendOutcome{false, SendStage::PayloadTooLarge, ""};
    }
    u32 contentLength = (u32)contentLength64;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        return SendOutcome{false, SendStage::Socket, ""};
    }

    struct SockGuard {
        int fd;
        ~SockGuard() { close(fd); }
    } sockGuard{sock};

    // Non-blocking for the socket's whole lifetime; every send/recv is poll-gated
    // below so a half-open peer can't block forever.
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return SendOutcome{false, SendStage::Resolve, ""};
    }
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            return SendOutcome{false, SendStage::Connect, ""};
        }
        // Bounded wait for the connection to complete, then confirm via SO_ERROR.
        if (pollSocket(sock, POLLOUT, NET_TIMEOUT_MS) <= 0) {
            return SendOutcome{false, SendStage::Connect, ""};
        }
        int soErr        = 0;
        socklen_t optLen = sizeof(soErr);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &soErr, &optLen) != 0 || soErr != 0) {
            return SendOutcome{false, SendStage::Connect, ""};
        }
    }

    std::string header = StringUtils::format("POST /transfer/upload HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n", ip.c_str(), port);
    header += StringUtils::format("X-CP-Token: %s\r\n", token.c_str());
    header += StringUtils::format("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    header += StringUtils::format("Content-Length: %u\r\n\r\n", contentLength);

    bool ok = sendAll(sock, header.data(), header.size()) && sendAll(sock, partMeta.data(), partMeta.size()) &&
              sendAll(sock, partFileHeader.data(), partFileHeader.size());

    if (ok && isZip) {
        bool cancelled = false;
        SocketByteSink sink(sock);
        StdFileReader reader;
        if (!sendZipStream(
                sink, files, dirs, reader, []() { return TransferStatus::cancelRequested(); }, [](size_t n) { TransferStatus::addBytesDone(n); },
                cancelled)) {
            if (cancelled) {
                // The scope guard closes the socket, dropping the connection,
                // which the receiver treats as an aborted request.
                return SendOutcome{false, SendStage::Cancelled, ""};
            }
            ok = false;
        }
    }
    else if (ok) {
        FILE* input = fopen(payloadPath.c_str(), "rb");
        if (input == nullptr) {
            ok = false;
        }
        else {
            static const size_t kBuf = 0x40000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            while (true) {
                if (TransferStatus::cancelRequested()) {
                    fclose(input);
                    // The scope guard closes the socket, dropping the connection,
                    // which the receiver treats as an aborted request.
                    return SendOutcome{false, SendStage::Cancelled, ""};
                }
                size_t rd = fread(buf.get(), 1, kBuf, input);
                if (rd == 0) {
                    break;
                }
                if (!sendAll(sock, buf.get(), rd)) {
                    ok = false;
                    break;
                }
                TransferStatus::addBytesDone(rd);
            }
            fclose(input);
        }
    }

    if (ok) {
        ok = sendAll(sock, partEnd.data(), partEnd.size());
    }

    std::string response;
    {
        char responseBuf[512];
        while (true) {
            if (pollSocket(sock, POLLIN, NET_TIMEOUT_MS) <= 0) {
                break; // timeout or error: stop waiting on a silent peer
            }
            int rc = recv(sock, responseBuf, sizeof(responseBuf), 0);
            if (rc <= 0) {
                break;
            }
            response.append(responseBuf, (size_t)rc);
        }
    }

    if (!ok) {
        return SendOutcome{false, TransferStatus::cancelRequested() ? SendStage::Cancelled : SendStage::Send, ""};
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

std::optional<Transfer::TransferTarget> Transfer::parseTarget(const std::string& ipPort)
{
    auto hp = TransferProto::parseTarget(ipPort);
    if (!hp) {
        return std::nullopt;
    }
    return TransferTarget{std::move(hp->ip), hp->port};
}

bool Transfer::validPin(const std::string& pin)
{
    return TransferProto::validPin(pin);
}
