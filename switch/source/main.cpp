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

#include "main.hpp"
#include "MainScreen.hpp"
extern "C" {
#include "ftp.h"
}

PadState g_pad;
HidTouchScreenState g_touchState;

static void networkLoop(void)
{
    while (appletMainLoop() && !g_shouldExitNetworkLoop) {
        Configuration::getInstance().pollServer();
        if (g_ftpAvailable && Configuration::getInstance().isFTPEnabled()) {
            ftp_loop();
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

    g_screen = std::make_unique<MainScreen>();

    loadTitles();
    // get the user IDs
    std::vector<AccountUid> userIds = Account::ids();
    // set g_currentUId to a default user in case we loaded at least one user
    if (g_currentUId == 0)
        g_currentUId = userIds.at(0);

    Thread networkThread;
    threadCreate(&networkThread, (ThreadFunc)networkLoop, nullptr, nullptr, 16 * 1000, 0x2C, -2);
    threadStart(&networkThread);

    hidInitializeTouchScreen();
    // Configure our supported input layout: a single player with standard controller styles
    padConfigureInput(/*max_players=*/1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    padInitializeDefault(&g_pad);

    while (appletMainLoop()) {
        // Scan the gamepad. This should be done once for each frame
        padUpdate(&g_pad);

        if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) {
          break; // break in order to return to hbmenu
        }

        // Grab the latest touch screen state.
        int count = hidGetTouchScreenStates(&g_touchState, 1);
        touchState *touch = count ? g_touchState.touches : nullptr;

        g_screen->doDraw();
        g_screen->doUpdate(touch);
        SDLH_Render();
    }

    g_shouldExitNetworkLoop = true;
    threadWaitForExit(&networkThread);
    threadClose(&networkThread);

    servicesExit();
    exit(0);
}