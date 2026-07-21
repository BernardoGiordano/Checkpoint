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

#include "scriptrunner.hpp"
#include "scriptengine.hpp"
#include "thread.hpp"

extern "C" {
#include "checkpoint_api.h"
}

bool ScriptRunner::start(std::string scriptPath, std::string displayName, std::string titleIdHex)
{
    if (mState.load() != State::Idle) {
        return false;
    }

    mPath       = std::move(scriptPath);
    mName       = std::move(displayName);
    mTitleIdHex = std::move(titleIdHex);
    mBridge.reset();
    ckpt_script_abort_reset();

    mState.store(State::Running);
    if (!Threads::create(std::optional<size_t>(THREAD_STACK), ScriptRunner::runThread)) {
        mState.store(State::Idle);
        return false;
    }
    return true;
}

void ScriptRunner::runThread(void)
{
    get().run();
}

void ScriptRunner::run(void)
{
    // Runs on the worker thread. Drop below the main thread first so a script
    // that never yields its core (a pure compute loop) can't starve the loop
    // that samples hold-B and raises the abort — see ckpt_script_lower_priority.
    ckpt_script_lower_priority();

    ScriptEngine::Outcome out = ScriptEngine::run(mPath, {mTitleIdHex});
    // The run is over whatever the exit path was (return, exit(), parse-error
    // longjmp): reclaim any archive handles the script left open.
    ckpt_sav_close_all();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        // A script may still finish cleanly in the window before the abort
        // hook fires; a zero exit is reported as success, not as an abort.
        const bool cancelled = ckpt_script_abort_requested() != 0 && out.exitValue != 0;
        mOutcome             = Outcome{mName, out.exitValue, std::move(out.output), cancelled};
    }
    mState.store(State::Done);
}

void ScriptRunner::requestCancel(void)
{
    if (mState.load() != State::Running) {
        return;
    }
    // Order matters: raise the abort flag first so a script woken by
    // cancelAll() can't issue another blocking request in between.
    ckpt_script_abort_request();
    mBridge.cancelAll();
}

bool ScriptRunner::cancelRequested(void) const
{
    return ckpt_script_abort_requested() != 0;
}

std::optional<ScriptRunner::Outcome> ScriptRunner::takeResult(void)
{
    if (mState.load() != State::Done) {
        return std::nullopt;
    }

    Outcome outcome;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        outcome = mOutcome;
    }
    mState.store(State::Idle);
    return outcome;
}
