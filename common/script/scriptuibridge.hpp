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

#ifndef SCRIPTUIBRIDGE_HPP
#define SCRIPTUIBRIDGE_HPP

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// The blocking request/response channel between a running script and the UI.
// The script thread parks in request() until the main loop notices the pending
// request (polled once per frame), raises the matching overlay, and hands the
// user's answer back through respond(). No UI types on purpose: this is the
// platform-neutral seam the Switch render loop pumps identically.
struct UiRequest {
    enum class Kind { Message, Confirm, PickOne, PickMany, Keyboard, Numpad };
    Kind kind = Kind::Message;
    std::string prompt;
    std::vector<std::string> items; // PickOne / PickMany
    std::vector<bool> preselected;  // PickMany
    int maxChars = 0;               // Keyboard: buffer size including the terminator
    int numMin   = 0;               // Numpad: inclusive lower bound
    int numMax   = 0;               // Numpad: inclusive upper bound
};

struct UiResponse {
    bool confirmed = false;     // Confirm / PickMany
    int index      = -1;        // PickOne (-1 = cancelled) / Numpad (value, -1 = cancelled)
    std::vector<bool> selected; // PickMany
    std::string text;           // Keyboard (empty = cancelled)
};

class ScriptUiBridge {
public:
    // Script thread. Blocks until the main thread responds.
    UiResponse request(UiRequest req);

    // Script thread, fire-and-forget: the status line the main loop draws while
    // the script runs (the gui_status binding).
    void setStatus(std::string text);

    // Main thread. Non-null while a request is waiting and not yet answered;
    // the pointee is stable until respond() (the script thread parks meanwhile).
    const UiRequest* pending(void);

    // Main thread. Wakes the script thread with the user's answer.
    void respond(UiResponse resp);

    // Main thread, on script abort. Answers the pending request (if any) with
    // the default cancelled response and makes every future request() return
    // it immediately, so an aborting script can never park on the bridge
    // again. Cleared by reset().
    void cancelAll(void);

    // Main thread. Snapshot of the last gui_status text.
    std::string statusText(void);

    // Main thread, before each run: drops any stale status/request state.
    void reset(void);

private:
    std::mutex mMutex;
    std::condition_variable mCv;
    std::optional<UiRequest> mReq;
    std::optional<UiResponse> mResp;
    std::string mStatus;
    bool mCancelled = false;
};

#endif
