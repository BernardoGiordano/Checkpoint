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
#include "InfoOverlay.hpp"
#include "ModalChrome.hpp"
#include "colors.hpp"
#include "gfx.hpp"
#include "i18n.hpp"
#include "shapes.hpp"
#include <cstdio>

namespace {
    constexpr int CARD_W = 640, CARD_H = 220;
    constexpr int CARD_X = (1280 - CARD_W) / 2, CARD_Y = (720 - CARD_H) / 2;
    constexpr int BAR_X = CARD_X + 32, BAR_W = CARD_W - 64, BAR_H = 16;
    constexpr int BAR_Y = CARD_Y + 116;
}

UpdateOverlay::UpdateOverlay(Screen& screen, const AutoUpdater::Update& update, std::function<void()> onInstalled)
    : Overlay(screen), mUpdate(update), mOnInstalled(std::move(onInstalled))
{
    mText   = i18n::t("updater.downloading", {update.version});
    mWorker = std::thread([this]() {
        const AutoUpdater::Outcome outcome = AutoUpdater::install(mUpdate);
        mOutcome                           = outcome;
        mFinished.store(true, std::memory_order_release);
    });
}

UpdateOverlay::~UpdateOverlay()
{
    // The overlay only closes after the worker reported in, so this normally
    // joins an already-finished thread. It still has to be here: a forced applet
    // exit can destroy the screen mid-transfer, and the worker must be off curl
    // before the socket services go away.
    if (mWorker.joinable())
        mWorker.join();
}

void UpdateOverlay::draw(void) const
{
    ModalChrome::dim();
    Shapes::cardRound(CARD_X, CARD_Y, CARD_W, CARD_H, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

    u32 titleW, titleH;
    Gfx::GetTextDimensions(24, mText.c_str(), &titleW, &titleH);
    Gfx::DrawText(24, CARD_X + (CARD_W - (int)titleW) / 2, CARD_Y + 40, COLOR_TEXT, mText.c_str());

    const AutoUpdater::Progress p = AutoUpdater::progress();
    // No total yet (headers still in flight) leaves the trough empty rather
    // than guessing a fraction.
    const float frac = p.total > 0 ? (float)p.downloaded / (float)p.total : 0.0f;

    Shapes::fillRound(BAR_X, BAR_Y, BAR_W, BAR_H, 0, COLOR_FILL2);
    const int fillW = (int)(BAR_W * (frac > 1.0f ? 1.0f : frac));
    if (fillW > 0) {
        Shapes::fillRound(BAR_X, BAR_Y, fillW, BAR_H, 0, COLOR_ACCENT);
    }

    char bytes[48];
    snprintf(bytes, sizeof(bytes), "%.1f / %.1f MB", p.downloaded / 1048576.0f, p.total / 1048576.0f);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)((frac > 1.0f ? 1.0f : frac) * 100));
    u32 pctW;
    Gfx::GetTextDimensions(20, pct, &pctW, NULL);
    Gfx::DrawText(20, BAR_X, BAR_Y + BAR_H + 8, COLOR_TEXT2, bytes);
    Gfx::DrawText(20, BAR_X + BAR_W - (int)pctW, BAR_Y + BAR_H + 8, COLOR_TEXT, pct);

    // Once the bytes are down the console is writing the artifact into place,
    // which reports nothing: say so instead of parking a full bar.
    const std::string hint = (p.total > 0 && p.downloaded >= p.total) ? i18n::t("updater.installing") : i18n::t("updater.keep_awake");
    u32 hintW;
    Gfx::GetTextDimensions(18, hint.c_str(), &hintW, NULL);
    Gfx::DrawText(18, CARD_X + (CARD_W - (int)hintW) / 2, CARD_Y + CARD_H - 44, COLOR_TEXT2, hint.c_str());
}

void UpdateOverlay::update(const InputState&)
{
    if (!mFinished.load(std::memory_order_acquire)) {
        return;
    }
    if (mWorker.joinable()) {
        mWorker.join();
    }

    if (mOutcome == AutoUpdater::Outcome::Installed) {
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
