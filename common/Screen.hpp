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

#ifndef SCREEN_HPP
#define SCREEN_HPP

#if defined(_3DS)
#include <3ds.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif
#include <memory>

#if defined(_3DS)
typedef touchPosition touchState;
#elif defined(__SWITCH__)
typedef HidTouchState touchState;
#endif

class Overlay;

class Screen {
    friend class Overlay;

public:
    Screen(void) {}
    virtual ~Screen(void) {}
    // Call currentOverlay->update if it exists, and update if it doesn't
    virtual void doUpdate(touchState* touch) final;
    virtual void update(touchState* touch) = 0;
    // Call draw, then currentOverlay->draw if it exists
#if defined(_3DS)
    virtual void doDrawTop(void) const final;
    virtual void doDrawBottom(void) const final;
    virtual void drawTop(void) const    = 0;
    virtual void drawBottom(void) const = 0;
#elif defined(__SWITCH__)
    virtual void doDraw() const final;
    virtual void draw() const = 0;
#endif
    void removeOverlay() { currentOverlay = nullptr; }
    void setOverlay(std::shared_ptr<Overlay>& overlay) { currentOverlay = overlay; }

protected:
    // No point in restricting this to only being editable during update, especially since it's drawn afterwards. Allows setting it before the first
    // draw loop is done
    mutable std::shared_ptr<Overlay> currentOverlay = nullptr;
};

#endif