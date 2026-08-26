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

#ifndef UPDATEOVERLAY_HPP
#define UPDATEOVERLAY_HPP

#include "Overlay.hpp"
#include "autoupdater.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Runs one accepted update to completion. The download and install happen on a
// worker thread so the main loop keeps drawing: this overlay is the bar that
// makes that visible, polling AutoUpdater::progress() once a frame. It takes no
// input — the transfer cannot be cancelled, and quitting is blocked by
// AutoUpdater::busy() while it runs.
//
// On success it calls `onInstalled`, which main() uses to leave its loop and
// relaunch; on failure it replaces itself with the install-failed notice.
class UpdateOverlay : public Overlay {
public:
    UpdateOverlay(Screen& screen, const AutoUpdater::Update& update, std::function<void()> onInstalled);
    ~UpdateOverlay();
    void draw(void) const override;
    void update(const InputState&) override;

private:
    AutoUpdater::Update mUpdate;
    std::function<void()> mOnInstalled;
    std::string mText;
    std::thread mWorker;
    // Written once by the worker as its last act, read by the UI thread every
    // frame; the release/acquire pair publishes mOutcome along with it.
    std::atomic<bool> mFinished{false};
    AutoUpdater::Outcome mOutcome = AutoUpdater::Outcome::Failed;
};

#endif
