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

#ifndef FTPSERVER_HPP
#define FTPSERVER_HPP

#include <string>

// mtheall's ftpd (3rd-party/ftpd), driven on the background thread the core
// spawns for itself. The listen socket is bound by that thread once the network
// is up and the FTP toggle is on; flipping the toggle off closes it again.
namespace FTPServer {
    // Listen port. Unchanged from the previous FTP core so existing bookmarks
    // and firewall rules keep working.
    inline constexpr int FTP_PORT = 50000;

    // Creates the server and starts its thread. Call once, after socInit
    // succeeds.
    void init(void);

    // No-op: exit() stops and joins the core's own thread. Kept for symmetry
    // with the other loop-owning services in the teardown path.
    void requestStop(void);

    // Stops the server and joins its thread. Must run before socketExit() and
    // before Configuration / Logging are torn down, since the loop reads all
    // three.
    void exit(void);

    // "ftp://<ip>:50000" while listening, otherwise empty (FTP off, or no
    // network yet).
    std::string getAddress(void);
}

#endif
