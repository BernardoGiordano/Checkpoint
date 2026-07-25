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

#ifndef SCRIPTARGS_HPP
#define SCRIPTARGS_HPP

#include <cstddef>

struct ParseState;
struct Value;

// A checked view over picoc's (Param, NumArgs) argument array.
//
// Every native binding receives its arguments the same way — an array of
// struct Value*, each one's payload reached through ->Val->Integer or
// ->Val->Pointer. Spelling that out per binding meant ~60 unchecked casts: a
// script passing NULL where a char* was expected, or a count that does not
// match the array beside it, dereferenced straight through into a data abort
// with no diagnostic, on a device with no debugger.
//
// This is the one place that knows the convention. Each accessor bounds-checks
// the index, checks the Value's declared type against what the binding is
// asking for, and validates the payload; anything wrong ends the run through
// ProgramFail — the same path a parse error takes, so the message lands in the
// log pane the user is already watching, naming the binding and the argument.
//
// Two rules follow from ProgramFail being a longjmp to the run's exit point:
//
//   * an accessor never returns on failure, so a binding must read every
//     argument it needs *before* constructing any C++ object that owns a
//     resource. In practice: one block of ScriptArgs calls at the top, then
//     the work.
//   * ScriptArgs itself owns nothing and allocates nothing, so it is safe to
//     have one live across a failing accessor.
//
// Bounds this cannot check are named rather than guessed: the length of a
// caller-supplied array is only ever the count argument sitting next to it, so
// strArray/intArray take that count and the binding must have obtained it from
// count(), which is what rejects the absurd values.
class ScriptArgs {
public:
    // Upper bound on any element count a script passes in. Nothing legitimate
    // comes close — a pick list is tens of entries, the largest bundled script
    // builds a few thousand — while an uninitialised or miscomputed count is
    // usually huge or negative. The point is to fail loudly at the boundary
    // instead of walking off the end of the array.
    static constexpr int kMaxListItems = 65536;

    // `binding` is the script-visible name (the one in library_checkpoint.c's
    // table, not the ckpt_ symbol): it is what the user sees in the message and
    // what they can search for in the API header.
    ScriptArgs(struct ParseState* parser, struct Value** param, int numArgs, const char* binding)
        : mParser(parser), mParam(param), mNumArgs(numArgs), mBinding(binding)
    {
    }

    /* ---- integers ------------------------------------------------------ */

    int num(int i) const;
    // Fails with both bounds in the message, so the script author is told the
    // contract rather than just that they broke it.
    int numInRange(int i, int min, int max) const;
    // A byte count: non-negative, widened once here so no binding has to cast a
    // signed script int to size_t itself.
    size_t byteCount(int i) const;
    // An element count for the array argument beside it: non-negative and below
    // kMaxListItems.
    int count(int i) const;

    /* ---- strings ------------------------------------------------------- */

    // A NUL-terminated string argument. NULL fails the run.
    const char* str(int i) const;
    // Same, but NULL reads as `fallback` — for text a script may legitimately
    // omit (a progress label).
    const char* strOr(int i, const char* fallback) const;

    /* ---- pointers ------------------------------------------------------ */

    void* ptr(int i) const;
    void* ptrOrNull(int i) const;

    // Out parameters, by what the binding stores through them.
    char** outStr(int i) const;
    // For an out parameter a script is allowed to skip by passing NULL.
    char** outStrOrNull(int i) const;
    int* outInt(int i) const;
    // A caller-supplied char buffer of `size` bytes, terminator included.
    // `size` must come from a numInRange/num on the argument that carries it.
    char* outBuf(int i, int size) const;

    /* ---- arrays -------------------------------------------------------- */

    // `count` elements, none of them NULL: the check the pick-list bindings
    // used to skip before indexing straight into the array.
    char* const* strArray(int i, int count) const;
    int* intArray(int i, int count) const;

    /* ---- diagnostics --------------------------------------------------- */

    // Ends the run with "<binding>: <message>". printf formatting, unlike
    // picoc's own ProgramFail (which understands only %s/%d/%c/%t/%f).
    [[noreturn]]
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void fail(const char* fmt, ...) const;

private:
    struct Value* at(int i) const;
    int intAt(int i) const;
    void* ptrAt(int i, bool allowNull) const;

    struct ParseState* mParser;
    struct Value** mParam;
    int mNumArgs;
    const char* mBinding;
};

#endif
