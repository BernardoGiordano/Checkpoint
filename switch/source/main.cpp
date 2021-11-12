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

    InputState input;
    g_input = &input;
    PadState pad;
    padInitializeDefault(&pad);

    g_screen = std::make_unique<MainScreen>(input);

    loadTitles();
    // get the user IDs
    std::vector<AccountUid> userIds = Account::ids();
    // set g_currentUId to a default user in case we loaded at least one user
    if (g_currentUId == 0)
        g_currentUId = userIds.at(0);

    Thread networkThread;
    threadCreate(&networkThread, (ThreadFunc)networkLoop, nullptr, nullptr, 16 * 1000, 0x2C, -2);
    threadStart(&networkThread);

    while (appletMainLoop()) {
        padUpdate(&pad);

        input.kDown = padGetButtonsDown(&pad);
        if (input.kDown & HidNpadButton_Plus)
            break;

        input.kHeld = padGetButtons(&pad);
        input.kUp = padGetButtonsUp(&pad);
        hidGetTouchScreenStates(&input.touch, 1);

        g_screen->doDraw();
        g_screen->doUpdate(input);
        SDLH_Render();
    }

    g_shouldExitNetworkLoop = true;
    threadWaitForExit(&networkThread);
    threadClose(&networkThread);

    servicesExit();
    exit(0);
}
