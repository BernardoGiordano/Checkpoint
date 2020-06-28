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

#include <3ds.h>

#include "app.hpp"
#include "fspxi.hpp"
#include "logger.hpp"
#include "stringutils.hpp"
#include "io.hpp"
#include "wifi.hpp"
#include "Screen.hpp"
#include "MainScreen.hpp"

namespace {
    bool g_successfulInit = false;
    void consoleDisplayError(const std::string& message, Result res)
    {
        gfxInitDefault();

        consoleInit(GFX_TOP, nullptr);
        printf("\x1b[2;13HCheckpoint v%d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, GIT_REV);
        printf("\x1b[5;1HError during startup: \x1b[31m0x%08lX\x1b[0m", res);
        printf("\x1b[8;1HDescription: \x1b[33m%s\x1b[0m", message.c_str());
        printf("\x1b[29;16HPress START to exit.");
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        while (aptMainLoop() && !(hidKeysDown() & KEY_START)) {
            hidScanInput();
        }

        gfxExit();
    }
}

int __stacksize__ = 128 * 1024;

extern "C" void userAppInit()
{
    IODataHolder data;
    data.srcPath = "sdmc:/3ds";
    io::createDirectory(data);
    data.srcPath = "sdmc:/3ds/Checkpoint";
    io::createDirectory(data);
    data.srcPath = Platform::Directories::SaveBackupsDir;
    io::createDirectory(data);
    data.srcPath = Platform::Directories::ExtdataBackupsDir;
    io::createDirectory(data);
    data.srcPath = Platform::Directories::WifiSlotBackupsDir;
    io::createDirectory(data);
    data.srcPath = "sdmc:/cheats";
    io::createDirectory(data);

    Result res = 0;
    Handle hbldrHandle;
    if (R_FAILED(res = svcConnectToPort(&hbldrHandle, "hb:ldr"))) {
        consoleDisplayError("Rosalina not found on this system.\nAn updated CFW is required to launch Checkpoint.", res);
        return;
    }

    if (R_FAILED(res = FSPXI::getHandle())) {
        consoleDisplayError("SVC extensions not found on this system.\nAn updated CFW is required to launch Checkpoint.", res);
        return;
    }

    romfsInit();
    cfguInit();
    amInit();
    pxiDevInit();

    hidSetRepeatParameters(15, 9);

    g_successfulInit = true;
}

extern "C" void userAppExit()
{
    pxiDevExit();
    amExit();
    cfguExit();
    romfsExit();

    FSPXI::closeHandle();

    if (Wifi::anyWriteToWifiSlots) {
        archiveUnmountAll();
        fsExit();

        APT_HardwareResetAsync();
        while(true) {
            svcSleepThread(1000);
        }
    }
}

Checkpoint::Checkpoint()
{
    consoleDebugInit(debugDevice_SVC);
    Logger::log(Logger::INFO, "Checkpoint loading completed!");

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

    LightEvent_Init(&data.shouldPerformAction, RESET_ONESHOT);
    data.actionThread = threadCreate(Action::performActionThreadFunc, &data, __stacksize__, prio - 2, -2, false);
    data.actionThreadKeepGoing.test_and_set();

    data.multiSelectedCount.fill(0);
    Wifi::load(data.thingsToActOn[BackupTypes::WifiSlots]);
    data.input.keepRunning = g_successfulInit;
    data.input.currentScreen = std::make_shared<MainScreen>(data.draw);

    LightEvent_Init(&data.titleLoadingThreadBeginEvent, RESET_ONESHOT);
    LightLock_Init(&data.backupableVectorLock);
    data.titleLoadingThread = threadCreate(Title::loaderThreadFunc, &data, __stacksize__, prio - 1, -2, false);

    data.titleLoadingThreadKeepGoing.test_and_set();
    LightEvent_Signal(&data.titleLoadingThreadBeginEvent);
}
Checkpoint::~Checkpoint()
{
    data.titleLoadingThreadKeepGoing.clear(); // the thread will stop
    LightEvent_Signal(&data.titleLoadingThreadBeginEvent); // if it stopped trying to load, make it try again and it will stop directly
    threadJoin(data.titleLoadingThread, U64_MAX);
    threadFree(data.titleLoadingThread);

    data.actionThreadKeepGoing.clear(); // the thread will stop
    LightEvent_Signal(&data.shouldPerformAction);
    threadJoin(data.actionThread, U64_MAX);
    threadFree(data.actionThread);

    Title::freeIcons();
    if (Wifi::anyWriteToWifiSlots) {
        Wifi::finalizeWrite();
    }

    Logger::flush();
}

bool Checkpoint::mainLoop()
{
    return data.input.keepRunning && aptMainLoop();
}
void Checkpoint::update()
{
    hidScanInput();
    hidTouchRead(&data.input.touch);
    data.input.kDown = hidKeysDown();
    data.input.kDownRepeated = hidKeysDownRepeat();
    data.input.kHeld = hidKeysHeld();

    // copy the shared_ptr before possibly having it change values, to ensure nothing bad happens
    auto screen = data.input.currentScreen;
    screen->doUpdate(data.input);
}
void Checkpoint::prepareDraw()
{
    data.draw.citro.beginFrame();
    data.draw.citro.clear(COLOR_BG);
}
void Checkpoint::draw()
{
    data.input.currentScreen->doDraw(data.draw);
}
void Checkpoint::endDraw()
{
    data.draw.citro.endFrame();
}
