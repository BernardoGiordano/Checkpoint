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

#include "TransferOverlay.hpp"
#include "ModalChrome.hpp"
#include "common.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "transfer.hpp"
#include "transferstatus.hpp"

TransferMenuOverlay::TransferMenuOverlay(Screen& screen, const std::function<void()>& callbackSend, const std::function<void()>& callbackReceive)
    : ChoiceOverlay(screen, "Choose Send or Receive", Button{"Send", ModalChrome::BTN_LEFT_X, COLOR_ACCENT, COLOR_WHITE, KEY_R, callbackSend},
          Button{"Receive", ModalChrome::BTN_RIGHT_X, COLOR_RAISED, COLOR_TEXT, KEY_B, callbackReceive}, KEY_START)
{
}

ReceiveOverlay::ReceiveOverlay(Screen& screen) : Overlay(screen) {}

void ReceiveOverlay::drawTop(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
}

void ReceiveOverlay::drawBottom(void) const
{
    TextPool& text = TextPool::get();
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(30, 40, 0.5f, 260, 160, COLOR_CARD);
    Gui::drawOutline(30, 40, 260, 160, 2, COLOR_LINE);

    TransferSnapshot ts      = TransferStatus::snapshot();
    const bool networkActive = ts.active && ts.kind == TransferKind::Network;

    bool completed = Transfer::receiverHasCompleted();
    if (completed && !networkActive) {
        std::string backupName = Transfer::receiverCompletedName();
        if (backupName.empty()) {
            backupName = "(unnamed backup)";
        }

        text.draw("File received", 40, 60, 0.65f, COLOR_TEXT);
        text.draw(backupName, 40, 92, 0.52f, COLOR_MUTED);

        std::string notice = Transfer::receiverNotice();
        if (!notice.empty()) {
            text.draw(notice, 40, 122, 0.45f, COLOR_MUTED);
        }

        text.draw("Press A (OK) to refresh now", 40, 170, 0.5f, COLOR_MUTED);
        return;
    }

    std::string info = "Receiver active";
    if (Transfer::receiverRunning()) {
        info = StringUtils::format(
            "IP: %s\nPort: %d\nPIN: %s", Transfer::receiverIp().c_str(), Transfer::receiverPort(), Transfer::receiverToken().c_str());
    }
    else {
        info = "Receiver stopped";
    }

    text.draw(info, 40, 60, 0.55f, COLOR_TEXT);

    int noticeY = 120;
    if (networkActive) {
        u64 total = ts.bytesTotal, done = ts.bytesDone;
        int pct            = total > 0 ? (int)((done * 100) / total) : 0;
        std::string prefix = ts.mode.empty() ? "Downloading backup" : ts.mode;
        std::string status = StringUtils::format("%s... %d%% (%s)", prefix.c_str(), pct, TransferStatus::bytesToMB(done, total).c_str());
        text.draw(status, 40, 120, 0.5f, COLOR_MUTED);
        noticeY = 142;
    }

    std::string notice = Transfer::receiverNotice();
    if (!notice.empty()) {
        text.draw(notice, 40, noticeY, 0.45f, COLOR_MUTED);
    }

    text.draw("Press B to close", 40, 170, 0.5f, COLOR_MUTED);
}

void ReceiveOverlay::update(const InputState& input)
{
    (void)input;
    if (Transfer::receiverHasCompleted() && (hidKeysDown() & KEY_A)) {
        Transfer::stopReceiver();
        Transfer::clearReceiverCompletion();
        Transfer::clearReceiverNotice();
        screen.removeOverlay();
        return;
    }
    if (hidKeysDown() & KEY_B) {
        Transfer::stopReceiver();
        Transfer::clearReceiverCompletion();
        Transfer::clearReceiverNotice();
        screen.removeOverlay();
    }
}
