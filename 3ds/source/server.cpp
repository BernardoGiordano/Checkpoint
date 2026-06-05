/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2025 Bernardo Giordano
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
#include "main.hpp"
#include "logging.hpp"
#include "thread.hpp"
#include <3ds.h>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

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
    s32 serverSocket               = -1;
    std::atomic<bool> serverIsRunning{false};
    std::string serverAddress;

    std::map<std::string, Server::HttpHandler> handlers;
    // handlers is mutated from the main thread (register/unregister on receiver
    // start/stop) while the network thread looks handlers up, so guard it.
    std::mutex handlersMutex;

    std::string extractPath(const std::string& request)
    {
        size_t pathStart = request.find(" ") + 1;
        if (pathStart != std::string::npos) {
            size_t pathEnd = request.find(" ", pathStart);
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

    static std::string readRequest(s32 clientSocket, bool& outTooLarge)
    {
        outTooLarge = false;
        std::string data;
        data.reserve(4096);
        char buffer[2048];
        ssize_t received     = 0;
        size_t headerEnd     = std::string::npos;
        size_t contentLength = 0;
        std::string path;
        bool trackTransfer = false;

        while (true) {
            struct pollfd pfd;
            pfd.fd      = clientSocket;
            pfd.events  = POLLIN;
            pfd.revents = 0;
            int ready   = poll(&pfd, 1, RECV_TIMEOUT_MS);
            if (ready <= 0) {
                // Timed out or poll error: abandon this (possibly stalled) request.
                break;
            }
            received = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                break;
            }
            data.append(buffer, received);

            if (headerEnd == std::string::npos) {
                // No header terminator yet: guard against a client that never
                // sends one (or buries it past the cap) and grows us unbounded.
                if (data.size() > MAX_REQUEST_SIZE) {
                    outTooLarge = true;
                    break;
                }
                headerEnd = data.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    contentLength = parseContentLength(data.substr(0, headerEnd));
                    path          = extractPath(data.substr(0, headerEnd));
                    if (contentLength > MAX_REQUEST_SIZE) {
                        outTooLarge = true;
                        break;
                    }
                    if (path == "/transfer/upload") {
                        trackTransfer = true;
                        g_transferIsNetwork = true;
                        transferSetMode("Downloading backup");
                        transferSetProgress(0, contentLength);
                        g_isTransferringFile = true;
                    }
                    if (contentLength == 0) {
                        break;
                    }
                }
            }

            if (headerEnd != std::string::npos) {
                // contentLength is bounded by MAX_REQUEST_SIZE above, so this read
                // is bounded too: we stop as soon as the declared body has arrived.
                size_t totalNeeded = headerEnd + 4 + contentLength;
                if (trackTransfer && data.size() > headerEnd + 4) {
                    transferSetDone(data.size() - (headerEnd + 4));
                }
                if (data.size() >= totalNeeded) {
                    break;
                }
            }
        }

        return data;
    }

    static void handleHttpRequest(s32 clientSocket)
    {
        bool tooLarge        = false;
        std::string request  = readRequest(clientSocket, tooLarge);
        if (tooLarge) {
            // Reset any transfer UI state we may have set while reading headers.
            g_isTransferringFile = false;
            g_transferIsNetwork  = false;
            std::string body   = "{\"ok\":false,\"error\":\"Payload too large\"}";
            std::string header = "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\nContent-Length: " +
                                 std::to_string(body.length()) + "\r\n\r\n";
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
            std::string header = "HTTP/1.1 " + std::to_string(response.statusCode);
            header += (response.statusCode == 200 ? " OK" : (response.statusCode == 404 ? " Not Found" : " Error"));
            header += "\r\nContent-Type: " + response.contentType;
            header += "\r\nContent-Length: " + std::to_string(response.body.length());
            header += "\r\n\r\n";

            send(clientSocket, header.c_str(), header.length(), 0);
            send(clientSocket, response.body.c_str(), response.body.length(), 0);
        }
        else {
            // 404 for unregistered endpoints
            std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    }

    static void networkLoop()
    {
        struct sockaddr_in clientAddr;
        u32 clientLen = sizeof(clientAddr);

        // Set server socket to non-blocking
        fcntl(serverSocket, F_SETFL, fcntl(serverSocket, F_GETFL, 0) | O_NONBLOCK);

        serverIsRunning.store(true);
        while (serverRunning.test_and_set()) {
            s32 clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket >= 0) {
                // Set client socket to blocking for simpler sending
                fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) & ~O_NONBLOCK);
                handleHttpRequest(clientSocket);
                close(clientSocket);
            }
            else if (errno != EAGAIN) {
                // FIXME do something
            }

            // Prevent 100% CPU usage
            svcSleepThread(100000000); // 100ms
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

void Server::unregisterHandler(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers.erase(path);
    }
    Logging::info("Unregistered HTTP handler for path {}", path);
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

void Server::exit()
{
    serverIsRunning.store(false);
    serverRunning.clear();

    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(handlersMutex);
        handlers.clear();
    }

    Logging::trace("HTTP server stopped");
}
