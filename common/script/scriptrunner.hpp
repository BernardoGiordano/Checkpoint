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

#ifndef SCRIPTRUNNER_HPP
#define SCRIPTRUNNER_HPP

#include "scriptuibridge.hpp"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>

// The single owner of "a script is running on a worker thread". Mirrors the
// TransferJob shape: Meyer's singleton, start() spawns the worker, active()
// gates exit/refresh/other jobs, takeResult() polled from the main loop each
// frame. The worker just runs ScriptEngine::run; gui_* bindings park it on the
// bridge until the main loop answers. One script at a time — enforced here.
class ScriptRunner {
public:
    struct Outcome {
        std::string scriptName; // display name, for the result overlay
        int exitValue = 0;
        std::string output;     // everything the script printed (4 KB cap)
        bool cancelled = false; // ended because the user requested an abort
    };

    static ScriptRunner& get(void)
    {
        static ScriptRunner instance;
        return instance;
    }

    // Main thread. Spawns the worker and returns true, or false when a script
    // is already running / the thread could not be created. `titleIdHex` is the
    // selected title as 16-hex-uppercase ("" when none) — it becomes the
    // script's argv[0] and the selected_title() binding.
    bool start(std::string scriptPath, std::string displayName, std::string titleIdHex);

    // True from start() until takeResult() collects the outcome.
    bool active(void) const { return mState.load() != State::Idle; }

    // Main thread, the kill switch: answers the pending/future bridge requests
    // as cancelled and makes picoc fail the run at the next statement, so even
    // a script stuck in a loop dies without rebooting the console. The worker
    // then finishes normally through the usual takeResult() path. No-op when
    // no script is running.
    void requestCancel(void);

    // Main thread, for the "Cancelling…" hint.
    bool cancelRequested(void) const;

    // If the script finished, returns its outcome and resets to idle.
    std::optional<Outcome> takeResult(void);

    ScriptUiBridge& bridge(void) { return mBridge; }

    // Script thread (the selected_title binding). Written only while idle.
    const std::string& selectedTitle(void) const { return mTitleIdHex; }

    // Main thread, for the status strip. Written only while idle.
    const std::string& scriptName(void) const { return mName; }

    // Worker entry point (thunk to get().run()).
    static void runThread(void);

private:
    ScriptRunner(void) = default;

    void run(void);

    // PicoC recurses on ParseStatement for nested C constructs, so the worker
    // needs far more OS stack than the 32 KB default — PKSM effectively ran on
    // the whole main-thread stack. Distinct from picoc's own 32 KB heap.
    static constexpr size_t THREAD_STACK = 128 * 1024;

    enum class State { Idle, Running, Done };

    std::atomic<State> mState{State::Idle};
    std::mutex mMutex; // guards mOutcome
    Outcome mOutcome;
    std::string mPath, mName, mTitleIdHex;
    ScriptUiBridge mBridge;
};

#endif
