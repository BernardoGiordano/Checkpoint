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

#include "pksmbridge.hpp"

bool isPKSMBridgeTitle(u64 id) {
    if (id == 0x0100187003A36000 || id == 0x010003F003A34000)
    {
        return true;
    }
    return false;
}

bool validateIpAddress(const std::string& ip)
{
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 0;
}

void sendToPKSMBrigde(size_t index, u128 uid)
{
    const size_t cellIndex = Gui::index(CELLS);
    if (cellIndex == 0 || !Gui::askForConfirmation("Send save to PKSM?"))
    {
        return;
    }

    // load data
    Title title;
    getTitle(title, uid, index);
    std::string srcPath = title.fullPath(cellIndex) + "/";
    std::ifstream save(srcPath + "savedata.bin", std::ios::binary | std::ios::ate);
    std::streamsize size = save.tellg();
    save.seekg(0, std::ios::beg);
    char* data = new char[size];
    save.read(data, size);

    // get server address
    std::pair<bool, std::string> ipaddress = KeyboardManager::get().keyboard("192.168.1.16");
    if (!ipaddress.first || !validateIpAddress(ipaddress.second))
    {
        Gui::createError(-1, "Invalid IP address.");
        delete[] data;
        socketExit();
        return;
    }

    // send via UDP
    socketInitializeDefault();
    
    int fd;
    struct sockaddr_in servaddr;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        Gui::createError((Result)fd, "Socket creation failed.");
        close(fd);
        delete[] data;
        socketExit();
        return;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PKSM_PORT);
    servaddr.sin_addr.s_addr = inet_addr(ipaddress.second.c_str());

    if (connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
        Gui::createError((Result)fd, "Socket connection failed.");
        close(fd);
        delete[] data;
        socketExit();
        return;
    }

    int total = 0;
    int chunk = 1024;
    int n;
    while (total < size) {
        int tosend = size - total > chunk ? chunk : size - total;
        n = send(fd, data + total, tosend, 0);
        if (n == -1) break;
        total += n;
    }
    if (total == size)
    {
        Gui::createInfo("Success!", "Data sent correctly.");
    }
    else
    {
        Gui::createError(-1, "Failed to send data.");
    }

    close(fd);
    delete[] data;
    socketExit();
}