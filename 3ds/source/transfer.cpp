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
#include "fsstream.hpp"
#include "io.hpp"
#include "json.hpp"
#include "loader.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "server.hpp"
#include "transferprotocol.hpp"
#include "transferstatus.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// The wire protocol (CRC, zip framing, path safety, HTTP header/auth helpers) is
// shared with the Switch build and chlink; the definitions live in
// common/transferprotocol. This file owns only the 3DS file+socket IO around it.
using namespace TransferProto;

namespace {
    static const int TRANSFER_PORT = 8000;
    // The server streams the raw upload body here; handleUpload extracts/copies
    // straight from the file-part byte range, so no separate staged zip is needed.
    static const char* TEMP_UPLOAD = "/3ds/Checkpoint/transfer_upload.tmp";
    // Legacy staging file from before the streaming server; still swept on boot.
    static const char* TEMP_ZIP_RECV_LEGACY = "/3ds/Checkpoint/transfer_recv.zip";

    std::string g_token;
    std::string g_receiverIp;
    int g_receiverPort     = TRANSFER_PORT;
    bool g_receiverRunning = false;
    std::atomic<bool> g_pendingRefresh{false};
    std::atomic<bool> g_receiverCompleted{false};
    // The PIN is the sole credential gating writes into the SD tree. Bound brute
    // force: after this many bad-token uploads the receiver shuts itself down.
    // Reset when the receiver is (re)armed in startReceiver.
    constexpr int MAX_AUTH_ATTEMPTS = 5;
    std::atomic<int> g_failedAuthAttempts{0};

    // A 4-digit PIN from srand(time) is trivially predictable; seed from the
    // system CSPRNG so a LAN peer can't reconstruct it from the clock.
    int generatePin()
    {
        u32 r   = 0;
        bool ok = false;
        if (R_SUCCEEDED(psInit())) {
            ok = R_SUCCEEDED(PS_GenerateRandomBytes(&r, sizeof(r)));
            psExit();
        }
        if (!ok) {
            // Fallback: the high-resolution tick is far less predictable than the
            // second-granularity clock the old srand(time) leaked.
            r = (u32)(svcGetSystemTick() ^ osGetTime());
        }
        return 1000 + (int)(r % 9000);
    }
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

