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

#include "main.hpp"
#include "MainScreen.hpp"
#include "thread.hpp"
#include "util.hpp"

int main()
{
    Result res;
    try {
        res = servicesInit();
    }
    catch (const std::exception& e) {
        res = consoleDisplayError(std::string("Error during services init. ") + e.what(), -1111);
    }
    catch (...) {
        res = consoleDisplayError("Unknown error during startup", -2222);
    }

    if (R_FAILED(res)) {
        try {
            Logger::getInstance().flush();
            res = consoleDisplayError("Unknown error during startup in the try block of the FAILED branch", res);
        }
        catch (const std::exception& e) {
            res = consoleDisplayError(std::string("Error during startup in the catch block of the FAILED branch. ") + e.what(), -4444);
        }
        catch (...) {
            res = consoleDisplayError("Unknown error during startup in the catch ... block of the FAILED branch", -5555);
        }
        exit(res);
    }

    try {
        g_screen = std::make_unique<MainScreen>();

        while (aptMainLoop()) {
            touchPosition touch;
            hidScanInput();
            hidTouchRead(&touch);

            if (hidKeysDown() & KEY_START) {
                if (!g_isLoadingTitles) {
                    break;
                }
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            g_screen->doDrawTop();
            C2D_SceneBegin(g_bottom);
            g_screen->doDrawBottom();
            Gui::frameEnd();
            g_screen->doUpdate(InputState{touch});
        }
    }
    catch (const std::exception& e) {
        consoleDisplayError(std::string("Error during main. ") + e.what(), -3333);
    }
    catch (...) {
        res = consoleDisplayError("Unknown error during main", -6666);
    }

    Logger::getInstance().flush();

    exit(0);
}
