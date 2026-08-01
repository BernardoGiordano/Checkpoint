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
#include "common.hpp"
#include "i18n.hpp"
#include "main.hpp"
#include "textpool.hpp"
#include "transfer.hpp"
#include "transferstatus.hpp"

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
            backupName = i18n::t("transfer.unnamed");
        }

        text.draw(i18n::t("transfer.file_received"), 40, 60, 0.65f, COLOR_TEXT);
        text.draw(backupName, 40, 92, 0.52f, COLOR_MUTED);

        std::string notice = Transfer::receiverNotice();
        if (!notice.empty()) {
            text.draw(notice, 40, 122, 0.45f, COLOR_MUTED);
        }

        text.draw(i18n::t("transfer.refresh_hint"), 40, 170, 0.5f, COLOR_MUTED);
        return;
    }

    std::string info = i18n::t("transfer.receiver_active");
    if (Transfer::receiverRunning()) {
        info = StringUtils::format(
            "IP: %s\nPort: %d\nPIN: %s", Transfer::receiverIp().c_str(), Transfer::receiverPort(), Transfer::receiverToken().c_str());
    }
    else {
        info = i18n::t("transfer.receiver_stopped");
    }

    text.draw(info, 40, 60, 0.55f, COLOR_TEXT);

    int noticeY = 120;
    if (networkActive) {
        u64 total = ts.bytesTotal, done = ts.bytesDone;
        int pct            = total > 0 ? (int)((done * 100) / total) : 0;
        float frac         = total > 0 ? (float)done / (float)total : 0.0f;
        std::string prefix = ts.mode.empty() ? i18n::t("transfer.downloading") : i18n::transferMode(ts.mode);
        text.draw(prefix, 40, 112, 0.5f, COLOR_TEXT);
        Gui::drawProgressBar(40, 132, 240, 10, frac, TransferStatus::bytesToMB(done, total), StringUtils::format("%d%%", pct));
        noticeY = 158;
    }

    std::string notice = Transfer::receiverNotice();
    if (!notice.empty()) {
        text.draw(notice, 40, noticeY, 0.45f, COLOR_MUTED);
    }

    std::string hint = networkActive ? (TransferStatus::cancelRequested() ? i18n::t("transfer.cancelling") : i18n::t("transfer.cancel_hint"))
                                     : i18n::t("transfer.close_hint");
    text.draw(hint, 40, 170, 0.5f, COLOR_MUTED);
}

void ReceiveOverlay::update(const InputState& input)
{
    (void)input;

    // While an upload is in flight, B means "cancel the transfer", never "close
    // the overlay": the receiver must stay registered until the server thread
    // has abandoned the request, so closing waits for the transfer to end.
    TransferSnapshot ts = TransferStatus::snapshot();
    if (ts.active && ts.kind == TransferKind::Network) {
        if (hidKeysHeld() & KEY_B) {
            if (++mCancelHoldFrames >= 45 && !TransferStatus::cancelRequested()) {
                TransferStatus::requestCancel();
            }
        }
        else {
            mCancelHoldFrames = 0;
        }
        return;
    }
    mCancelHoldFrames = 0;

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
