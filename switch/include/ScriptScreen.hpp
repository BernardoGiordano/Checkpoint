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

#ifndef SCRIPTSCREEN_HPP
#define SCRIPTSCREEN_HPP

#include "Screen.hpp"
#include "scriptrequestpump.hpp"
#include "scriptrunner.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// The screen a script run owns, start to finish. It replaces MainScreen for the
// duration (holding it alive so its selection and scroll survive) and gives the
// whole display to one tile: the transcript, with the run's status in its header
// and the progress bars along its bottom. Whatever the script *asks* is a modal
// card over it. MainScreen knows nothing about scripts beyond launching one.
//
// The run's own state lives in ScriptRunner (the worker) and ScriptConsole (the
// output); this screen only renders them and pumps the UI bridge. Which request
// kind becomes which dialog is common/script's business (ScriptRequestSink);
// this screen supplies the Switch dialogs and nothing more.
class ScriptScreen : public Screen, public ScriptRequestSink {
public:
    // `previous` is the screen to return to once the user closes the finished
    // run — kept alive rather than rebuilt, so nothing is reloaded.
    explicit ScriptScreen(std::shared_ptr<Screen> previous, std::string scriptName);
    ~ScriptScreen(void) override;

    void draw(void) const override;
    void update(const InputState& input) override;

    // Plus must not quit the app out from under a running script.
    bool allowsExit(void) const override { return false; }

    // True while a script session owns the screen. main.cpp asks before routing
    // the D-Pad to the log pane; "a script is running" is not the same thing — the
    // pane stays up, and scrollable, after the run has finished.
    static bool showing(void);

    // Log focus: a script dialog is pending but hidden, so the transcript owns
    // the whole screen and the pad. A card that pops up right after a burst of
    // output otherwise buries the very lines the answer depends on, and the
    // script cannot be told to wait — it is parked on the bridge. Y toggles it
    // from either side; the request stays pending the whole time.
    static bool logFocused(void);
    static void toggleLogFocus(void);

    // Tells ScriptConsole how wide a log row is, in monospace columns, so it can
    // wrap output as it arrives and one stored line is exactly one drawn row.
    // Called once at startup: the width is fixed by the tile and the font, and
    // fixing it before any script runs keeps every line wrapped the same way.
    static void configureLogWidth(void);

private:
    // ScriptRequestSink: the Switch half of a script's UI request.
    void showMessage(const std::string& prompt) override;
    void showConfirm(const std::string& prompt) override;
    void showPickOne(const std::string& prompt, const std::vector<std::string>& items) override;
    void showPickMany(const std::string& prompt, const std::vector<std::string>& items, const std::vector<bool>& preselected) override;
    std::string keyboard(const std::string& prompt, size_t maxChars) override;
    int numpad(const std::string& prompt, int min, int max) override;

    // Static because exactly one script runs at a time, and main.cpp asks
    // without holding the screen. Where the pane is scrolled to is
    // ScriptLogView's business, not this screen's.
    static bool sShowing;
    static bool sLogFocus;

    std::shared_ptr<Screen> mPrevious;
    std::string mName;

    // Set once the worker's outcome has been collected; the screen then waits
    // for the user instead of returning on its own, so the last lines of a
    // finished run stay readable.
    bool mFinished = false;
    ScriptRunner::Outcome mOutcome;
};

#endif
