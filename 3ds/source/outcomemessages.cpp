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
#include "gbasave.hpp"
#include "i18n.hpp"

namespace {
    // The GbaSave synthetic results carry advice a bare stage can't; an empty
    // string means the result adds nothing over the stage mapping.
    std::string gbaMessage(Result res)
    {
        if (res == GbaSave::resNoSave) {
            return i18n::t("outcome.gba_no_save");
        }
        if (res == GbaSave::resSizeMismatch) {
            return i18n::t("outcome.gba_size_mismatch");
        }
        if (res == GbaSave::resBadBackup) {
            return i18n::t("outcome.gba_bad_backup");
        }
        return "";
    }
}

std::string OutcomeMessages::backupError(io::BackupStage stage, const std::string& dataType, Result res)
{
    if (std::string gba = gbaMessage(res); !gba.empty()) {
        return gba;
    }
    switch (stage) {
        case io::BackupStage::OpenArchive:
            return i18n::t("outcome.open_archive");
        case io::BackupStage::DeleteDst:
            return i18n::t("outcome.delete_dst");
        case io::BackupStage::CreateDst:
            return i18n::t("outcome.create_dst");
        default:
            return i18n::t("outcome.backup_failed", {dataType});
    }
}

std::string OutcomeMessages::restoreError(io::BackupStage stage, const std::string& dataType, Result res)
{
    if (std::string gba = gbaMessage(res); !gba.empty()) {
        return gba;
    }
    switch (stage) {
        case io::BackupStage::OpenArchive:
            return i18n::t("outcome.open_archive");
        case io::BackupStage::ReadFile:
            return i18n::t("outcome.read_file");
        case io::BackupStage::Commit:
            return i18n::t("outcome.commit");
        case io::BackupStage::SecureValue:
            return i18n::t("outcome.secure_value");
        default:
            return i18n::t("outcome.restore_failed", {dataType});
    }
}

std::string OutcomeMessages::sendError(const Transfer::SendOutcome& outcome)
{
    switch (outcome.stage) {
        case Transfer::SendStage::PayloadTooLarge:
            return i18n::t("outcome.send_too_large");
        case Transfer::SendStage::Zip:
            return i18n::t("outcome.send_zip");
        case Transfer::SendStage::Socket:
            return i18n::t("outcome.send_socket");
        case Transfer::SendStage::Resolve:
            return i18n::t("outcome.send_resolve");
        case Transfer::SendStage::Connect:
            return i18n::t("outcome.send_connect");
        case Transfer::SendStage::Response:
            return outcome.detail.empty() ? i18n::t("outcome.send_no_response") : i18n::t("outcome.send_receiver_error", {outcome.detail});
        case Transfer::SendStage::Send:
            // A receiver-side cancel just drops the connection, so the sender
            // can't tell it apart from a genuine network failure.
            return i18n::t("outcome.send_interrupted");
        case Transfer::SendStage::Cancelled:
            return i18n::t("transfer.cancelled");
        default:
            return i18n::t("outcome.send_failed");
    }
}
