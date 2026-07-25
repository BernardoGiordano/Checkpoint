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

#include "scriptrequestpump.hpp"

void pumpScriptRequests(ScriptUiBridge& bridge, ScriptRequestSink& sink)
{
    const std::optional<UiRequest> req = bridge.take();
    if (!req) {
        return;
    }

    switch (req->kind) {
        case UiRequest::Kind::Message:
            sink.showMessage(req->prompt);
            break;
        case UiRequest::Kind::Confirm:
            sink.showConfirm(req->prompt);
            break;
        case UiRequest::Kind::PickOne:
            sink.showPickOne(req->prompt, req->items);
            break;
        case UiRequest::Kind::PickMany:
            sink.showPickMany(req->prompt, req->items, req->preselected);
            break;
        case UiRequest::Kind::Keyboard: {
            UiResponse resp;
            // maxChars counts the terminator; the keyboards want the room left
            // for text, and 0 means "the platform's own default".
            resp.text = sink.keyboard(req->prompt, req->maxChars > 0 ? (size_t)req->maxChars - 1 : 0);
            bridge.respond(std::move(resp));
            break;
        }
        case UiRequest::Kind::Numpad: {
            UiResponse resp;
            resp.index = sink.numpad(req->prompt, req->numMin, req->numMax);
            bridge.respond(std::move(resp));
            break;
        }
    }
}
