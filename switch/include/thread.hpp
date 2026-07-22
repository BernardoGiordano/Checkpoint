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

#ifndef THREAD_HPP
#define THREAD_HPP

#include <cstddef>
#include <optional>

// The subset of the 3DS Threads helper that common/script/scriptrunner.cpp
// uses: spawn a detached worker with an explicit stack size. Not a general
// thread pool — one slot, sized for ScriptRunner's one-script-at-a-time model
// (the next create() reaps the previous worker, which active() guarantees has
// already finished).
namespace Threads {
    bool create(std::optional<size_t> stackSize, void (*entrypoint)(void));

    // Reap the worker if one is still live (wait for it to exit, close the
    // handle, free its stack). A no-op when none was ever created. create()
    // calls it before reusing the slot; the shutdown path calls it before
    // servicesExit so a still-running script's thread can't be left touching
    // the fs services being torn down. The caller must first ask any running
    // script to stop, or this blocks until it finishes on its own.
    void join(void);
}

#endif
