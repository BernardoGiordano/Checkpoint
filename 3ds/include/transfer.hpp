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

#ifndef TRANSFER_HPP
#define TRANSFER_HPP

#include "title.hpp"
#include <optional>
#include <string>

namespace Transfer {
    // A parsed send destination. The screen only prompts for the raw "ip:port"
    // string and hands it here; validation policy lives with the transport.
    struct TransferTarget {
        std::string ip;
        u16 port = 0;
    };

    // Parses "ip:port"; nullopt on a missing colon, empty ip, or a port outside
    // [1, 65535].
    std::optional<TransferTarget> parseTarget(const std::string& ipPort);

    // True iff `pin` is exactly 4 ASCII digits (the receiver token format).
    bool validPin(const std::string& pin);

    bool startReceiver(std::string& outError);
    void stopReceiver(void);
    bool receiverRunning(void);
    bool consumePendingRefresh(void);
    std::string receiverToken(void);
    std::string receiverIp(void);
    int receiverPort(void);
    std::string receiverNotice(void);
    bool receiverHasCompleted(void);
    std::string receiverCompletedName(void);
    void clearReceiverNotice(void);
    void clearReceiverCompletion(void);

    // Where a send stopped. The screen owns the user-facing message for each
    // stage; `detail` carries only receiver-echoed text (HTTP error body or
    // status line), never Checkpoint-authored prose.
    enum class SendStage { EmptyBackup, Zip, Socket, Resolve, Connect, Send, Response };

    struct SendOutcome {
        bool ok         = false;
        SendStage stage = SendStage::Send;
        std::string detail;
    };

    // Blocking; runs on the TransferJob worker. Progress flows through
    // TransferStatus (beginNetwork/addBytesDone) and is drawn by the main loop.
    SendOutcome sendBackup(const Title& title, const std::u16string& backupPath, const std::string& backupName, const std::string& dataType,
        const std::string& ip, u16 port, const std::string& token);
}

#endif
