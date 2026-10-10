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

#include <cstdint>
#include <functional>
#include <string>

namespace Server {
    struct HttpResponse {
        int statusCode;
        std::string contentType;
        std::string body;
    };

    using HttpHandler = std::function<HttpResponse(const std::string& path, const std::string& requestData)>;

    // A streamed upload: for a registered upload path the server writes the raw
    // request body to a temp file (instead of buffering it in RAM), so an upload
    // far larger than MAX_REQUEST_SIZE is handled without heap pressure. The
    // handler is handed the header block plus the body temp-file path and its
    // length; the server deletes the temp file once the handler returns.
    struct UploadRequest {
        std::string headers;  // request line + header block, without the trailing CRLFCRLF
        std::string bodyPath; // temp file holding the raw request body (SD-relative, e.g. /3ds/...)
        uint64_t bodyLength;  // declared Content-Length (bytes streamed to bodyPath)
    };

    using UploadHandler = std::function<HttpResponse(const UploadRequest&)>;

    // Binds and listens on `port` (Configuration::serverPort). The port is read
    // once here, so a change in Settings takes effect on the next launch.
    void init(uint16_t port);
    void exit(void);
    // Signals the network loop to stop (it exits within one iteration) without
    // tearing anything down. Needed at shutdown so the thread ends before
    // Threads::exit joins the pool; the socket cleanup in Server::exit runs later.
    void requestStop(void);
    bool isRunning(void);
    std::string getAddress(void);
    // Port the server is bound to, 0 if it never came up. Not necessarily
    // Configuration::serverPort: Settings may have changed that since launch.
    uint16_t getPort(void);

    void registerHandler(const std::string& path, HttpHandler handler);
    void unregisterHandler(const std::string& path);

    // Registers a streaming upload handler for `path`; the body is written to
    // `tmpPath` (SD-relative) before the handler runs. Unregistered via
    // unregisterHandler.
    void registerUploadHandler(const std::string& path, const std::string& tmpPath, UploadHandler handler);
}

#endif