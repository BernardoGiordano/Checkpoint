/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#ifndef CHEATMANAGEROVERLAY_HPP
#define CHEATMANAGEROVERLAY_HPP

#include "Overlay.hpp"
#include "SDLHelper.hpp"
#include "YesNoOverlay.hpp"
#include "cheatmanager.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "directory.hpp"
#include "scrollable.hpp"
#include <memory>
#include <string>

class Clickable;
class Scrollable;

class CheatManagerOverlay : public Overlay {
public:
    CheatManagerOverlay(Screen& screen, const std::string& mtext);
    ~CheatManagerOverlay(void) {}
    void draw(void) const override;
    void update(PadState*) override;

protected:
    void save(const std::string& key, Scrollable* s);

private:
    bool multiSelected;
    std::string existingCheat;
    std::string key;
    const size_t MAGIC_LEN = strlen(SELECTED_MAGIC);
    std::shared_ptr<Scrollable> scrollable;
    size_t currentIndex;
};

#endif