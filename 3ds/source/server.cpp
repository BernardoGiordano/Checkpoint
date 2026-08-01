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

#include "server.hpp"
#include "archive.hpp"
#include "common.hpp"
#include "fsstream.hpp"
#include "i18n.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "thread.hpp"
#include "transferstatus.hpp"
#include "util.hpp"
#include <3ds.h>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
    static const int SERVER_PORT = 8000;
    // Hard upper bound on a single request we are willing to buffer in RAM.
    // The whole request is held in memory, so this caps worst-case allocation
    // and prevents a malformed/malicious Content-Length from exhausting the heap.
    static const size_t MAX_REQUEST_SIZE = 32 * 1024 * 1024;
    std::atomic_flag serverRunning       = ATOMIC_FLAG_INIT;
    s32 serverSocket                     = -1;
    std::atomic<bool> serverIsRunning{false};
    std::string serverAddress;

    // Cap on the header block alone (the upload body is streamed to SD, so it is
    // not bounded by this). Guards against a client that never sends a header
    // terminator and grows us unbounded.
    static const size_t MAX_HEADER_SIZE = 128 * 1024;

    std::map<std::string, Server::HttpHandler> handlers;
    // Streaming upload handlers keyed by path: {temp body file path, handler}.
    std::map<std::string, std::pair<std::string, Server::UploadHandler>> uploadHandlers;
    // handlers/uploadHandlers are mutated from the main thread (register/unregister
    // on receiver start/stop) while the network thread looks them up, so guard both.
    std::mutex handlersMutex;

    std::string extractPath(const std::string& request)
    {
        size_t sp = request.find(" ");
        if (sp != std::string::npos) {
            size_t pathStart = sp + 1;
            size_t pathEnd   = request.find(" ", pathStart);
            if (pathEnd != std::string::npos) {
                return request.substr(pathStart, pathEnd - pathStart);
            }
        }
        return "";
    }

    static size_t parseContentLength(const std::string& headers)
    {
        size_t pos = headers.find("Content-Length:");
        if (pos == std::string::npos) {
            pos = headers.find("Content-length:");
        }
        if (pos == std::string::npos) {
            return 0;
        }
        pos += strlen("Content-Length:");
        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
            pos++;
        }
        size_t end = headers.find("\r\n", pos);
        if (end == std::string::npos) {
            end = headers.size();
        }
        return (size_t)strtoul(headers.substr(pos, end - pos).c_str(), nullptr, 10);
    }

    // Per-recv idle timeout. The server is single-threaded, so without a bound a
    // stalled (or malicious) client would hang the whole receiver indefinitely.
    // The 3DS SOC layer doesn't support SO_RCVTIMEO, so we gate recv with poll.
    static const int RECV_TIMEOUT_MS = 15000;

    // Poll in short slices instead of one long wait so a cancel request is noticed
    // within a second even when the sender has stalled; idleMs accumulates across
    // empty slices to preserve the overall idle timeout. Returns bytes read (>0),
    // 0 on a clean close or idle timeout, or -1 on a cancel/poll error to abandon.
    static const int POLL_SLICE_MS = 1000;

    static ssize_t pollRecv(s32 sock, char* buf, size_t len, int& idleMs, bool trackCancel)
    {
        while (true) {
            if (trackCancel && TransferStatus::cancelRequested()) {
                return -1;
            }
            struct pollfd pfd;
            pfd.fd      = sock;
            pfd.events  = POLLIN;
            pfd.revents = 0;
            int ready   = poll(&pfd, 1, POLL_SLICE_MS);
            if (ready < 0) {
                return -1;
            }
            if (ready == 0) {
                idleMs += POLL_SLICE_MS;
                if (idleMs >= RECV_TIMEOUT_MS) {
                    return 0;
                }
                continue;
            }
            idleMs = 0;
            return recv(sock, buf, len, 0);
        }
    }

    static void sendResponse(s32 clientSocket, const Server::HttpResponse& response)
    {
        std::string header = "HTTP/1.1 " + std::to_string(response.statusCode);
        header += (response.statusCode == 200 ? " OK" : (response.statusCode == 404 ? " Not Found" : " Error"));
        header += "\r\nContent-Type: " + response.contentType;
        header += "\r\nContent-Length: " + std::to_string(response.body.length());
        header += "\r\n\r\n";
        send(clientSocket, header.c_str(), header.length(), 0);
        send(clientSocket, response.body.c_str(), response.body.length(), 0);
    }

    // Streams the body of an upload request to `tmpPath`, tracking progress and
    // honouring a receive cancel. `leftover` is the body already read while
    // parsing the header. Returns true when the full body was written.
    static bool streamBodyToFile(s32 clientSocket, const std::u16string& tmpPath, size_t contentLength, const char* leftover, size_t leftoverLen)
    {
        FSStream out(Archive::sdmc(), tmpPath, FS_OPEN_WRITE, (u32)contentLength);
        if (!out.good()) {
            Logging::error("Failed to open upload temp file (0x{:08X}).", (u32)out.result());
            return false;
        }

        TransferStatus::beginNetwork(i18n::t("transfer.downloading"), contentLength);

        size_t written = 0;
        if (leftoverLen > 0) {
            size_t toWrite = leftoverLen > contentLength ? contentLength : leftoverLen;
            out.write(leftover, (u32)toWrite);
            written += toWrite;
            TransferStatus::setBytesDone(written);
        }

        constexpr size_t RECV_CHUNK = 32 * 1024;
        std::unique_ptr<char[]> buffer(new char[RECV_CHUNK]);
        int idleMs = 0;
        bool ok    = true;
        while (written < contentLength) {
            ssize_t received = pollRecv(clientSocket, buffer.get(), RECV_CHUNK, idleMs, true);
            if (received <= 0) {
                ok = false; // clean close, idle timeout, or cancel: incomplete body
                break;
            }
            size_t toWrite = (size_t)received;
            if (written + toWrite > contentLength) {
                toWrite = contentLength - written;
            }
            out.write(buffer.get(), (u32)toWrite);
            written += toWrite;
            TransferStatus::setBytesDone(written);
        }
        out.close();
        return ok && written == contentLength;
    }

    static void handleHttpRequest(s32 clientSocket)
    {
        // Read only up to and including the header terminator; the body is either
        // streamed to disk (upload paths) or read into RAM afterwards (others).
        std::string data;
        data.reserve(4096);
        constexpr size_t RECV_CHUNK = 32 * 1024;
        std::unique_ptr<char[]> buffer(new char[RECV_CHUNK]);
        size_t headerEnd = std::string::npos;
        int idleMs       = 0;

        while (true) {
            ssize_t received = pollRecv(clientSocket, buffer.get(), RECV_CHUNK, idleMs, false);
            if (received <= 0) {
                break;
            }
            data.append(buffer.get(), received);
            headerEnd = data.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                break;
            }
            if (data.size() > MAX_HEADER_SIZE) {
                break;
            }
        }
        if (headerEnd == std::string::npos) {
            return;
        }

        std::string headers  = data.substr(0, headerEnd);
        std::string path     = extractPath(headers);
        size_t contentLength = parseContentLength(headers);
        size_t bodyStart     = headerEnd + 4;

        // Streaming upload path.
        std::string tmpPath;
        Server::UploadHandler uploadHandler;
        bool isUpload = false;
        {
            std::lock_guard<std::mutex> lock(handlersMutex);
            auto it = uploadHandlers.find(path);
            if (it != uploadHandlers.end()) {
                tmpPath       = it->second.first;
                uploadHandler = it->second.second;
                isUpload      = true;
            }
        }
        if (isUpload) {
            std::u16string tmpU16 = StringUtils::UTF8toUTF16(tmpPath.c_str());
            const char* leftover  = data.data() + bodyStart;
            size_t leftoverLen    = data.size() - bodyStart;
            bool complete         = streamBodyToFile(clientSocket, tmpU16, contentLength, leftover, leftoverLen);
            if (!complete) {
                // Cancelled, stalled, or dropped mid-upload: drop the request and
                // clear the transfer UI; the handler never runs on a partial body.
                FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, tmpU16.data()));
                TransferStatus::end();
                Logging::info("Upload abandoned before the full body arrived.");
                return;
            }
            Server::UploadRequest req{headers, tmpPath, (uint64_t)contentLength};
            Server::HttpResponse response = uploadHandler(req);
            FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, tmpU16.data()));
            sendResponse(clientSocket, response);
            return;
        }

        // Non-upload request: buffer the (small) remainder in RAM.
        if (contentLength > MAX_REQUEST_SIZE) {
            std::string body = "{\"ok\":false,\"error\":\"Payload too large\"}";
            std::string header =
                "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.length()) + "\r\n\r\n";
            send(clientSocket, header.c_str(), header.length(), 0);
            send(clientSocket, body.c_str(), body.length(), 0);
            return;
        }
        while (data.size() < bodyStart + contentLength) {
            ssize_t received = pollRecv(clientSocket, buffer.get(), RECV_CHUNK, idleMs, false);
            if (received <= 0) {
                break;
            }
            data.append(buffer.get(), received);
        }

        Server::HttpHandler handler;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(handlersMutex);
            auto it = handlers.find(path);
            if (it != handlers.end()) {
                handler = it->second;
                found   = true;
            }
        }
        if (found) {
            sendResponse(clientSocket, handler(path, data));
        }
        else {
            std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    }

    static bool acceptWouldBlock(int err)
    {
        return err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS;
    }

    static void networkLoop()
    {
        // Set server socket to non-blocking
        fcntl(serverSocket, F_SETFL, fcntl(serverSocket, F_GETFL, 0) | O_NONBLOCK);

        int lastErrno        = 0;
        u32 suppressed       = 0;
        auto flushSuppressed = [&]() {
            if (suppressed > 0) {
                Logging::error("The previous accept() error repeated {} more times.", suppressed);
                suppressed = 0;
            }
            lastErrno = 0;
        };

        serverIsRunning.store(true);
        while (serverRunning.test_and_set()) {
            struct sockaddr_in clientAddr;
            u32 clientLen    = sizeof(clientAddr);
            s32 clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket >= 0) {
                flushSuppressed();
                // Set client socket to blocking for simpler sending
                fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) & ~O_NONBLOCK);
                handleHttpRequest(clientSocket);
                close(clientSocket);
            }
            else if (!acceptWouldBlock(errno)) {
                if (errno == lastErrno) {
                    suppressed++;
                }
                else {
                    flushSuppressed();
                    Logging::error("accept() failed on the HTTP server socket with errno {}.", errno);
                    lastErrno = errno;
                }
            }

            // Prevent 100% CPU usage
            svcSleepThread(100000000); // 100ms
        }

        flushSuppressed();
        serverIsRunning.store(false);
    }
}

