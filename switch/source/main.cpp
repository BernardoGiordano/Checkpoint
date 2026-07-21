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

#include "main.hpp"
#include "MainScreen.hpp"
#include "backupsize.hpp"
#include "colors.hpp"
#include "logging.hpp"
#include "scriptrunner.hpp"
#include "titlecatalog.hpp"
#include "transfer.hpp"
#include "transferjob.hpp"
extern "C" {
#include "ftp.h"
}

static void networkLoop(void)
{
    // Driven purely by the exit flag: appletMainLoop() belongs to the main
    // thread (calling it here can eat exit/focus applet events).
    loop_status_t lastStatus = LOOP_CONTINUE;
    while (!g_shouldExitNetworkLoop) {
        if (g_ftpAvailable && Configuration::getInstance().isFTPEnabled()) {
            loop_status_t status = ftp_loop();
            if (status != LOOP_CONTINUE) {
                if (status != lastStatus)
                    Logging::warning("[ftp] ftp_loop returned {}", status == LOOP_RESTART ? "LOOP_RESTART" : "LOOP_EXIT");
                svcSleepThread(100'000'000ULL); // don't spin on a persistent error
            }
            lastStatus = status;
        }
        else {
            // FTP off (the default): don't busy-spin a core for the app's
            // lifetime; wake ~10x/s to re-check the flag and the toggle.
            svcSleepThread(100'000'000ULL); // 100 ms
        }
    }
}

int main(void)
{
    Result res = servicesInit();
    if (R_FAILED(res)) {
        servicesExit();
        exit(res);
    }

    // Match the color tokens to the persisted theme before any screen draws.
    Colors::apply(Configuration::getInstance().theme());

    InputState input;
    g_input = &input;
    PadState pad;
    padInitializeDefault(&pad);

    g_screen = std::make_unique<MainScreen>(input);

    // Remove any transfer temp files a previous crash/power-loss left behind.
    Transfer::sweepTempFiles();

    TitleCatalog::get().loadTitles();
    // get the user IDs
    std::vector<AccountUid> userIds = Account::ids();
    // set g_currentUId to a default user in case we loaded at least one user
    if (g_currentUId == 0 && !userIds.empty())
        g_currentUId = userIds.at(0);

    Thread networkThread;
    // Stack size must be page-aligned or threadCreate fails with LibnxError_BadInput.
    Result netRc = threadCreate(&networkThread, (ThreadFunc)networkLoop, nullptr, nullptr, 16 * 1024, 0x2C, -2);
    if (R_FAILED(netRc))
        Logging::error("[net] threadCreate failed with result 0x{:08X}", netRc);
    else if (R_FAILED(netRc = threadStart(&networkThread)))
        Logging::error("[net] threadStart failed with result 0x{:08X}", netRc);

    while (appletMainLoop()) {
        padUpdate(&pad);

        input.kDown = padGetButtonsDown(&pad);
        // Don't exit mid-copy (the worker is touching the save filesystem, and
        // tearing down services under it would crash) or mid-script (picoc
        // cannot be preempted).
        if ((input.kDown & HidNpadButton_Plus) && !TransferJob::get().active() && !ScriptRunner::get().active())
            break;

        input.kHeld = padGetButtons(&pad);
        input.kUp   = padGetButtonsUp(&pad);
        hidGetTouchScreenStates(&input.touch, 1);

        // Script kill switch: hold B to abort the running script (picoc fails
        // the run at its next statement, so even an infinite loop dies without
        // rebooting the console). Lives here rather than in MainScreen::update
        // so it keeps counting while a script-raised overlay is swallowing
        // that screen's update.
        static int scriptCancelHold = 0;
        if (ScriptRunner::get().active() && (input.kHeld & HidNpadButton_B)) {
            if (++scriptCancelHold >= 45 && !ScriptRunner::get().cancelRequested()) {
                ScriptRunner::get().requestCancel();
            }
        }
        else {
            scriptCancelHold = 0;
        }

        g_screen->doDraw();
        g_screen->doUpdate(input);
        if (g_pendingScreen) {
            g_screen        = std::move(g_pendingScreen);
            g_pendingScreen = nullptr;
        }
        Gfx::Render();
    }

    // If the system forced the loop to end while a copy was live, let it finish
    // and join the worker before tearing anything down.
    TransferJob::get().join();
    // Stop the backup-size worker and join it before tearing down the fs services
    // it walks (aborts any long scan in progress).
    BackupSizeCache::get().shutdown();

    g_shouldExitNetworkLoop = true;
    threadWaitForExit(&networkThread);
    threadClose(&networkThread);

    g_screen.reset();
    servicesExit();
    exit(0);
}
