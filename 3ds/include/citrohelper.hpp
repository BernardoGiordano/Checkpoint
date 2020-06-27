/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#ifndef CITROHELPER_HPP
#define CITROHELPER_HPP

#include <citro3d.h>
#include <citro2d.h>

#include "colors.hpp"

class CTRH {
public:
    CTRH();
    ~CTRH();

    static inline C2D_Image wifiSlotIcons[3];

    void staticText(C2D_Text* t, const char* msg);
    void dynamicText(C2D_Text* t, const char* msg);

    void beginFrame();
    void clear(u32 clearColor);
    void targetTop();
    void targetBottom();
    void endFrame();

    float getTimer() const;

    C2D_Image getImage(size_t idx) const;

    void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color) const;
    void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color) const;

private:
    C3D_RenderTarget* top;
    C3D_RenderTarget* bottom;

    C2D_SpriteSheet spritesheet;
    C2D_TextBuf staticBuf, dynamicBuf;

    float timer;
};

#endif
