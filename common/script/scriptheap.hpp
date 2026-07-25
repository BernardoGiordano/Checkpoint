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

#ifndef SCRIPTHEAP_HPP
#define SCRIPTHEAP_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

// Run-scoped ownership for the native memory a script touches.
//
// A run ends in one of four ways: a normal return, exit(), a parse/runtime
// error, or the hold-B abort. Only the first runs the script's own free()
// calls — the other three longjmp straight to the exit point. So a script's
// free() can never be the thing that reclaims memory, and before this module
// every char*, every struct directory and every json tree survived an aborted
// run for the lifetime of the process.
//
// Everything a run allocates natively is registered here instead: the script's
// malloc/calloc/realloc (picoc's stdlib routes through the C shim in
// scriptheap_c.h), the strings bindings return, and the json trees json_new
// hands out. ScriptRunner::run() calls releaseAll() after the run, next to
// ckpt_sav_close_all(), so the last exit path is as clean as the first.
//
// The script-facing contract is unchanged: free() and json_delete() still work,
// they just stop being load-bearing — each is a release() that untracks the
// block, so releaseAll() cannot double-free what the script already returned.
// A free() of something this heap does not own (a borrowed json element, a
// second free of the same pointer) is ignored rather than corrupting the heap.
//
// Threading: bindings run on the script worker, releaseAll() runs on the same
// thread once the worker's interpreter has returned. The mutex is there because
// the map is a process-lifetime singleton, not because two threads race for it.
class ScriptHeap {
public:
    // A block's destroy function. Blocks are mostly malloc'd bytes, but a json
    // tree needs `delete`, so ownership carries the way to end it.
    using Deleter = void (*)(void*);

    static ScriptHeap& get(void)
    {
        static ScriptHeap heap;
        return heap;
    }

    // malloc/calloc/realloc, tracked. Null on failure, exactly like the
    // functions they stand in for; realloc(nullptr, n) is alloc(n).
    void* alloc(size_t size);
    void* allocZeroed(size_t count, size_t size);
    void* reallocate(void* ptr, size_t size);

    // A NUL-terminated, tracked copy of `str` — the one form every binding that
    // returns a string to a script uses. Embedded NULs survive: the copy is
    // size() + 1 bytes, so it doubles as the response-body buffer web_get and
    // friends hand back with an explicit length.
    void* dupString(const std::string& str);

    // Take ownership of a block this heap did not allocate (a `new`ed json
    // tree). `deleter` is called with `ptr` at release time.
    void adopt(void* ptr, Deleter deleter);

    // Destroy one block. False (and nothing happens) if the heap does not own
    // it — the caller decides whether that is a script error or a no-op.
    bool release(void* ptr);
    bool owns(void* ptr) const;

    // Destroy everything left. Called once per run, whatever the exit path.
    void releaseAll(void);

    size_t liveBlocks(void) const;

private:
    ScriptHeap(void) = default;

    mutable std::mutex mMutex;
    std::unordered_map<void*, Deleter> mBlocks;
};

// Every binding that returns a string to a script goes through this: a
// run-scoped copy the interpreter sees as an ordinary char*. Used unqualified
// from the bindings in both checkpoint_api.cpp copies and from json/web_api,
// which is why it is a free function and not a static member.
inline void* strToRet(const std::string& str)
{
    return ScriptHeap::get().dupString(str);
}

#endif
