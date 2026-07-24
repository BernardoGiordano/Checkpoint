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

// The progress_* script API. Platform-neutral: both targets render
// ScriptConsole the same way, only the tile geometry differs.
//
// Every binding here is a plain state write and returns immediately. That is
// the point: a script reporting progress from inside a copy loop must never
// wait for a frame, so unlike the gui_* bindings none of these touch
// ScriptUiBridge. The UI thread reads a snapshot whenever it happens to draw.

#include "scriptconsole.hpp"

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    const char* str(struct Value** Param, int i)
    {
        const char* s = (const char*)Param[i]->Val->Pointer;
        return s != nullptr ? s : "";
    }
}

void ckpt_progress_begin(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().beginLayer((size_t)Param[0]->Val->Integer, str(Param, 1), Param[2]->Val->Integer);
}

void ckpt_progress_set(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().setLayer((size_t)Param[0]->Val->Integer, Param[1]->Val->Integer);
}

void ckpt_progress_label(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().setLayerLabel((size_t)Param[0]->Val->Integer, str(Param, 1));
}

void ckpt_progress_end(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().endLayer((size_t)Param[0]->Val->Integer);
}

void ckpt_progress_clear(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().clearProgress();
}
