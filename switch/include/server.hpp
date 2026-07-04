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

#ifndef SERVER_HPP
#define SERVER_HPP

#include <functional>
#include <string>

// Minimal HTTP/1.1 server over a raw BSD socket, mirroring the 3DS Server API
// so the shared logging module registers the same /logs endpoints on both
// platforms. Paths are dispatched to registered handlers on a worker thread.
namespace Server {
    struct HttpResponse {
        int statusCode;
        std::string contentType;
        std::string body;
    };

    using HttpHandler = std::function<HttpResponse(const std::string& path, const std::string& requestData)>;

    void init(void);
    void exit(void);
    // Signals the accept loop to stop; it exits within one iteration. exit()
    // joins the worker and tears the socket down.
    void requestStop(void);
    bool isRunning(void);
    // "http://<console-ip>:8000" once listening, empty otherwise.
    std::string getAddress(void);

    void registerHandler(const std::string& path, HttpHandler handler);
    void unregisterHandler(const std::string& path);
}

#endif