void Server::registerHandler(const std::string& path, Server::HttpHandler handler)
{
    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers[path] = handler;
    }
    Logging::info("Registered HTTP handler for path {}", path);
}

void Server::unregisterHandler(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers.erase(path);
        uploadHandlers.erase(path);
    }
    Logging::info("Unregistered HTTP handler for path {}", path);
}

void Server::registerUploadHandler(const std::string& path, const std::string& tmpPath, Server::UploadHandler handler)
{
    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        uploadHandlers[path] = {tmpPath, handler};
    }
    Logging::info("Registered upload handler for path {}", path);
}

bool Server::isRunning(void)
{
    return serverIsRunning.load();
}

std::string Server::getAddress(void)
{
    return serverAddress;
}

void Server::init()
{
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (serverSocket < 0) {
        Logging::error("Failed to create socket with error {}: {}", errno, strerror(errno));
        return;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = gethostid();

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
        Logging::error("Failed to bind to port {} with error {}", SERVER_PORT, errno);
        close(serverSocket);
        serverSocket = -1;
        return;
    }

    if (listen(serverSocket, 5) != 0) {
        Logging::error("Failed to listen on socket with error {}: {}", errno, strerror(errno));
        close(serverSocket);
        serverSocket = -1;
        return;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(serverAddr.sin_addr), ipStr, INET_ADDRSTRLEN);

    serverRunning.test_and_set();
    Threads::create(networkLoop);
    serverAddress = "http://" + std::string(ipStr) + ":" + std::to_string(SERVER_PORT);
}

void Server::requestStop()
{
    serverRunning.clear();
}

void Server::exit()
{
    serverIsRunning.store(false);
    serverRunning.clear();
    serverIsRunning = false;

    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers.clear();
        uploadHandlers.clear();
    }

    Logging::trace("HTTP server stopped");
}
