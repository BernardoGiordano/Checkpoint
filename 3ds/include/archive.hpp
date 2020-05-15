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

#ifndef ARCHIVE_HPP
#define ARCHIVE_HPP

#include "KeyboardManager.hpp"
#include "fsstream.hpp"
#include "util.hpp"
#include <3ds.h>

typedef enum { MODE_SAVE, MODE_EXTDATA } Mode_t;

namespace Archive {
    Result init(void);
    void exit(void);

    Mode_t mode(void);
    void mode(Mode_t v);
    FS_Archive sdmc(void);

    Result save(FS_Archive* archive, FS_MediaType mediatype, u32 lowid, u32 highid);
    Result rawSave(FSPXI_Archive* archive, FS_MediaType mediatype, u32 lowid, u32 highid);
    Result extdata(FS_Archive* archive, u32 extdata);
    bool accessible(FS_MediaType mediatype, u32 lowid, u32 highid); // save
    bool accessibleRaw(FS_MediaType mediatype, u32 lowid, u32 highid); // raw save
    bool accessible(u32 extdata);                                   // extdata
    bool setPlayCoins(void);
}

#endif