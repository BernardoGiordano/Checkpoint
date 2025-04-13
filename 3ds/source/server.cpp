/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2025 Bernardo Giordano, Admiral Fish, piepie62
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
#include "thread.hpp"
#include <3ds.h>
#include <cstring>
#include <map>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
    static const int SERVER_PORT   = 8000;
    std::atomic_flag serverRunning = ATOMIC_FLAG_INIT;
    s32 serverSocket               = -1;
    bool isRunning                 = false;
    std::string serverAddress;

    std::map<std::string, Server::HttpHandler> handlers;

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

    static void handleHttpRequest(s32 clientSocket)
    {
        char buffer[1024] = {0};
        recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        std::string request(buffer);
        std::string path = extractPath(request);
        auto it          = handlers.find(path);
        if (it != handlers.end()) {
            Server::HttpResponse response = it->second(path, request);
            std::string header            = "HTTP/1.1 " + std::to_string(response.statusCode);
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

        isRunning = true;
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
    }
}

void Server::registerHandler(const std::string& path, Server::HttpHandler handler)
{
    handlers[path] = handler;
    Logging::info("Registered HTTP handler for path {}", path);
}

void Server::unregisterHandler(const std::string& path)
{
    handlers.erase(path);
    Logging::info("Unregistered HTTP handler for path {}", path);
}

bool Server::isRunning(void)
{
    return isRunning;
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
    serverRunning.clear();

    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }

    handlers.clear();

    Logging::trace("HTTP server stopped");
}