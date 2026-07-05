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
#include "logging.hpp"
#include <switch.h>

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// A near-direct port of 3ds/source/server.cpp. The differences are the worker
// thread (libnx threadCreate instead of the 3DS thread pool) and dropping the
// network-transfer progress tracking the 3DS request reader does — the Switch
// only serves the read-only /logs endpoints today, which carry no request body.
namespace {
    constexpr int SERVER_PORT = 8000;
    // Hard upper bound on a single request we are willing to buffer in RAM, so a
    // malformed/malicious Content-Length cannot exhaust the heap.
    constexpr size_t MAX_REQUEST_SIZE = 32 * 1024 * 1024;
    // Per-recv idle timeout. The server is single-threaded, so without a bound a
    // stalled client would hang the receiver indefinitely.
    constexpr int RECV_TIMEOUT_MS = 15000;

    std::atomic_flag serverRunning = ATOMIC_FLAG_INIT;
    s32 serverSocket               = -1;
    std::atomic<bool> serverIsRunning{false};
    std::string serverAddress;

    Thread serverThread;
    bool threadValid = false;

    std::map<std::string, Server::HttpHandler> handlers;
    // handlers is mutated from the main thread (register/unregister) while the
    // worker thread looks handlers up, so guard it.
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

    size_t parseContentLength(const std::string& headers)
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

    std::string readRequest(s32 clientSocket, bool& outTooLarge)
    {
        outTooLarge = false;
        std::string data;
        data.reserve(4096);
        // Heap-allocated: a large recv chunk means far fewer syscalls/appends,
        // and it can't live on the worker's small stack.
        constexpr size_t RECV_CHUNK = 32 * 1024;
        std::vector<char> buffer(RECV_CHUNK);
        size_t headerEnd     = std::string::npos;
        size_t contentLength = 0;

        while (true) {
            struct pollfd pfd;
            pfd.fd      = clientSocket;
            pfd.events  = POLLIN;
            pfd.revents = 0;
            int ready   = poll(&pfd, 1, RECV_TIMEOUT_MS);
            if (ready <= 0) {
                break; // timed out or poll error: abandon this (stalled) request
            }
            ssize_t received = recv(clientSocket, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                break;
            }
            data.append(buffer.data(), received);

            if (headerEnd == std::string::npos) {
                if (data.size() > MAX_REQUEST_SIZE) {
                    outTooLarge = true;
                    break;
                }
                headerEnd = data.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    contentLength = parseContentLength(data.substr(0, headerEnd));
                    if (contentLength > MAX_REQUEST_SIZE) {
                        outTooLarge = true;
                        break;
                    }
                    if (contentLength == 0) {
                        break;
                    }
                    data.reserve(headerEnd + 4 + contentLength);
                }
            }

            if (headerEnd != std::string::npos && data.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        }

        return data;
    }

    void handleHttpRequest(s32 clientSocket)
    {
        bool tooLarge       = false;
        std::string request = readRequest(clientSocket, tooLarge);
        if (tooLarge) {
            std::string body = "{\"ok\":false,\"error\":\"Payload too large\"}";
            std::string header =
                "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.length()) + "\r\n\r\n";
            send(clientSocket, header.c_str(), header.length(), 0);
            send(clientSocket, body.c_str(), body.length(), 0);
            return;
        }

        std::string path = extractPath(request);
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
            Server::HttpResponse response = handler(path, request);
            std::string header            = "HTTP/1.1 " + std::to_string(response.statusCode);
            header += (response.statusCode == 200 ? " OK" : (response.statusCode == 404 ? " Not Found" : " Error"));
            header += "\r\nContent-Type: " + response.contentType;
            header += "\r\nContent-Length: " + std::to_string(response.body.length());
            header += "\r\n\r\n";
            send(clientSocket, header.c_str(), header.length(), 0);
            send(clientSocket, response.body.c_str(), response.body.length(), 0);
        }
        else {
            std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    }

    void networkLoop(void*)
    {
        struct sockaddr_in clientAddr;
        u32 clientLen = sizeof(clientAddr);

        fcntl(serverSocket, F_SETFL, fcntl(serverSocket, F_GETFL, 0) | O_NONBLOCK);

        serverIsRunning.store(true);
        while (serverRunning.test_and_set()) {
            s32 clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientSocket >= 0) {
                fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) & ~O_NONBLOCK);
                handleHttpRequest(clientSocket);
                close(clientSocket);
            }
            else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                Logging::error("accept() failed on the log server socket with errno {}.", errno);
            }
            svcSleepThread(100'000'000ULL); // 100ms; don't peg a core
        }

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

std::string Server::getAddress(void)
{
    return serverAddress;
}

void Server::init()
{
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (serverSocket < 0) {
        Logging::error("Failed to create log server socket with error {}: {}", errno, strerror(errno));
        return;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = gethostid();

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
        Logging::error("Failed to bind log server to port {} with error {}", SERVER_PORT, errno);
        close(serverSocket);
        serverSocket = -1;
        return;
    }

    if (listen(serverSocket, 5) != 0) {
        Logging::error("Failed to listen on log server socket with error {}: {}", errno, strerror(errno));
        close(serverSocket);
        serverSocket = -1;
        return;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(serverAddr.sin_addr), ipStr, INET_ADDRSTRLEN);

    serverRunning.test_and_set();
    // Same priority/core as the FTP + copy workers; the accept loop is IO-bound.
    if (R_SUCCEEDED(threadCreate(&serverThread, networkLoop, nullptr, nullptr, 0x8000, 0x2C, -2)) && R_SUCCEEDED(threadStart(&serverThread))) {
        threadValid   = true;
        serverAddress = "http://" + std::string(ipStr) + ":" + std::to_string(SERVER_PORT);
        Logging::info("Log server listening on {}", serverAddress);
    }
    else {
        // Could not spawn the worker: nothing will accept connections, so leave
        // the address empty and free the socket.
        serverRunning.clear();
        close(serverSocket);
        serverSocket = -1;
    }
}

void Server::exit()
{
    serverRunning.clear();
    if (threadValid) {
        threadWaitForExit(&serverThread);
        threadClose(&serverThread);
        threadValid = false;
    }
    serverIsRunning.store(false);

    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers.clear();
    }

    Logging::trace("Log server stopped");
}
