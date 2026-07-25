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
#include "logging.hpp"
#include "scriptconsole.hpp"
#include "scriptengine.hpp"
#include "scriptheap.hpp"
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
    ScriptConsole::get().reset();
    ckpt_script_abort_reset();

    // Breadcrumb the start on the main thread: if the worker faults mid-run the
    // "exited with" line from ScriptEngine::run never appears, so this line being
    // the last script trace pins the crash to this script's execution.
    Logging::info("[script] starting '{}' ({}), title {}", mName, mPath, mTitleIdHex.empty() ? "<none>" : mTitleIdHex);

    mState.store(State::Running);
    if (!Threads::create(std::optional<size_t>(THREAD_STACK), ScriptRunner::runThread)) {
        mState.store(State::Idle);
        Logging::error("[script] worker thread creation failed for '{}'", mName);
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
    // longjmp): reclaim any archive handles the script left open, and the
    // native memory it (or the bindings) allocated on its behalf. Only a normal
    // return runs the script's own free() calls, so this is the only reclaim
    // that covers an abort — see scriptheap.hpp.
    ckpt_sav_close_all();
    ScriptHeap::get().releaseAll();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        // A script may still finish cleanly in the window before the abort
        // hook fires; a zero exit is reported as success, not as an abort.
        const bool cancelled = ckpt_script_abort_requested() != 0 && out.exitValue != 0;
        mOutcome             = Outcome{mName, out.exitValue, std::move(out.output), cancelled};
        // Publish Done under the same lock shutdown() evaluates its predicate
        // under, or a store landing between that check and the wait would be
        // missed and teardown would block forever.
        mState.store(State::Done);
    }
    mDoneCv.notify_all();
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

void ScriptRunner::shutdown(void)
{
    if (mState.load() == State::Idle) {
        return;
    }

    Logging::trace("[script] shutdown: aborting '{}' and reaping the worker", mName);
    // requestCancel() is the whole "make it stop" half: abort flag first, then
    // cancelAll() so a parked request() returns and every later one refuses to
    // park. Without it the wait below (and the platform join after it) would
    // never be answered.
    requestCancel();

    // Wait for run() to publish its outcome. It runs past the abort only as far
    // as the script's next statement, plus whatever native binding it is inside
    // right now — that call has to finish either way before it is safe to tear
    // down the services underneath it.
    std::unique_lock<std::mutex> lock(mMutex);
    mDoneCv.wait(lock, [this] { return mState.load() != State::Running; });
    Logging::trace("[script] shutdown: worker finished");
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
