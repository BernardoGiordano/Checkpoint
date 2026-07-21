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

#include "scriptengine.hpp"
#include "logging.hpp"
#include <algorithm>
#include <unistd.h>

extern "C" {
#include "picoc.h"
}

namespace {
    // picoc's own heap, not the OS thread stack: every variable, string and
    // parse node the script allocates comes out of this. PKSM ships 32 KB;
    // bundled sharkive.c parses a ~2 MB cheat DB (held natively, not here) but
    // has a deeper parse tree and more locals, so give scripts 64 KB of margin.
    constexpr int PICOC_STACKSIZE = 64 * 1024;

    // Script printf() goes through picoc's stdlib to the real stdout, which is
    // nowhere on a 3DS. Point stdout's buffer at ours for the duration of the
    // run and read it back afterwards — PKSM's trick, and process-global, which
    // is why only one script may run at a time.
    constexpr size_t CAPTURE_SIZE = 4096;
    char g_capture[CAPTURE_SIZE];
}

ScriptEngine::Outcome ScriptEngine::run(const std::string& path, const std::vector<std::string>& args)
{
    std::fill_n(g_capture, CAPTURE_SIZE, '\0');

    const int stdoutSave = dup(STDOUT_FILENO);
    setvbuf(stdout, g_capture, _IOFBF, CAPTURE_SIZE);

    Picoc pc;
    PicocInitialize(&pc, PICOC_STACKSIZE);

    // Anything that fails inside picoc — a parse error, a script exit() — longjmps
    // back to here with PicocExitValue set, so no C++ frame holding a resource may
    // live between this setjmp and PicocCleanup.
    if (!PicocPlatformSetExitPoint(&pc)) {
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }

        PicocPlatformScanFile(&pc, path.c_str());
        PicocCallMain(&pc, (int)argv.size(), argv.data());
    }

    Outcome outcome;
    outcome.exitValue = pc.PicocExitValue;

    PicocCleanup(&pc);

    dup2(stdoutSave, STDOUT_FILENO);
    close(stdoutSave);

    g_capture[CAPTURE_SIZE - 1] = '\0';
    outcome.output              = g_capture;

    Logging::info("[script] {} exited with {}", path, outcome.exitValue);
    if (!outcome.output.empty()) {
        Logging::info("[script] output: {}", outcome.output);
    }

    return outcome;
}
