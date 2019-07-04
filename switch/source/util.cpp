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

#include "util.hpp"

void servicesExit(void)
{
    ftp_exit();
    nifmExit();
    // debug
    socketExit();

    freeIcons();
    if (g_notificationLedAvailable) {
        hidsysExit();
    }
    nsExit();
    Account::exit();
    SDLH_Exit();
    plExit();
    romfsExit();
}

Result servicesInit(void)
{
    // debug
    Result socinit = 0;
    if ((socinit = socketInitializeDefault()) == 0) {
        // nxlinkStdio();
    }

    Result res = 0;
    romfsInit();
    io::createDirectory("sdmc:/switch");
    io::createDirectory("sdmc:/switch/Checkpoint");
    io::createDirectory("sdmc:/switch/Checkpoint/saves");
    io::createDirectory("sdmc:/switch/Checkpoint/cheats");

    if (R_FAILED(res = plInitialize())) {
        return res;
    }

    if (R_FAILED(res = Account::init())) {
        return res;
    }

    if (R_FAILED(res = nsInitialize())) {
        return res;
    }

    if (!SDLH_Init()) {
        return -1;
    }

    if (R_SUCCEEDED(res = hidsysInitialize())) {
        g_notificationLedAvailable = true;
    }

    Configuration::getInstance();

    if (R_SUCCEEDED(socinit) && R_SUCCEEDED(res = nifmInitialize())) {
        if (R_SUCCEEDED(res = ftp_init())) {
            g_ftpAvailable = true;
        }
    }

    return 0;
}

std::u16string StringUtils::UTF8toUTF16(const char* src)
{
    char16_t tmp[256] = {0};
    utf8_to_utf16((uint16_t*)tmp, (uint8_t*)src, 256);
    return std::u16string(tmp);
}

// https://stackoverflow.com/questions/14094621/change-all-accented-letters-to-normal-letters-in-c
std::string StringUtils::removeAccents(std::string str)
{
    std::u16string src = UTF8toUTF16(str.c_str());
    const std::u16string illegal = UTF8toUTF16("ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ");
    const std::u16string fixed = UTF8toUTF16("AAAAAAECEEEEIIIIDNOOOOOx0UUUUYPsaaaaaaeceeeeiiiiOnooooo/0uuuuypy");

    for (size_t i = 0, sz = src.length(); i < sz; i++) {
        size_t index = illegal.find(src[i]);
        if (index != std::string::npos) {
            src[i] = fixed[index];
        }
    }

    return UTF16toUTF8(src);
}

std::string StringUtils::removeNotAscii(std::string str)
{
    for (size_t i = 0, sz = str.length(); i < sz; i++) {
        if (!isascii(str[i])) {
            str[i] = ' ';
        }
    }
    return str;
}

HidsysNotificationLedPattern blinkLedPattern(u8 times)
{
    HidsysNotificationLedPattern pattern;
    memset(&pattern, 0, sizeof(pattern));

    pattern.baseMiniCycleDuration = 0x1;   // 12.5ms.
    pattern.totalMiniCycles       = 0x2;   // 2 mini cycles.
    pattern.totalFullCycles       = times; // Repeat n times.
    pattern.startIntensity        = 0x0;   // 0%.

    pattern.miniCycles[0].ledIntensity      = 0xF; // 100%.
    pattern.miniCycles[0].transitionSteps   = 0xF; // 15 steps. Total 187.5ms.
    pattern.miniCycles[0].finalStepDuration = 0x0; // Forced 12.5ms.
    pattern.miniCycles[1].ledIntensity      = 0x0; // 0%.
    pattern.miniCycles[1].transitionSteps   = 0xF; // 15 steps. Total 187.5ms.
    pattern.miniCycles[1].finalStepDuration = 0x0; // Forced 12.5ms.

    return pattern;
}

void blinkLed(u8 times)
{
    if (g_notificationLedAvailable) {
        size_t n;
        u64 uniquePadIds[2];
        HidsysNotificationLedPattern pattern = blinkLedPattern(times);
        memset(uniquePadIds, 0, sizeof(uniquePadIds));
        Result res = hidsysGetUniquePadsFromNpad(hidGetHandheldMode() ? CONTROLLER_HANDHELD : CONTROLLER_PLAYER_1, uniquePadIds, 2, &n);
        if (R_SUCCEEDED(res)) {
            for (size_t i = 0; i < n; i++) {
                hidsysSetNotificationLedPattern(&pattern, uniquePadIds[i]);
            }
        }
    }
}