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

#include "scriptargs.hpp"
#include "scriptconsole.hpp"

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    // A layer index used to be cast straight to size_t and handed to
    // ScriptConsole, whose range check then swallowed a negative one whole: the
    // bar simply never appeared and the script had no way to find out. Checked
    // here instead, so the mistake is reported at the call that made it.
    size_t layerArg(const ScriptArgs& args)
    {
        return (size_t)args.numInRange(0, 0, (int)ScriptConsole::MAX_LAYERS - 1);
    }
}

void ckpt_progress_begin(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "progress_begin");
    const size_t layer = layerArg(args);
    const char* label  = args.strOr(1, "");
    ScriptConsole::get().beginLayer(layer, label, args.num(2));
}

void ckpt_progress_set(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "progress_set");
    const size_t layer = layerArg(args);
    ScriptConsole::get().setLayer(layer, args.num(1));
}

void ckpt_progress_label(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "progress_label");
    const size_t layer = layerArg(args);
    const char* label  = args.strOr(1, "");
    ScriptConsole::get().setLayerLabel(layer, label);
}

void ckpt_progress_end(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().endLayer(layerArg(ScriptArgs(Parser, Param, NumArgs, "progress_end")));
}

void ckpt_progress_clear(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    ScriptConsole::get().clearProgress();
}
