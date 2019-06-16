/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

bool isPKSMBridgeTitle(u64 id)
{
    if (id == 0x0100187003A36000 || id == 0x010003F003A34000) {
        return true;
    }
    return false;
}

bool validateIpAddress(const std::string& ip)
{
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 0;
}

void sendToPKSMBrigde(size_t index, u128 uid, size_t cellIndex)
{
    if (cellIndex == 0 || !Gui::askForConfirmation("Send save to PKSM?")) {
        return;
    }

    auto systemKeyboardAvailable = KeyboardManager::get().isSystemKeyboardAvailable();
    if (!systemKeyboardAvailable.first) {
        Gui::showError(systemKeyboardAvailable.second, "System keyboard not accessible.");
    }

    // load data
    Title title;
    getTitle(title, uid, index);
    std::string srcPath = title.fullPath(cellIndex) + "/savedata.bin";
    FILE* save          = fopen(srcPath.c_str(), "rb");
    if (save == NULL) {
        return;
    }

    fseek(save, 0, SEEK_END);
    size_t size = ftell(save);
    rewind(save);
    char* data = new char[size];
    fread(data, 1, size, save);
    fclose(save);

    // get server address
    auto ipaddress = KeyboardManager::get().keyboard("Input PKSM IP address");
    if (!ipaddress.first || !validateIpAddress(ipaddress.second)) {
        Gui::showError(-1, "Invalid IP address.");
        delete[] data;
        return;
    }

    // send via TCP
    int fd;
    struct sockaddr_in servaddr;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        Gui::showError(errno, "Socket creation failed.");
        delete[] data;
        return;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(PKSM_PORT);
    servaddr.sin_addr.s_addr = inet_addr(ipaddress.second.c_str());

    if (connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        Gui::showError(errno, "Socket connection failed.");
        close(fd);
        delete[] data;
        return;
    }

    size_t total = 0;
    size_t chunk = 1024;
    int n;
    size_t tosend;
    while (total < size) {
        tosend = size - total > chunk ? chunk : size - total;
        n      = send(fd, data + total, tosend, 0);
        if (n == -1) {
            break;
        }
        total += n;
        fprintf(stderr, "Sent %lu bytes, %lu still missing\n", (unsigned long)total, (unsigned long)(size - total));
    }
    if (total == size) {
        Gui::showInfo("Data sent correctly.");
    }
    else {
        Gui::showError(errno, "Failed to send data.");
    }

    close(fd);
    delete[] data;
}

void recvFromPKSMBridge(size_t index, u128 uid, size_t cellIndex)
{
    if (cellIndex == 0 || !Gui::askForConfirmation("Receive save from PKSM?")) {
        return;
    }

    int fd;
    struct sockaddr_in servaddr;
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        Gui::showError(errno, "Socket creation failed.");
        return;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(PKSM_PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        Gui::showError(errno, "Socket bind failed.");
        close(fd);
        return;
    }
    if (listen(fd, 5) < 0) {
        Gui::showError(errno, "Socket listen failed.");
        close(fd);
        return;
    }

    int fdconn;
    int addrlen = sizeof(servaddr);
    if ((fdconn = accept(fd, (struct sockaddr*)&servaddr, (socklen_t*)&addrlen)) < 0) {
        Gui::showError(errno, "Socket accept failed.");
        close(fd);
        return;
    }

    size_t size = 0x100000;
    Title title;
    getTitle(title, uid, index);
    std::string srcPath = title.fullPath(cellIndex) + "/savedata.bin";
    FILE* save          = fopen(srcPath.c_str(), "wb");
    if (save == NULL) {
        return;
    }

    char* data = new char[size]();

    size_t total = 0;
    size_t chunk = 1024;
    int n;
    size_t torecv;
    while (total < size) {
        torecv = size - total > chunk ? chunk : size - total;
        n      = recv(fdconn, data + total, torecv, 0);
        if (n == -1) {
            break;
        }
        total += n;
        fprintf(stderr, "Recv %lu bytes, %lu still missing\n", (unsigned long)total, (unsigned long)(size - total));
    }
    if (total == size) {
        Gui::showInfo("Data received correctly.");
        fwrite(data, 1, size, save);
    }
    else {
        Gui::showError(errno, "Failed to receive data.");
    }

    fclose(save);
    close(fd);
    close(fdconn);
    delete[] data;
}