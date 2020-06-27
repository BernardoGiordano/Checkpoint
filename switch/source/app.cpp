/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include <switch.h>

#include <atomic>

#include "app.hpp"
#include "logger.hpp"
#include "stringutils.hpp"
#include "io.hpp"
#include "colors.hpp"
#include "Screen.hpp"

extern "C" {
#include "ftp.h"
}

namespace {
    bool g_successfulInit = false;
    bool g_ftpAvailable = false;
    bool g_notificationLedAvailable = false;
    std::atomic_flag g_KeepRunningNetworkLoop = ATOMIC_FLAG_INIT;

    void networkLoop(void* arg)
    {
        (void)arg;
        while (g_KeepRunningNetworkLoop.test_and_set()) {
            Configuration::get().pollServer();
            if (g_ftpAvailable && Configuration::get().isFTPEnabled()) {
                ftp_loop();
            }
        }
    }
}

extern "C" void userAppInit()
{
    IODataHolder data;
    data.srcPath = "sdmc:/switch";
    io::createDirectory(data);
    data.srcPath = "sdmc:/switch/Checkpoint";
    io::createDirectory(data);
    data.srcPath = "sdmc:/switch/Checkpoint/saves";
    io::createDirectory(data);

    Logger::log(Logger::INFO, "Starting Checkpoint loading...");

    if (appletGetAppletType() != AppletType_Application) {
        Logger::log(Logger::WARN, "Please do not run Checkpoint in applet mode.");
    }

    Result socinit = 0;
    if (R_SUCCEEDED(socinit = socketInitializeDefault())) {
        g_KeepRunningNetworkLoop.test_and_set();
        // nxlinkStdio();
    }
    else {
        Logger::log(Logger::INFO, "Unable to initialize socket. Result code 0x%08lX.", socinit);
    }

    Result res = 0;

    romfsInit();

    if (R_FAILED(res = plInitialize(PlServiceType_User))) {
        Logger::log(Logger::ERROR, "plInitialize failed. Result code 0x%08lX.", res);
        return;
    }

    /*
    if (R_FAILED(res = Account::init())) {
        Logger::log(Logger::ERROR, "Account::init failed. Result code 0x%08lX.", res);
        return;
    }
    */

    if (R_FAILED(res = nsInitialize())) {
        Logger::log(Logger::ERROR, "nsInitialize failed. Result code 0x%08lX.", res);
        return;
    }

    if (R_SUCCEEDED(res = hidsysInitialize())) {
        g_notificationLedAvailable = true;
    }
    else {
        Logger::log(Logger::INFO, "Notification led not available. Result code 0x%08lX.", res);
    }

    Configuration::load();

    if (R_SUCCEEDED(socinit)) {
        if (R_SUCCEEDED(res = ftp_init())) {
            g_ftpAvailable = true;
            Logger::log(Logger::INFO, "FTP Server successfully loaded.");
        }
        else {
            Logger::log(Logger::INFO, "FTP Server failed to load. Result code 0x%08lX.", res);
        }
    }

    if (R_SUCCEEDED(res = pdmqryInitialize())) {

    }
    else {
        Logger::log(Logger::WARN, "pdmqryInitialize failed with result 0x%08lX.", res);
    }

    Logger::log(Logger::INFO, "Checkpoint loading completed!");
    g_successfulInit = true;
}

extern "C" void userAppExit()
{
    Configuration::save();

    Logger::flush();

    if (g_ftpAvailable)
        ftp_exit();
    if (g_notificationLedAvailable)
        hidsysExit();

    pdmqryExit();
    socketExit();
    nsExit();
    plExit();
    romfsExit();
}

Checkpoint::Checkpoint()
{
    threadCreate(&data.networkThread, networkLoop, nullptr, nullptr, 16 * 1024, 0x2C, -2);
    threadStart(&data.networkThread);

    data.keepRunning = g_successfulInit && data.draw.SDLHelper.ready;
}
Checkpoint::~Checkpoint()
{
    g_KeepRunningNetworkLoop.clear();
    threadWaitForExit(&data.networkThread);
    threadClose(&data.networkThread);
}

bool Checkpoint::mainLoop()
{
    return data.keepRunning && appletMainLoop();
}
void Checkpoint::update()
{
    hidScanInput();
    hidTouchRead(&data.input.touch, 0);
    data.input.kDown = hidKeysDown(CONTROLLER_P1_AUTO);
    data.input.kHeld = hidKeysHeld(CONTROLLER_P1_AUTO);

    // copy the shared_ptr before possibly having it change values, to ensure nothing bad happens
    auto screen = data.input.currentScreen;
    screen->doUpdate(data.input);
}
void Checkpoint::prepareDraw()
{
    data.draw.SDLHelper.clearScreen(theme().c1);
}
void Checkpoint::draw()
{
    data.currentScreen->doDraw(data.draw.SDLHelper);
}
void Checkpoint::endDraw()
{
    data.SDLHelper.render();
}
