/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

#include "util.hpp"

void servicesExit(void)
{
    // debug
    // socketExit();
    nsExit();
    Account::exit();
    hbkbd::exit();
    Gui::exit();
    fontExit();
    plExit();
}

Result servicesInit(void)
{
    Result res = 0;
    res = io::createDirectory("sdmc:/switch");
    res = io::createDirectory("sdmc:/switch/Checkpoint");
    res = io::createDirectory("sdmc:/switch/Checkpoint/saves");
    if (R_FAILED(res))
    {
        return res;
    }

    if (R_FAILED(res = plInitialize()))
    {
        return res;
    }

    if (!fontInitialize())
    {
        return -1;
    }

    if (R_FAILED(res = Account::init()))
    {
        return res;
    }

    if (R_FAILED(res = nsInitialize()))
    {
        return res;
    }  
    
    Gui::init();
    hbkbd::init();

    // debug
    // static int sock = -1;
    // struct sockaddr_in srv_addr;
    // int ret = socketInitializeDefault();
    // if (ret != 0) {
    //     Gui::createError(ret, "socketInitializeDefault");
    // } else {
	//     sock = socket(AF_INET, SOCK_STREAM, 0);
	//     if (!sock) {
    //         Gui::createError(-3, "socket");
    // }

	//     bzero(&srv_addr, sizeof srv_addr);
    //     srv_addr.sin_family=AF_INET;
    //     srv_addr.sin_addr = __nxlink_host;
    //     srv_addr.sin_port=htons(NXLINK_CLIENT_PORT);

	//     ret = connect(sock, (struct sockaddr *) &srv_addr, sizeof(srv_addr));
    // 	   if (ret != 0) {
    //         Gui::createError(ret, "connect");
    // 	   } else {
	// 	       fflush(stdout);
	// 	       dup2(sock, STDOUT_FILENO);
    // 	   }
    // }

    return 0;
}