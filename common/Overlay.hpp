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

#ifndef OVERLAY_HPP
#define OVERLAY_HPP

#include "Screen.hpp"
#include <memory>
#include <string>
#if defined(_3DS)
#include <3ds.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif

class Overlay {
public:
    Overlay(Screen& screen) : screen(screen), me(screen.currentOverlay) {}
    virtual ~Overlay() {}
    virtual void update(touchState* touch) = 0;
#if defined(_3DS)
    virtual void drawTop() const    = 0;
    virtual void drawBottom() const = 0;
#elif defined(__SWITCH__)
    virtual void draw() const = 0;
#endif

protected:
    Screen& screen;
    std::shared_ptr<Overlay>& me;
};

#endif