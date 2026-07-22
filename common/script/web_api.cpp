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

// Platform-neutral pieces of the network script API. web_request/web_upload_file
// and url_encode are per-platform (they drive curl next to web_get in each
// checkpoint_api.cpp), but header parsing is pure string logic, so it reuses the
// same TransferProto::headerValue the wireless transfer relies on — one copy for
// both consoles.

#include "transferprotocol.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {
    // Same contract as the other bindings' strToRet: scripts receive plain
    // malloc'd copies they may free().
    void* strToRet(const std::string& str)
    {
        char* ret = (char*)malloc(str.size() + 1);
        if (ret) {
            memcpy(ret, str.c_str(), str.size() + 1);
        }
        return (void*)ret;
    }
}

void ckpt_http_header_value(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    (void)Parser;
    (void)NumArgs;
    const char* headers       = (char*)Param[0]->Val->Pointer;
    const char* key           = (char*)Param[1]->Val->Pointer;
    ReturnValue->Val->Pointer = strToRet(TransferProto::headerValue(headers ? headers : "", key ? key : ""));
}
