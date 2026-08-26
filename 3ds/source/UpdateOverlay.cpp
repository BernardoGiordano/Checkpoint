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

#include "UpdateOverlay.hpp"
#include "MessageOverlay.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "i18n.hpp"
#include "logging.hpp"
#include "textpool.hpp"
#include "thread.hpp"
#include "util.hpp"
#include <cstdio>

UpdateOverlay::UpdateOverlay(Screen& screen, const AutoUpdater::Update& update, std::function<void()> onInstalled)
    : Overlay(screen), mJob(std::make_shared<Job>()), mOnInstalled(std::move(onInstalled))
{
    mJob->update = update;
    mText        = i18n::t("updater.downloading", {update.version});
    mLayout      = ModalChrome::fitText(mText, SIZE, false);
    mPosx        = ceilf((320 - StringUtils::textWidth(mText, SIZE)) / 2);

    if (!Threads::create(Threads::WORKER_STACK, [job = mJob]() {
            const AutoUpdater::Outcome outcome = AutoUpdater::install(job->update);
            job->outcome                       = outcome;
            job->finished.store(true, std::memory_order_release);
        })) {
        // No worker, no transfer: report the failure through the same path the
        // worker would have taken rather than leaving the bar up forever.
        Logging::warning("Could not start the update worker thread.");
        mJob->finished.store(true, std::memory_order_release);
    }
}

void UpdateOverlay::drawTop(void) const
{
    ModalChrome::dimTop();
}

void UpdateOverlay::drawBottom(void) const
{
    ModalChrome::dimBottom();
    ModalChrome::drawCard(mLayout, COLOR_LINE);
    TextPool::get().draw(mText, mPosx, mLayout.textY, SIZE, COLOR_TEXT);

    const AutoUpdater::Progress p = AutoUpdater::progress();
    // No total yet (headers still in flight) leaves the trough empty rather
    // than guessing a fraction.
    const float frac = p.total > 0 ? (float)p.downloaded / (float)p.total : 0.0f;

    char bytes[48];
    snprintf(bytes, sizeof(bytes), "%.1f / %.1f MB", p.downloaded / 1048576.0f, p.total / 1048576.0f);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)((frac > 1.0f ? 1.0f : frac) * 100));
    Gui::drawProgressBar(ModalChrome::BTN_WIDE_X, mLayout.btnY + 2, ModalChrome::BTN_WIDE_W, 8, frac, bytes, pct);

    // Once the bytes are down the console is writing the artifact into place,
    // which reports nothing: say so instead of parking a full bar.
    const std::string hint = (p.total > 0 && p.downloaded >= p.total) ? i18n::t("updater.installing") : i18n::t("updater.keep_awake");
    // The bar's own byte / percent labels sit just under the trough, so the
    // hint clears their line rather than printing on top of it.
    TextPool::get().drawCentered(hint, ModalChrome::CARD_X, ModalChrome::CARD_W, mLayout.btnY + 28, 0.42f, COLOR_FAINT);
}

void UpdateOverlay::update(const InputState& input)
{
    (void)input;
    if (!mJob->finished.load(std::memory_order_acquire)) {
        return;
    }

    if (mJob->outcome == AutoUpdater::Outcome::Installed) {
        // Leave the modal up: main() ends the loop on this call, so the last
        // frame the user sees is the finished transfer rather than a flash of
        // the title grid.
        if (mOnInstalled) {
            mOnInstalled();
        }
        return;
    }

    Screen& current = screen;
    dismissThen([&current]() {
        std::shared_ptr<Overlay> error = std::make_shared<InfoOverlay>(current, i18n::t("updater.install_failed"));
        current.setOverlay(error);
    });
}
