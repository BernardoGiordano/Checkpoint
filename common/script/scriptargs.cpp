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

#include "scriptargs.hpp"
#include <cstdarg>
#include <cstdio>

extern "C" {
#include "interpreter.h"
}

namespace {
    // picoc's own diagnostics are terse and unpunctuated; match them.
    const char* baseName(enum BaseType base)
    {
        switch (base) {
            case TypeInt:
                return "int";
            case TypePointer:
                return "pointer";
            default:
                return "value";
        }
    }
}

void ScriptArgs::fail(const char* fmt, ...) const
{
    // Formatted here rather than handed to ProgramFail, which knows only
    // %s/%d/%c/%t/%f and has no way to prepend the binding name. A stack buffer
    // on purpose: this runs on the way out of a failing run, so it must not
    // depend on an allocation succeeding.
    char message[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    ProgramFail(mParser, "%s: %s", mBinding, message);
    __builtin_unreachable(); // ProgramFail longjmps; it is just not declared so
}

struct Value* ScriptArgs::at(int i) const
{
    // picoc rejects a call with too few arguments before it ever reaches an
    // intrinsic, so this only fires when a binding reads past its own
    // prototype in library_checkpoint.c.
    if (mParam == nullptr || i < 0 || i >= mNumArgs) {
        fail("argument %d is missing (%d passed)", i + 1, mNumArgs);
    }
    struct Value* value = mParam[i];
    if (value == nullptr || value->Val == nullptr) {
        fail("argument %d has no value", i + 1);
    }
    return value;
}

int ScriptArgs::intAt(int i) const
{
    struct Value* value = at(i);
    // The Value carries the type from the prototype, so a mismatch here means
    // the binding and its declaration disagree — a Checkpoint bug that used to
    // read a pointer as an int and abort somewhere else entirely.
    if (value->Typ == nullptr || value->Typ->Base != TypeInt) {
        fail("argument %d is declared %s, read as int", i + 1, value->Typ != nullptr ? baseName(value->Typ->Base) : "untyped");
    }
    return value->Val->Integer;
}

void* ScriptArgs::ptrAt(int i, bool allowNull) const
{
    struct Value* value = at(i);
    // Every pointer parameter — char*, char**, int*, struct directory*,
    // struct JSON* — arrives as TypePointer; picoc allocates the argument slot
    // from the prototype, so an array argument has already decayed by here.
    if (value->Typ == nullptr || value->Typ->Base != TypePointer) {
        fail("argument %d is declared %s, read as a pointer", i + 1, value->Typ != nullptr ? baseName(value->Typ->Base) : "untyped");
    }
    void* ptr = value->Val->Pointer;
    if (ptr == nullptr && !allowNull) {
        fail("argument %d must not be NULL", i + 1);
    }
    return ptr;
}

/* ---- integers ---------------------------------------------------------- */

int ScriptArgs::num(int i) const
{
    return intAt(i);
}

int ScriptArgs::numInRange(int i, int min, int max) const
{
    const int n = intAt(i);
    if (n < min || n > max) {
        fail("argument %d is %d, must be between %d and %d", i + 1, n, min, max);
    }
    return n;
}

size_t ScriptArgs::byteCount(int i) const
{
    const int n = intAt(i);
    if (n < 0) {
        fail("argument %d is a size of %d bytes", i + 1, n);
    }
    return (size_t)n;
}

int ScriptArgs::count(int i) const
{
    const int n = intAt(i);
    if (n < 0 || n > kMaxListItems) {
        fail("argument %d is a count of %d, must be between 0 and %d", i + 1, n, kMaxListItems);
    }
    return n;
}

/* ---- strings ----------------------------------------------------------- */

const char* ScriptArgs::str(int i) const
{
    return (const char*)ptrAt(i, false);
}

const char* ScriptArgs::strOr(int i, const char* fallback) const
{
    const char* s = (const char*)ptrAt(i, true);
    return s != nullptr ? s : fallback;
}

/* ---- pointers ---------------------------------------------------------- */

void* ScriptArgs::ptr(int i) const
{
    return ptrAt(i, false);
}

void* ScriptArgs::ptrOrNull(int i) const
{
    return ptrAt(i, true);
}

char** ScriptArgs::outStr(int i) const
{
    return (char**)ptrAt(i, false);
}

char** ScriptArgs::outStrOrNull(int i) const
{
    return (char**)ptrAt(i, true);
}

int* ScriptArgs::outInt(int i) const
{
    return (int*)ptrAt(i, false);
}

char* ScriptArgs::outBuf(int i, int size) const
{
    if (size <= 0) {
        fail("argument %d is an output buffer of %d bytes", i + 1, size);
    }
    return (char*)ptrAt(i, false);
}

/* ---- arrays ------------------------------------------------------------ */

char* const* ScriptArgs::strArray(int i, int count) const
{
    char* const* items = (char* const*)ptrAt(i, count > 0 ? false : true);
    // Walked once here so the binding's own loop can index without checking:
    // a NULL element becomes a named script error instead of the strlen that
    // faults inside whatever std::string is being built from it.
    for (int n = 0; n < count; n++) {
        if (items[n] == nullptr) {
            fail("argument %d element %d is NULL", i + 1, n);
        }
    }
    return items;
}

int* ScriptArgs::intArray(int i, int count) const
{
    return (int*)ptrAt(i, count > 0 ? false : true);
}
