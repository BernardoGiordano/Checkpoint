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

#ifndef CLICKABLE_HPP
#define CLICKABLE_HPP

#include "SDLHelper.hpp"
#include "iclickable.hpp"
#include "main.hpp"
#include <string>
#include <switch.h>

class Clickable : public IClickable<SDL_Color> {
public:
    Clickable(int x, int y, u16 w, u16 h, SDL_Color colorBg, SDL_Color colorText, const std::string& message, bool centered)
        : IClickable(x, y, w, h, colorBg, colorText, message, centered)
    {
    }
    virtual ~Clickable(void){};

    void draw(float font, SDL_Color overlay) override;
    bool held(void) override;
    bool released(void) override;
    void drawOutline(SDL_Color color) override;
};

#endif