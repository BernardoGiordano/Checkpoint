/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

#ifndef SMDH_HPP
#define SMDH_HPP

#include <3ds.h>

typedef struct {
    u32 magic;
    u16 version;
    u16 reserved;
} smdhHeader_s;

typedef struct {
    u16 shortDescription[0x40];
    u16 longDescription[0x80];
    u16 publisher[0x40];
} smdhTitle_s;

typedef struct {
    u8 gameRatings[0x10];
    u32 regionLock;
    u8 matchMakerId[0xC];
    u32 flags;
    u16 eulaVersion;
    u16 reserved;
    u32 defaultFrame;
    u32 cecId;
} smdhSettings_s;

typedef struct {
    smdhHeader_s header;
    smdhTitle_s applicationTitles[16];
    smdhSettings_s settings;
    u8 reserved[0x8];
    u8 smallIconData[0x480];
    u16 bigIconData[0x900];
} smdh_s;

smdh_s* loadSMDH(u32 low, u32 high, u8 media);

#endif