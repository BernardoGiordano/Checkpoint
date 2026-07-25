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

#ifndef SCRIPTENGINE_HPP
#define SCRIPTENGINE_HPP

#include <string>
#include <vector>

// Runs one PicoC script to completion on the calling thread and hands back what
// it printed and what it returned. Nothing here knows about threads or UI: the
// worker-thread owner (ScriptRunner) and the UI bridge are layered on top, so
// the interpreter can be exercised headlessly without a running screen.
//
// One script at a time — the interpreter instance is a local of run(), so a
// second call would get a second interpreter; the single-run rule is enforced
// by ScriptRunner's worker, not by this module holding state.
namespace ScriptEngine {
    struct Outcome {
        // 0 = the script's main() returned 0. Anything else is a script-side
        // failure: a non-zero return, an exit(), or a parse/runtime error, in
        // which case picoc's diagnostic is at the end of output.
        int exitValue = 0;
        // The tail of what the script printed, plus any interpreter
        // diagnostic — enough for the result card. The full (scrollable) output
        // lives in ScriptConsole, which is where it was streamed as it ran.
        std::string output;
    };

    // argv handed to the script's main(). Phase 1 passes the selected title id
    // as 16-hex-uppercase (empty string when no title is selected).
    Outcome run(const std::string& path, const std::vector<std::string>& args);
}

#endif
