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

#include "ftpserver.hpp"
#include "configuration.hpp"
#include "ftpServer.h"
#include "logging.hpp"
#include <memory>

namespace {
    // ftpd's core owns its own background thread (see 3rd-party/ftpd), so there
    // is no loop to drive from here: creating the server starts it, destroying
    // it stops and joins it. This replaced networkLoop() in main.cpp.
    UniqueFtpServer server;
}

void FTPServer::init(void)
{
    // The predicate is polled by the server loop. Off means "close the listen
    // socket and drop every session" rather than "keep the port open but stop
    // pumping it".
    server = FtpServer::create(FTP_PORT, []() { return Configuration::getInstance().isFTPEnabled(); });
    Logging::info("FTP server created on port {}", FTP_PORT);
}

void FTPServer::requestStop(void)
{
    // Nothing to raise: exit() joins the core's thread itself. Kept so the
    // teardown call sites in main.cpp / util.cpp stay symmetric with Server's.
}

void FTPServer::exit(void)
{
    // Destroying the server raises its quit flag and joins its thread, so this
    // must run before the singletons the loop touches (Configuration, Logging)
    // are torn down.
    server.reset();
    Logging::trace("FTP server stopped");
}

std::string FTPServer::getAddress(void)
{
    if (!server) {
        return {};
    }

    const std::string address = server->address();
    return address.empty() ? std::string() : "ftp://" + address;
}
