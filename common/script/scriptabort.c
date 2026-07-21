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

/* The script kill switch.
 *
 * picoc is built with -DDEBUGGER, which makes ParseStatement() call
 * DebugCheckStatement() before every statement it runs. The submodule's
 * debug.c (the interactive debugger those hooks were meant for) is filtered
 * out of the build; this file provides the Debug* symbols instead and turns
 * the per-statement hook into an abort check. When the main thread requests
 * an abort, the next statement fails the run through picoc's normal error
 * path — ProgramFail longjmps to the exit point set in ScriptEngine::run —
 * and ScriptRunner cleans up exactly as for any failed script.
 */

#include "checkpoint_api.h"
#include "interpreter.h"
#include <stdatomic.h>

static atomic_int gAbortRequested;

void ckpt_script_abort_request(void)
{
    atomic_store(&gAbortRequested, 1);
}

int ckpt_script_abort_requested(void)
{
    return atomic_load_explicit(&gAbortRequested, memory_order_relaxed);
}

void ckpt_script_abort_reset(void)
{
    atomic_store(&gAbortRequested, 0);
}

/* called by PicocInitialize */
void DebugInit(Picoc* pc)
{
    pc->BreakpointCount  = 0;
    pc->DebugManualBreak = false;
}

/* called by PicocCleanup */
void DebugCleanup(Picoc* pc)
{
    (void)pc;
}

/* called by ParseStatement before every statement executed in RunModeRun */
void DebugCheckStatement(struct ParseState* Parser)
{
    if (atomic_load_explicit(&gAbortRequested, memory_order_relaxed)) {
        ProgramFail(Parser, "aborted by user");
    }
}
