/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2021 Bernardo Giordano, FlagBrew
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

static bool isLGPE(u64 id)
{
    return id == 0x0100187003A36000 || id == 0x010003F003A34000;
}

static bool isSWSH(u64 id)
{
    return id == 0x0100ABF008968000 || id == 0x01008DB008C2C000;
}

bool isPKSMBridgeTitle(u64 id)
{
    return isLGPE(id) || isSWSH(id);
}

bool validateIpAddress(const std::string& ip)
{
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 0;
}

std::tuple<bool, Result, std::string> sendToPKSMBrigde(size_t index, AccountUid uid, size_t cellIndex)
{
    auto systemKeyboardAvailable = KeyboardManager::get().isSystemKeyboardAvailable();
    if (!systemKeyboardAvailable.first) {
        return std::make_tuple(false, systemKeyboardAvailable.second, "System keyboard not accessible.");
    }

    // load data
    Title title;
    getTitle(title, uid, index);
    std::string filename;
    if (isLGPE(title.id())) {
        filename = "/savedata.bin";
    }
    else if (isSWSH(title.id())) {
        filename = "/backup";
    }
    else {
        return std::make_tuple(false, systemKeyboardAvailable.second, "Invalid title.");
    }

    std::string srcPath = title.fullPath(cellIndex) + filename;
    FILE* save          = fopen(srcPath.c_str(), "rb");
    if (save == NULL) {
        return std::make_tuple(false, systemKeyboardAvailable.second, "Failed to open source file.");
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
        delete[] data;
        return std::make_tuple(false, -1, "Invalid IP address.");
    }

    // send via TCP
    int fd;
    struct sockaddr_in servaddr;
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        delete[] data;
        return std::make_tuple(false, errno, "Socket creation failed.");
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(PKSM_PORT);
    servaddr.sin_addr.s_addr = inet_addr(ipaddress.second.c_str());

    if (connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        close(fd);
        delete[] data;
        return std::make_tuple(false, errno, "Socket connection failed.");
    }

    size_t total = 0;
    size_t chunk = 1024;
    while (total < size) {
        size_t tosend = size - total > chunk ? chunk : size - total;
        int n         = send(fd, data + total, tosend, 0);
        if (n == -1) {
            break;
        }
        total += n;
        fprintf(stderr, "Sent %lu bytes, %lu still missing\n", (unsigned long)total, (unsigned long)(size - total));
    }

    close(fd);
    delete[] data;

    if (total == size) {
        return std::make_tuple(true, 0, "Data sent correctly.");
    }
    else {
        return std::make_tuple(false, errno, "Failed to send data.");
    }
}

std::tuple<bool, Result, std::string> recvFromPKSMBridge(size_t index, AccountUid uid, size_t cellIndex)
{
    int fd;
    struct sockaddr_in servaddr;
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        Logger::getInstance().log(Logger::ERROR, "Socket creation failed: %d.", fd);
        return std::make_tuple(false, errno, "Socket creation failed.");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(PKSM_PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        close(fd);
        Logger::getInstance().log(Logger::ERROR, "Socket bind failed with errno %d.", errno);
        return std::make_tuple(false, errno, "Socket bind failed.");
    }
    if (listen(fd, 5) < 0) {
        close(fd);
        Logger::getInstance().log(Logger::ERROR, "Socket listen failed with errno %d.", errno);
        return std::make_tuple(false, errno, "Socket listen failed.");
    }

    int fdconn;
    int addrlen = sizeof(servaddr);
    if ((fdconn = accept(fd, (struct sockaddr*)&servaddr, (socklen_t*)&addrlen)) < 0) {
        close(fd);
        Logger::getInstance().log(Logger::ERROR, "Socket accept failed: %d.", fdconn);
        return std::make_tuple(false, errno, "Socket accept failed.");
    }

    size_t size;
    Title title;
    getTitle(title, uid, index);
    std::string filename;
    if (isLGPE(title.id())) {
        filename = "/savedata.bin";
        size     = 0x100000;
    }
    else if (isSWSH(title.id())) {
        filename = "/backup";
        size     = 0x180B19;
    }
    else {
        filename = "DEFAULT";
        // WHAT DO WE DO ABOUT SIZE?
    }
    std::string srcPath = title.fullPath(cellIndex) + filename;
    FILE* save          = fopen(srcPath.c_str(), "wb");
    if (save == NULL) {
        close(fd);
        close(fdconn);
        Logger::getInstance().log(Logger::ERROR, "Failed to open destination file with errno %d.", errno);
        return std::make_tuple(false, errno, "Failed to open destination file.");
    }

    char* data = new char[size]();

    size_t total = 0;
    size_t chunk = 1024;
    while (total < size) {
        size_t torecv = size - total > chunk ? chunk : size - total;
        int n         = recv(fdconn, data + total, torecv, 0);
        if (n == -1) {
            break;
        }
        total += n;
        fprintf(stderr, "Recv %lu bytes, %lu still missing\n", (unsigned long)total, (unsigned long)(size - total));
    }

    close(fd);
    close(fdconn);

    if (total == size) {
        fwrite(data, 1, size, save);
        fclose(save);
        delete[] data;
        Logger::getInstance().log(Logger::INFO, "pksmbridge data received correctly.");
        return std::make_tuple(true, 0, "Data received correctly.");
    }
    else {
        fclose(save);
        delete[] data;
        Logger::getInstance().log(Logger::ERROR, "Failed to receive pksmbridge data.");
        return std::make_tuple(false, errno, "Failed to receive data.");
    }
}
