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

#include "outcomemessages.hpp"

std::string OutcomeMessages::backupError(io::BackupStage stage, const std::string& dataType)
{
    switch (stage) {
        case io::BackupStage::OpenArchive:
            return "Failed to open save archive.";
        case io::BackupStage::DeleteDst:
            return "Failed to delete the existing backup\ndirectory recursively.";
        case io::BackupStage::CreateDst:
            return "Failed to create destination directory.";
        default:
            return "Failed to backup " + dataType + ".";
    }
}

std::string OutcomeMessages::restoreError(io::BackupStage stage, const std::string& dataType)
{
    switch (stage) {
        case io::BackupStage::OpenArchive:
            return "Failed to open save archive.";
        case io::BackupStage::ReadFile:
            return "Failed to read save file backup.";
        case io::BackupStage::Commit:
            return "Failed to commit save data.";
        case io::BackupStage::SecureValue:
            return "Failed to fix secure value.";
        default:
            return "Failed to restore " + dataType + ".";
    }
}

std::string OutcomeMessages::sendError(const Transfer::SendOutcome& outcome)
{
    switch (outcome.stage) {
        case Transfer::SendStage::Zip:
            return "Failed to create backup package.";
        case Transfer::SendStage::Socket:
            return "Failed to open socket.";
        case Transfer::SendStage::Resolve:
            return "Invalid IP address.";
        case Transfer::SendStage::Connect:
            return "Failed to connect.";
        case Transfer::SendStage::Response:
            return outcome.detail.empty() ? "Receiver returned no response." : "Receiver error: " + outcome.detail;
        default:
            return "Transfer failed.";
    }
}
