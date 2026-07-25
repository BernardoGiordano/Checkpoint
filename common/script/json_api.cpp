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

// The json_* script API: a port of PKSM's nlohmann wrappers (pksm_api.cpp)
// Platform-neutral — nothing here touches the UI bridge or the catalog, so the
// Switch target reuses it as-is. Getter type mismatches fail the script via
// ProgramFail instead of letting nlohmann throw through the interpreter.

#include "json.hpp"
#include "scriptheap.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    nlohmann::json* arg(struct Value** Param, int i)
    {
        return (nlohmann::json*)Param[i]->Val->Pointer;
    }

    // The heap's destroy function for a tree: json_new's `new`, undone.
    void deleteJson(void* ptr)
    {
        delete (nlohmann::json*)ptr;
    }
}

void ckpt_json_new(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* ret = new nlohmann::json;
    // explicitly set it to invalid, like PKSM: a json_new the script never
    // json_parse's answers 0 to json_is_valid
    *ret = nlohmann::json::parse("{", nullptr, false);
    // Run-scoped from here on: a script that aborts between json_new and
    // json_delete no longer strands the whole tree.
    ScriptHeap::get().adopt(ret, deleteJson);
    ReturnValue->Val->Pointer = (void*)ret;
}

void ckpt_json_parse(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* out = arg(Param, 0);
    *out                = nlohmann::json::parse((char*)Param[1]->Val->Pointer, nullptr, false);
}

void ckpt_json_delete(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    // json_array_element/json_object_element return borrowed pointers into a
    // parent tree with the same struct JSON* type as an owned root, so this is
    // the one place that can tell the two apart: only a root json_new adopted
    // is in the heap. Deleting a borrowed element used to corrupt the
    // allocator; now it fails the script where the mistake was made.
    if (!ScriptHeap::get().release(arg(Param, 0))) {
        ProgramFail(Parser, "json_delete on a value that is not a json_new root");
    }
}

void ckpt_json_is_valid(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_discarded() ? 0 : 1;
}

void ckpt_json_is_int(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_number_integer() ? 1 : 0;
}

void ckpt_json_is_bool(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_boolean() ? 1 : 0;
}

void ckpt_json_is_string(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_string() ? 1 : 0;
}

void ckpt_json_is_array(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_array() ? 1 : 0;
}

void ckpt_json_is_object(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->is_object() ? 1 : 0;
}

void ckpt_json_get_int(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    if (!get->is_number()) {
        ProgramFail(Parser, "json_get_int on a non-number value");
    }
    ReturnValue->Val->Integer = get->get<int>();
}

void ckpt_json_get_bool(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    if (!get->is_boolean()) {
        ProgramFail(Parser, "json_get_bool on a non-boolean value");
    }
    ReturnValue->Val->Integer = get->get<bool>() ? 1 : 0;
}

void ckpt_json_get_string(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    if (!get->is_string()) {
        ProgramFail(Parser, "json_get_string on a non-string value");
    }
    ReturnValue->Val->Pointer = strToRet(get->get_ref<std::string&>());
}

// nlohmann's size(): array/object element count, 1 for scalars, 0 for null.
void ckpt_json_array_size(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = (int)arg(Param, 0)->size();
}

void ckpt_json_array_element(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    const int index     = Param[1]->Val->Integer;
    if (!get->is_array() || index < 0 || index >= (int)get->size()) {
        ProgramFail(Parser, "json_array_element index %d out of range", index);
    }
    ReturnValue->Val->Pointer = &(*get)[index];
}

void ckpt_json_object_contains(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ReturnValue->Val->Integer = arg(Param, 0)->contains((char*)Param[1]->Val->Pointer) ? 1 : 0;
}

void ckpt_json_object_element(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    const char* name    = (char*)Param[1]->Val->Pointer;
    if (!get->is_object() || !get->contains(name)) {
        ProgramFail(Parser, "json_object_element: no member '%s'", name);
    }
    ReturnValue->Val->Pointer = &(*get)[name];
}

void ckpt_json_object_key(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    nlohmann::json* get = arg(Param, 0);
    const int index     = Param[1]->Val->Integer;
    if (!get->is_object() || index < 0 || index >= (int)get->size()) {
        ProgramFail(Parser, "json_object_key index %d out of range", index);
    }
    auto it = get->cbegin();
    std::advance(it, index);
    ReturnValue->Val->Pointer = strToRet(it.key());
}