    void collectFiles(FS_Archive arch, const std::u16string& root, const std::u16string& sub, std::vector<SendFile>& out,
        std::vector<std::string>* outDirs = nullptr)
    {
        std::u16string current = root;
        if (!current.empty() && current.back() != u'/') {
            current += StringUtils::UTF8toUTF16("/");
        }
        current += sub;
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
                SendFile entry;
                entry.absPath = StringUtils::UTF16toUTF8(abs);
                entry.relPath = rel;
                entry.size    = input.size();
                input.close();
                out.push_back(entry);
            }
        }
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

    // Reads a bounded head window off the front of the streamed body so
    // TransferProto::parseMultipart can locate the parts without scanning the
    // (large) payload.
    bool readBodyHead(const std::u16string& bodyPath, u64 bodyLen, std::string& outHead, std::string& outError)
    {
        FSStream f(Archive::sdmc(), bodyPath, FS_OPEN_READ);
        if (!f.good()) {
            outError = "Failed to open upload body.";
            return false;
        }
        const u32 headWindow = 64 * 1024;
        u32 headLen          = (u32)(bodyLen < headWindow ? bodyLen : headWindow);
        outHead.resize(headLen);
        if (headLen > 0 && f.read(&outHead[0], headLen) != headLen) {
            f.close();
            outError = "Failed to read upload body.";
            return false;
        }
        f.close();
        return true;
    }

    // ByteReader over the streamed body, positioned at the file part's start.
    // TransferProto::extractZip owns the length accounting.
    struct BodyReader : TransferProto::ByteReader {
        FSStream input;
        explicit BodyReader(const std::u16string& path, u64 startOffset) : input(Archive::sdmc(), path, FS_OPEN_READ)
        {
            if (input.good()) {
                input.offset((u32)startOffset);
            }
        }
        bool good() { return input.good(); }
        ~BodyReader() override
        {
            if (input.good()) {
                input.close();
            }
        }
        size_t read(void* dst, size_t n) override { return input.good() ? input.read(dst, (u32)n) : 0; }
    };

    // ExtractSink writing under a u16string destination root via FSStream.
    struct FsExtractSink : TransferProto::ExtractSink {
        std::u16string destRoot;
        std::optional<FSStream> output;
        explicit FsExtractSink(std::u16string root) : destRoot(std::move(root)) {}
        bool makeDir(const std::string& relPath) override
        {
            std::u16string dirPath = destRoot + StringUtils::UTF8toUTF16(relPath.c_str());
            if (!io::directoryExists(Archive::sdmc(), dirPath)) {
                io::createDirectory(Archive::sdmc(), dirPath);
            }
            return true;
        }
        bool beginFile(const std::string& relPath, uint32_t sizeHint) override
        {
            ensureDirectoryPath(destRoot, relPath);
            std::u16string outPath = destRoot + StringUtils::UTF8toUTF16(relPath.c_str());
            output.emplace(Archive::sdmc(), outPath, FS_OPEN_WRITE, sizeHint);
            return output->good();
        }
        bool writeFile(const void* data, size_t n) override
        {
            output->write(data, (u32)n);
            return true;
        }
        void endFile() override
        {
            if (output) {
                output->close();
                output.reset();
            }
        }
    };

    // FileReader opening backup files (UTF-8 abs path) via FSStream for the send.
    struct FsFileReader : TransferProto::FileReader {
        std::optional<FSStream> stream;
        bool open(const std::string& absPath) override
        {
            stream.emplace(Archive::sdmc(), StringUtils::UTF8toUTF16(absPath.c_str()), FS_OPEN_READ);
            return stream->good();
        }
        size_t read(void* dst, size_t n) override { return stream ? stream->read(dst, (u32)n) : 0; }
        void close() override
        {
            if (stream) {
                stream->close();
                stream.reset();
            }
        }
    };

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

    Server::HttpResponse handleUpload(const Server::UploadRequest& req)
    {
        auto cleanup      = []() { TransferStatus::end(); };
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

        std::u16string bodyPath = StringUtils::UTF8toUTF16(req.bodyPath.c_str());
        std::string metaJson;
        u64 fileOffset = 0;
        u64 fileLen    = 0;
        std::string error;
        std::string head;
        if (!readBodyHead(bodyPath, req.bodyLength, head, error) ||
            !parseMultipart(head, req.bodyLength, boundary, metaJson, fileOffset, fileLen, error)) {
            cleanup();
            return {400, "application/json", "{\"ok\":false,\"error\":\"Bad upload\"}"};
        }

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

        std::u16string basePath = Paths::rootFor(dataType == "extdata");

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
            // Name the folder exactly as TitleProbe would once this title is
            // installed ("0x%05X " + sanitized name), so the received backup
            // reconciles with the title's real folder instead of stranding in a
            // titleId-named one. Fall back to a bare name only when we have no id.
            std::string safeName         = titleName.empty() ? "Unknown" : titleName;
            std::u16string sanitizedName = StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(safeName.c_str()));
            std::u16string folder        = (tid != 0) ? Paths::ctrFolderName(tid, sanitizedName) : sanitizedName;
            destRoot                     = basePath + folder;
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

        if (isZip) {
            TransferStatus::beginNetwork("Extracting package", fileLen);

            std::string extractError;
            BodyReader reader(bodyPath, fileOffset);
            FsExtractSink sink(backupRoot);
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
                io::deleteFolderRecursively(Archive::sdmc(), backupRoot);
                cleanup();
                std::string message = extractError.empty() ? "Failed to extract package." : extractError;
                // Build via nlohmann so a message with a quote/backslash can't
                // produce malformed JSON (the sender would then show "no response"
                // instead of the real cause).
                nlohmann::json err;
                err["ok"]    = false;
                err["error"] = message;
                return {500, "application/json", err.dump()};
            }
            TransferStatus::setBytesDone(fileLen);
        }
        else {
            std::string safeFileNameUtf8 = sanitizeFileName(meta.value("fileName", ""));
            if (safeFileNameUtf8.empty()) {
                safeFileNameUtf8 = "received.bin";
            }
            std::u16string safeFileName = StringUtils::UTF8toUTF16(safeFileNameUtf8.c_str());
            std::u16string outputPath   = backupRoot + safeFileName;

            // Copy the file-part byte range straight out of the streamed body.
            FSStream input(Archive::sdmc(), bodyPath, FS_OPEN_READ);
            FSStream output(Archive::sdmc(), outputPath, FS_OPEN_WRITE, (u32)fileLen);
            if (!input.good() || !output.good()) {
                if (input.good()) {
                    input.close();
                }
                if (output.good()) {
                    output.close();
                }
                Logging::error("Failed to open {} to store the received file (source result 0x{:08X}, destination result 0x{:08X}).",
                    StringUtils::UTF16toUTF8(outputPath), (u32)input.result(), (u32)output.result());
                io::deleteFolderRecursively(Archive::sdmc(), backupRoot);
                cleanup();
                return {500, "application/json", "{\"ok\":false,\"error\":\"Failed to store file\"}"};
            }
            input.offset((u32)fileOffset);
            static const u32 kBuf = 0x40000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            u64 remaining = fileLen;
            bool copyOk   = true;
            while (remaining > 0) {
                if (TransferStatus::cancelRequested()) {
                    copyOk = false;
                    break;
                }
                u32 chunk = remaining > kBuf ? kBuf : (u32)remaining;
                u32 rd    = input.read(buf.get(), chunk);
                if (rd == 0) {
                    copyOk = false;
                    break;
                }
                output.write(buf.get(), rd);
                remaining -= rd;
                TransferStatus::addBytesDone(rd);
            }
            input.close();
            output.close();
            if (!copyOk) {
                Logging::error(
                    "Failed to store the received file {} ({} of {} bytes missing).", StringUtils::UTF16toUTF8(outputPath), remaining, (u64)fileLen);
                io::deleteFolderRecursively(Archive::sdmc(), backupRoot);
                cleanup();
                return {500, "application/json", "{\"ok\":false,\"error\":\"Failed to store file\"}"};
            }
            Logging::info("Received {} bytes into {}.", (u64)fileLen, StringUtils::UTF16toUTF8(outputPath));
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
    for (const char* leftover : {TEMP_UPLOAD, TEMP_ZIP_RECV_LEGACY}) {
        std::u16string p = StringUtils::UTF8toUTF16(leftover);
        if (io::fileExists(Archive::sdmc(), p)) {
            FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, p.data()));
            Logging::info("Removed leftover {} from a previous run.", leftover);
        }
    }

    const std::u16string root = StringUtils::UTF8toUTF16("/3ds/Checkpoint/");
    Directory dir(Archive::sdmc(), root);
    if (!dir.good()) {
        return;
    }
    const std::string prefix = "transfer_send_";
    const std::string suffix = ".zip";
    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            continue;
        }
        std::string name = StringUtils::UTF16toUTF8(dir.entry(i));
        if (name.size() > prefix.size() + suffix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            std::u16string path = root + dir.entry(i);
            FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
            Logging::info("Removed leftover {} from a previous run.", name);
        }
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

    g_failedAuthAttempts.store(0);
    // A PIN configured in Settings wins, so a PC-side script can be pointed at
    // the console without reading the screen first; anything not exactly 4
    // digits (including the empty default) falls back to a fresh random one.
    std::string token = Configuration::getInstance().defaultReceivePin();
    if (!TransferProto::validPin(token)) {
        token = StringUtils::format("%04d", generatePin());
    }
    std::string ip = Server::getAddress();
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
    // Every exit path must clear the transfer modal; the scope guard makes
    // that hold for each early return below.
    struct StatusGuard {
        ~StatusGuard() { TransferStatus::end(); }
    } statusGuard;

    std::vector<SendFile> files;
    std::vector<std::string> dirs;
    collectFiles(Archive::sdmc(), backupPath, StringUtils::UTF8toUTF16(""), files, &dirs);
    if (files.empty() && dirs.empty()) {
        return SendOutcome{false, SendStage::EmptyBackup, ""};
    }

    // Multi-file backups are zipped on the fly by sendZipStream — no staged
    // temp zip on SD; the exact zip size is known upfront because entries are
    // store-only.
    bool isZip = files.size() != 1 || !dirs.empty();
    std::u16string payloadPath;
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
        payloadPath           = StringUtils::UTF8toUTF16(entry.absPath.c_str());
        payloadName           = entry.relPath;
        payloadSize           = entry.size;
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

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return SendOutcome{false, SendStage::Resolve, ""};
    }
    // Connect while still blocking. The 3DS SOC layer does not reliably report
    // EINPROGRESS / SO_ERROR for a non-blocking connect (server.cpp forces its
    // client socket blocking for the same reason), so the poll(POLLOUT)+SO_ERROR
    // dance bailed out immediately on every attempt. Only the reachable-host case
    // matters here; the send/recv poll timeouts below still guard a stalled peer.
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        return SendOutcome{false, SendStage::Connect, ""};
    }

    // Non-blocking only from here on; every send/recv below is poll-gated so a
    // half-open peer can't block forever.
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    std::string header = StringUtils::format("POST /transfer/upload HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n", ip.c_str(), port);
    header += StringUtils::format("X-CP-Token: %s\r\n", token.c_str());
    header += StringUtils::format("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    header += StringUtils::format("Content-Length: %u\r\n\r\n", contentLength);

    bool ok = sendAll(sock, header.data(), header.size()) && sendAll(sock, partMeta.data(), partMeta.size()) &&
              sendAll(sock, partFileHeader.data(), partFileHeader.size());

    if (ok && isZip) {
        bool cancelled = false;
        SocketByteSink sink(sock);
        FsFileReader reader;
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
        FSStream input(Archive::sdmc(), payloadPath, FS_OPEN_READ);
        if (!input.good()) {
            ok = false;
        }
        else {
            static const u32 kBuf = 0x40000;
            std::unique_ptr<u8[]> buf(new u8[kBuf]);
            while (!input.eof()) {
                if (TransferStatus::cancelRequested()) {
                    input.close();
                    // The scope guard closes the socket, dropping the connection,
                    // which the receiver treats as an aborted request.
                    return SendOutcome{false, SendStage::Cancelled, ""};
                }
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
