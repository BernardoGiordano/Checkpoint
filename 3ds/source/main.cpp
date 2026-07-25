/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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
#include "ScriptScreen.hpp"
#include "backupsize.hpp"
#include "colors.hpp"
#include "configuration.hpp"
#include "ftpserver.hpp"
#include "loader.hpp"
#include "scriptlogview.hpp"
#include "scriptrunner.hpp"
#include "server.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include "transfer.hpp"
#include "transferjob.hpp"
#include "util.hpp"
#include <chrono>

int main()
{
    auto start = std::chrono::high_resolution_clock::now();

    Result res;
    try {
        res = servicesInit();
    }
    catch (const std::exception& e) {
        res = consoleDisplayError(std::string("Error during services init. ") + e.what(), -1);
        exit(res);
    }
    catch (...) {
        res = consoleDisplayError("Unknown error during startup", -2);
        exit(res);
    }

    if (R_FAILED(res)) {
        // at this point we already had an error message displayed
        exit(res);
    }

    try {
        // Remove temp transfer archives a previous crash/power-loss left behind.
        Transfer::sweepTempFiles();

        // Match the color tokens to the persisted theme before any screen draws.
        Colors::apply(Configuration::getInstance().theme());

        // Fixes the script log pane's wrap column before any script can print.
        ScriptScreen::configureLogWidth();

        g_screen       = std::make_unique<MainScreen>();
        auto uiIsReady = std::chrono::high_resolution_clock::now();
        Logging::info("Loading took {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(uiIsReady - start).count());

        while (aptMainLoop()) {
            touchPosition touch;
            hidScanInput();
            hidTouchRead(&touch);

            if (hidKeysDown() & KEY_START) {
                if (g_screen->allowsExit() && !TitleCatalog::get().progress().active && !TransferJob::get().active() &&
                    !ScriptRunner::get().active()) {
                    break;
                }
            }

            // Script kill switch: hold B to abort the running script (picoc
            // fails the run at its next statement, so even an infinite loop
            // dies without rebooting the console). Lives here rather than in
            // MainScreen::update so it keeps counting while a script-raised
            // overlay is swallowing that screen's update.
            static int scriptCancelHold = 0;
            if (ScriptRunner::get().active() && (hidKeysHeld() & KEY_B)) {
                if ((hidKeysDown() & KEY_B) && g_screen->hasOverlay()) {
                    // That press is the overlay's own B (dismiss a gui_message,
                    // back out of a picker), so the abort count starts from it
                    // rather than from whatever was already accumulated. Only
                    // the press is swallowed: holding B on through the overlays
                    // still aborts, which is what aborttest case 3 exercises.
                    scriptCancelHold = 0;
                }
                else if (++scriptCancelHold >= 45 && !ScriptRunner::get().cancelRequested()) {
                    ScriptRunner::get().requestCancel();
                }
            }
            else {
                scriptCancelHold = 0;
            }

            // Log pane scrolling, here for the same reason as the kill switch:
            // a script-raised overlay owns update(), and the pane underneath it
            // must stay scrollable while the user answers.
            static int scrollHold = 0;
            if (ScriptScreen::showing()) {
                // Everything on the D-Pad: up/down by a line, left/right by a
                // page. The shoulder buttons used to page and no longer do —
                // one pad for one job is less to explain than two.
                //
                // All of it only while the pane is what the D-Pad would
                // otherwise do nothing to: an overlay's list owns the D-Pad for
                // its own selection.
                const u32 kDown = hidKeysDown();
                if ((kDown & (KEY_DLEFT | KEY_DRIGHT)) && !g_screen->hasOverlay()) {
                    ScriptLogView::get().scrollPages((kDown & KEY_DLEFT) ? -1 : 1);
                }

                const u32 kHeld = hidKeysHeld() & (KEY_DUP | KEY_DDOWN);
                if (kHeld && !g_screen->hasOverlay()) {
                    // Tap moves one line; holding waits out a short delay and
                    // then repeats every other frame, so a long transcript is
                    // still reachable without mashing.
                    constexpr int DELAY = 20, RATE = 2;
                    if (scrollHold == 0 || (scrollHold >= DELAY && (scrollHold - DELAY) % RATE == 0)) {
                        ScriptLogView::get().scrollLines((kHeld & KEY_DUP) ? 1 : -1);
                    }
                    scrollHold++;
                }
                else {
                    scrollHold = 0;
                }
            }
            else {
                scrollHold = 0;
            }

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            g_screen->doDrawTop();
            C2D_SceneBegin(g_bottom);
            g_screen->doDrawBottom();
            Gui::frameEnd();
            TextPool::get().frameTick();
            g_screen->doUpdate(InputState{touch});

            // Apply any deferred screen swap requested during doUpdate (e.g.
            // opening or leaving the Settings page) now that it has returned.
            if (g_pendingScreen) {
                g_screen        = std::move(g_pendingScreen);
                g_pendingScreen = nullptr;
            }
        }
    }
    catch (const std::exception& e) {
        consoleDisplayError(std::string("Error during main. ") + e.what(), -5);
    }
    catch (...) {
        res = consoleDisplayError("Unknown error during main", -6);
    }

    // Stop and join every background thread BEFORE calling exit(). This cannot be
    // left to the atexit-registered Threads::exit: exit() runs atexit handlers and
    // static destructors in one LIFO chain ordered by registration time, and the
    // lazily-constructed singletons workers use (BackupSizeCache, TitleCatalog, …)
    // are created after servicesInit registered its handlers — so their destructors
    // run before that Threads::exit would, freeing the maps a live size-walk is
    // still inserting into (heap corruption, data abort in free()). Raise every
    // stop flag first, then join; the atexit registrations stay as a backstop for
    // early-error exits (Threads::exit is idempotent).
    TitleCatalog::clearCartScanFlag();
    Server::requestStop();
    FTPServer::requestStop();
    BackupSizeCache::shutdownStatic();
    // A forced APT exit (or an exception out of doUpdate) can end the loop
    // mid-script. The worker may be parked on the UI bridge waiting for the
    // loop that just ended, so it has to be aborted and unparked before
    // Threads::exit() tries to join it — see ScriptRunner::shutdown().
    ScriptRunner::get().shutdown();
    Threads::exit();
    // ftp_exit() closes the listen socket / live sessions; must run only after
    // the loop thread above has been joined, never while it may be in ftp_loop.
    FTPServer::exit();

    exit(0);
}
