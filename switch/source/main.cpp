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
#include "InfoOverlay.hpp"
#include "MainScreen.hpp"
#include "ScriptScreen.hpp"
#include "YesNoOverlay.hpp"
#include "autoupdater.hpp"
#include "backupsize.hpp"
#include "colors.hpp"
#include "ftpserver.hpp"
#include "i18n.hpp"
#include "logging.hpp"
#include "mtpserver.hpp"
#include "scriptlogview.hpp"
#include "scriptrunner.hpp"
#include "thread.hpp"
#include "titlecatalog.hpp"
#include "transfer.hpp"
#include "transferjob.hpp"
#include <atomic>
#include <optional>
#include <thread>

int main(int argc, char* argv[])
{
    Result res = servicesInit();
    if (R_FAILED(res)) {
        servicesExit();
        exit(res);
    }

    const std::string executablePath = argc > 0 && argv[0] ? argv[0] : "";
    std::optional<AutoUpdater::Update> availableUpdate;
    std::atomic<bool> updateCheckFinished{!Configuration::getInstance().isAutoUpdateEnabled()};
    bool updatePrompted = false;
    bool shouldExit     = false;

    // Match the color tokens to the persisted theme before any screen draws.
    Colors::apply(Configuration::getInstance().theme());

    // Fixes the script log pane's wrap column before any script can print.
    ScriptScreen::configureLogWidth();

    InputState input;
    g_input = &input;
    PadState pad;
    padInitializeDefault(&pad);

    g_screen = std::make_unique<MainScreen>(input);

    std::thread updateCheckThread;
    if (Configuration::getInstance().isAutoUpdateEnabled()) {
        updateCheckThread = std::thread([&availableUpdate, &updateCheckFinished, executablePath]() {
            availableUpdate = AutoUpdater::check(executablePath);
            updateCheckFinished.store(true, std::memory_order_release);
        });
    }

    // Remove any transfer temp files a previous crash/power-loss left behind.
    Transfer::sweepTempFiles();

    TitleCatalog::get().loadTitles();
    // get the user IDs
    std::vector<AccountUid> userIds = Account::ids();
    // set g_currentUId to a default user in case we loaded at least one user
    if (g_currentUId == 0 && !userIds.empty())
        g_currentUId = userIds.at(0);

    while (appletMainLoop() && !shouldExit) {
        padUpdate(&pad);

        input.kDown = padGetButtonsDown(&pad);
        // Don't exit mid-copy (the worker is touching the save filesystem, and
        // tearing down services under it would crash) or mid-script (picoc
        // cannot be preempted).
        // allowsExit() is what keeps Plus from quitting out of a finished script
        // session, where the runner is already idle but the log pane is still up.
        if ((input.kDown & HidNpadButton_Plus) && g_screen->allowsExit() && !TransferJob::get().active() && !ScriptRunner::get().active())
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
            if ((input.kDown & HidNpadButton_B) && g_screen->hasOverlay()) {
                // That press is the overlay's own B (dismiss a gui_message, back
                // out of a picker), so the abort count starts from it rather
                // than from whatever was already accumulated. Only the press is
                // swallowed: holding B on through the overlays still aborts,
                // which is what aborttest case 3 exercises.
                scriptCancelHold = 0;
            }
            else if (++scriptCancelHold >= 45 && !ScriptRunner::get().cancelRequested()) {
                ScriptRunner::get().requestCancel();
            }
        }
        else {
            scriptCancelHold = 0;
        }

        // Log pane scrolling, here for the same reason as the kill switch: a
        // script-raised overlay owns update(), and the pane beside it must stay
        // scrollable while the user answers.
        static int scrollHold = 0;
        if (ScriptScreen::showing()) {
            // A dialog the user pushed aside with Y leaves the pad free again:
            // the pane reads exactly as it does with no dialog pending.
            const bool padIsLog = !g_screen->hasOverlay() || ScriptScreen::logFocused();

            const u64 kPage = input.kDown & (HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_AnyLeft | HidNpadButton_AnyRight);
            if (kPage && padIsLog) {
                ScriptLogView::get().scrollPages((kPage & (HidNpadButton_Left | HidNpadButton_AnyLeft)) ? -1 : 1);
            }

            // Line scrolling: D-Pad up/down, and L/R doing exactly the same. The
            // shoulders are what makes the pane reachable while a script raises a
            // picker — its list owns the D-Pad for as long as the dialog is up,
            // so L/R are ungated where the pad is not.
            u64 kHeld = input.kHeld & (HidNpadButton_L | HidNpadButton_R);
            if (padIsLog) {
                kHeld |= input.kHeld & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_AnyUp | HidNpadButton_AnyDown);
            }
            if (kHeld) {
                // Tap moves one line; holding waits out a short delay and then
                // repeats every other frame, so a long transcript is still
                // reachable without mashing.
                constexpr int DELAY = 20, RATE = 2;
                if (scrollHold == 0 || (scrollHold >= DELAY && (scrollHold - DELAY) % RATE == 0)) {
                    ScriptLogView::get().scrollLines((kHeld & (HidNpadButton_Up | HidNpadButton_AnyUp | HidNpadButton_L)) ? 1 : -1);
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

        g_screen->doDraw();
        g_screen->doUpdate(input);
        if (g_pendingScreen) {
            g_screen        = std::move(g_pendingScreen);
            g_pendingScreen = nullptr;
        }

        // Poll worker result on UI thread. If another modal owns input, keep
        // update notice pending until that modal closes.
        if (!updatePrompted && updateCheckFinished.load(std::memory_order_acquire) && availableUpdate && !g_screen->hasOverlay()) {
            updatePrompted                  = true;
            const auto update               = *availableUpdate;
            auto screen                     = g_screen;
            std::shared_ptr<Overlay> prompt = std::make_shared<YesNoOverlay>(
                *screen, i18n::t("updater.update_available", {update.version}),
                [screen, update, executablePath, &shouldExit]() {
                    if (AutoUpdater::install(update) == AutoUpdater::Outcome::Installed) {
                        if (!AutoUpdater::requestRelaunch(executablePath)) {
                            Logging::warning("Update installed, but automatic relaunch is unavailable.");
                        }
                        shouldExit = true;
                    }
                    else {
                        std::shared_ptr<Overlay> error = std::make_shared<InfoOverlay>(*screen, i18n::t("updater.install_failed"));
                        screen->setOverlay(error);
                    }
                },
                []() {});
            g_screen->setOverlay(prompt);
        }
        Gfx::Render();
    }

    // Worker can still be inside curl when user exits quickly. Join before
    // socket and logging services disappear beneath it.
    if (updateCheckThread.joinable())
        updateCheckThread.join();

    // Teardown is breadcrumbed step-by-step: a crash or hang while closing the
    // app leaves the log pointing at the exact step that didn't return, so we
    // can tell a stuck worker-join from a gfx/service teardown fault instead of
    // guessing (see tools/nx-crash-bt.py for pairing this with the creport).
    Logging::trace("[shutdown] main loop exited (transfer active: {}, script active: {})", TransferJob::get().active(), ScriptRunner::get().active());

    // If the system forced the loop to end while a copy was live, let it finish
    // and join the worker before tearing anything down.
    Logging::trace("[shutdown] joining transfer worker...");
    TransferJob::get().join();
    // Stop the backup-size worker and join it before tearing down the fs services
    // it walks (aborts any long scan in progress).
    Logging::trace("[shutdown] stopping backup-size worker...");
    BackupSizeCache::get().shutdown();

    // A forced applet exit can end the loop mid-script. shutdown() aborts a
    // running script and unparks any UI-bridge wait so the worker can reach its
    // exit path; it must run before servicesExit tears down the fs services the
    // worker may still be touching. A worker parked in a native FS binding won't
    // see the abort until it returns, so this is the prime suspect for a
    // close-time hang — the breadcrumb pair around it will show if it stalls.
    ScriptRunner::get().shutdown();
    Logging::trace("[shutdown] joining script worker...");
    Threads::join();

    Logging::trace("[shutdown] stopping the FTP server...");
    FTPServer::exit();

    Logging::trace("[shutdown] stopping the MTP server...");
    MTPServer::exit();

    Logging::trace("[shutdown] releasing screen and services...");
    g_screen.reset();
    servicesExit();
    exit(0);
}
