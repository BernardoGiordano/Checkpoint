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

#include "scriptheap.hpp"
#include "logging.hpp"
#include <cstdlib>
#include <cstring>

extern "C" {
#include "scriptheap_c.h"
}

namespace {
    void freeBlock(void* ptr)
    {
        free(ptr);
    }
}

void* ScriptHeap::alloc(size_t size)
{
    void* ptr = malloc(size);
    if (ptr) {
        std::lock_guard<std::mutex> lock(mMutex);
        mBlocks[ptr] = freeBlock;
    }
    return ptr;
}

void* ScriptHeap::allocZeroed(size_t count, size_t size)
{
    void* ptr = calloc(count, size);
    if (ptr) {
        std::lock_guard<std::mutex> lock(mMutex);
        mBlocks[ptr] = freeBlock;
    }
    return ptr;
}

void* ScriptHeap::reallocate(void* ptr, size_t size)
{
    if (!ptr) {
        return alloc(size);
    }
    // Only blocks this heap owns may be realloc'd: anything else is a script
    // bug, and passing it to realloc() would be the corruption free() is
    // guarded against below. Leave it alone and answer null.
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mBlocks.find(ptr);
        if (it == mBlocks.end() || it->second != freeBlock) {
            Logging::warning("[script] realloc of an unowned pointer ignored");
            return nullptr;
        }
    }
    void* next = realloc(ptr, size);
    if (next) {
        std::lock_guard<std::mutex> lock(mMutex);
        mBlocks.erase(ptr);
        mBlocks[next] = freeBlock;
    }
    return next;
}

void* ScriptHeap::dupString(const std::string& str)
{
    // size() + 1 from data(): the terminator comes along, and so does any NUL
    // inside the payload — web_get's body reaches the script this way.
    char* ret = (char*)alloc(str.size() + 1);
    if (ret) {
        memcpy(ret, str.data(), str.size() + 1);
    }
    return (void*)ret;
}

void ScriptHeap::adopt(void* ptr, Deleter deleter)
{
    if (!ptr || !deleter) {
        return;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mBlocks[ptr] = deleter;
}

bool ScriptHeap::release(void* ptr)
{
    if (!ptr) {
        return false;
    }
    Deleter deleter = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mBlocks.find(ptr);
        if (it == mBlocks.end()) {
            return false;
        }
        deleter = it->second;
        mBlocks.erase(it);
    }
    // Outside the lock: a deleter is free() or a C++ destructor, neither of
    // which needs the map, and both of which can be slow.
    deleter(ptr);
    return true;
}

bool ScriptHeap::owns(void* ptr) const
{
    if (!ptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    return mBlocks.find(ptr) != mBlocks.end();
}

void ScriptHeap::releaseAll(void)
{
    std::unordered_map<void*, Deleter> blocks;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        blocks.swap(mBlocks);
    }
    if (blocks.empty()) {
        return;
    }
    // Not a defect in itself: a script that returns normally still leaves
    // whatever it never free()d, and a run aborted mid-request leaves the whole
    // response body. The count is the interesting part when a script is
    // suspected of exhausting linear memory on an O3DS.
    Logging::debug("[script] heap reclaimed {} block(s) left by the run", blocks.size());
    for (auto& block : blocks) {
        block.second(block.first);
    }
}

size_t ScriptHeap::liveBlocks(void) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mBlocks.size();
}

/* ---- the C face, for picoc's stdlib ----------------------------------- */

extern "C" {

void* ckpt_script_malloc(size_t size)
{
    return ScriptHeap::get().alloc(size);
}

void* ckpt_script_calloc(size_t count, size_t size)
{
    return ScriptHeap::get().allocZeroed(count, size);
}

void* ckpt_script_realloc(void* ptr, size_t size)
{
    return ScriptHeap::get().reallocate(ptr, size);
}

void ckpt_script_free(void* ptr)
{
    if (ptr && !ScriptHeap::get().release(ptr)) {
        // A double free, or a free() of a pointer the script never owned (a
        // borrowed json element, an interpreter-owned string). The real free()
        // would corrupt the allocator; releaseAll() has the real blocks anyway.
        Logging::warning("[script] free() of a pointer the script does not own, ignored");
    }
}
}
