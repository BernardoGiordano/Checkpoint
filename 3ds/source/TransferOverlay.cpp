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
#include "main.hpp"
#include "transfer.hpp"

TransferMenuOverlay::TransferMenuOverlay(Screen& screen, const std::function<void()>& callbackSend, const std::function<void()>& callbackReceive)
    : Overlay(screen), hid(2, 2)
{
    textBuf = C2D_TextBufNew(64);
    C2D_TextParse(&text, textBuf, "Choose Send or Receive");
    C2D_TextOptimize(&text);

    sendFunc    = callbackSend;
    receiveFunc = callbackReceive;

    posx = ceilf(320 - text.width * 0.6f) / 2;
    posy = 40 + ceilf(120 - 0.6f * fontGetInfo(NULL)->lineFeed) / 2;

    buttonSend    = std::make_unique<Clickable>(42, 162, 116, 36, COLOR_BLACK_DARKERR, COLOR_WHITE, "\uE005 Send", true);
    buttonReceive = std::make_unique<Clickable>(162, 162, 116, 36, COLOR_BLACK_DARKERR, COLOR_WHITE, "\uE001 Receive", true);
}

TransferMenuOverlay::~TransferMenuOverlay(void)
{
    C2D_TextBufDelete(textBuf);
}

void TransferMenuOverlay::drawTop(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
}

void TransferMenuOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_BLACK_DARKER);
    C2D_DrawText(&text, C2D_WithColor, posx, posy, 0.5f, 0.6f, 0.6f, COLOR_WHITE);
    C2D_DrawRectSolid(40, 160, 0.5f, 240, 40, COLOR_BLACK_MEDIUM);

    buttonSend->draw(0.7f, 0);
    buttonReceive->draw(0.7f, 0);

    if (hid.index() == 0) {
        Gui::drawPulsingOutline(42, 162, 116, 36, 2, COLOR_PURPLE_DARK);
    }
    else {
        Gui::drawPulsingOutline(162, 162, 116, 36, 2, COLOR_PURPLE_DARK);
    }
}

void TransferMenuOverlay::update(const InputState& input)
{
    (void)input;
    u32 kDown = hidKeysDown();
    hid.update(2);

    hid.index(buttonSend->held() ? 0 : buttonReceive->held() ? 1 : hid.index());
    buttonSend->selected(hid.index() == 0);
    buttonReceive->selected(hid.index() == 1);

    if ((kDown & KEY_R) || buttonSend->released() || ((kDown & KEY_A) && hid.index() == 0)) {
        sendFunc();
    }
    else if ((kDown & KEY_B) || buttonReceive->released() || ((kDown & KEY_A) && hid.index() == 1)) {
        receiveFunc();
    }
    else if (kDown & KEY_START) {
        screen.removeOverlay();
    }
}

ReceiveOverlay::ReceiveOverlay(Screen& screen) : Overlay(screen)
{
    textBuf = C2D_TextBufNew(256);
}

ReceiveOverlay::~ReceiveOverlay(void)
{
    C2D_TextBufDelete(textBuf);
}

void ReceiveOverlay::drawTop(void) const
{
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_OVERLAY);
}

void ReceiveOverlay::drawBottom(void) const
{
    C2D_TextBufClear(textBuf);
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(30, 40, 0.5f, 260, 160, COLOR_BLACK_DARKERR);

    bool completed = Transfer::receiverHasCompleted();
    if (completed && !(g_transferIsNetwork && g_isTransferringFile)) {
        std::string backupName = Transfer::receiverCompletedName();
        if (backupName.empty()) {
            backupName = "(unnamed backup)";
        }

        C2D_Text titleText;
        C2D_TextParse(&titleText, textBuf, "File received");
        C2D_TextOptimize(&titleText);
        C2D_DrawText(&titleText, C2D_WithColor, 40, 60, 0.5f, 0.65f, 0.65f, COLOR_WHITE);

        C2D_Text nameText;
        C2D_TextParse(&nameText, textBuf, backupName.c_str());
        C2D_TextOptimize(&nameText);
        C2D_DrawText(&nameText, C2D_WithColor, 40, 92, 0.5f, 0.52f, 0.52f, COLOR_GREY_LIGHT);

        std::string notice = Transfer::receiverNotice();
        if (!notice.empty()) {
            C2D_Text noticeText;
            C2D_TextParse(&noticeText, textBuf, notice.c_str());
            C2D_TextOptimize(&noticeText);
            C2D_DrawText(&noticeText, C2D_WithColor, 40, 122, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
        }

        C2D_Text hintText;
        C2D_TextParse(&hintText, textBuf, "Press A (OK) to refresh now");
        C2D_TextOptimize(&hintText);
        C2D_DrawText(&hintText, C2D_WithColor, 40, 170, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
        return;
    }

    std::string info = "Receiver active";
    if (Transfer::receiverRunning()) {
        info = StringUtils::format("IP: %s\nPort: %d\nPIN: %s", Transfer::receiverIp().c_str(), Transfer::receiverPort(),
            Transfer::receiverToken().c_str());
    }
    else {
        info = "Receiver stopped";
    }

    C2D_Text infoText;
    C2D_TextParse(&infoText, textBuf, info.c_str());
    C2D_TextOptimize(&infoText);
    C2D_DrawText(&infoText, C2D_WithColor, 40, 60, 0.5f, 0.55f, 0.55f, COLOR_WHITE);

    int noticeY = 120;
    if (g_transferIsNetwork && g_isTransferringFile) {
        u64 total = g_transferBytesTotal;
        u64 done  = g_transferBytesDone;
        int pct   = total > 0 ? (int)((done * 100) / total) : 0;
        std::string prefix = g_transferMode.empty() ? "Downloading backup" : g_transferMode;
        std::string status = StringUtils::format("%s... %d%% (%llu / %llu)", prefix.c_str(), pct, (unsigned long long)done, (unsigned long long)total);
        C2D_Text statusText;
        C2D_TextParse(&statusText, textBuf, status.c_str());
        C2D_TextOptimize(&statusText);
        C2D_DrawText(&statusText, C2D_WithColor, 40, 120, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
        noticeY = 142;
    }

    std::string notice = Transfer::receiverNotice();
    if (!notice.empty()) {
        C2D_Text noticeText;
        C2D_TextParse(&noticeText, textBuf, notice.c_str());
        C2D_TextOptimize(&noticeText);
        C2D_DrawText(&noticeText, C2D_WithColor, 40, noticeY, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
    }

    C2D_Text hintText;
    C2D_TextParse(&hintText, textBuf, "Press B to close");
    C2D_TextOptimize(&hintText);
    C2D_DrawText(&hintText, C2D_WithColor, 40, 170, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
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
