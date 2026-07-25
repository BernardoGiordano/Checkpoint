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

// Header parsing for the network script API: pure string logic, so it reuses the
// same TransferProto::headerValue the wireless transfer relies on. The transfers
// themselves live in httpcall.hpp, behind the web_* bindings in
// checkpoint_api.cpp.

#include "scriptargs.hpp"
#include "scriptheap.hpp"
#include "transferprotocol.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

void ckpt_http_header_value(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "http_header_value");
    // A NULL header block is what web_request leaves behind on a failed
    // transfer, so it reads as "no headers" rather than as a script error.
    const char* headers       = args.strOr(0, "");
    const char* key           = args.str(1);
    ReturnValue->Val->Pointer = strToRet(TransferProto::headerValue(headers, key));
}
