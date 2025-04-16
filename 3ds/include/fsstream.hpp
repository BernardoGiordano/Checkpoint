/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef FSSTREAM_HPP
#define FSSTREAM_HPP

#include <3ds.h>
#include <string>

class FSStream {
public:
    FSStream(FS_Archive archive, const std::u16string& path, u32 flags);
    FSStream(FS_Archive archive, const std::u16string& path, u32 flags, u32 size);
    ~FSStream() = default;

    Result close(void);
    bool eof(void);
    bool good(void);
    void offset(u32 o);
    u32 offset(void);
    u32 read(void* buf, u32 size);
    Result result(void);
    u32 size(void);
    u32 write(const void* buf, u32 size);

private:
    Handle mHandle;
    u32 mSize;
    u32 mOffset;
    Result mResult;
    bool mGood;
};

#endif