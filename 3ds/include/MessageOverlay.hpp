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

#ifndef MESSAGEOVERLAY_HPP
#define MESSAGEOVERLAY_HPP

#include "Overlay.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "util.hpp"
#include <memory>
#include <string>

// One-message modal with a single OK button (A/B/tap dismisses). Owns the whole
// chrome; InfoOverlay and ErrorOverlay are just color/header configurations.
class MessageOverlay : public Overlay {
public:
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

protected:
    // Everything Info and Error differ by.
    struct Style {
        u32 outline, buttonBg, buttonFg, ring;
        std::string header; // accent line above the text (empty = none)
        u32 headerColor;
    };
    MessageOverlay(Screen& screen, const std::string& mtext, const Style& style);

private:
    static constexpr float SIZE = 0.6f;
    Style mStyle;
    std::string mText;
    u32 mPosx, mPosy;
    std::unique_ptr<Clickable> mButton;
};

class InfoOverlay : public MessageOverlay {
public:
    InfoOverlay(Screen& screen, const std::string& mtext);
};

class ErrorOverlay : public MessageOverlay {
public:
    ErrorOverlay(Screen& screen, Result res, const std::string& mtext);
};

#endif
