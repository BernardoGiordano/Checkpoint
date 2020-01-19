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

#ifndef ACCOUNT_HPP
#define ACCOUNT_HPP

#include "SDLHelper.hpp"
#include <map>
#include <string.h>
#include <string>
#include <switch.h>
#include <vector>

#define USER_ICON_SIZE 64

namespace std {
    template <>
    struct hash<AccountUid> {
        size_t operator()(const AccountUid& a) const { return ((hash<u64>()(a.uid[0]) ^ (hash<u64>()(a.uid[1]) << 1)) >> 1); }
    };
}

inline bool operator==(const AccountUid& x, const AccountUid& y)
{
    return x.uid[0] == y.uid[0] && x.uid[1] == y.uid[1];
}

inline bool operator==(const AccountUid& x, u64 y)
{
    return x.uid[0] == y && x.uid[1] == y;
}

inline bool operator<(const AccountUid& x, const AccountUid& y)
{
    return x.uid[0] < y.uid[0] && x.uid[1] == y.uid[1];
}

struct User {
    AccountUid id;
    std::string name;
    std::string shortName;
    SDL_Texture* icon;
};

namespace Account {
    Result init(void);
    void exit(void);

    std::vector<AccountUid> ids(void);
    SDL_Texture* icon(AccountUid id);
    AccountUid selectAccount(void);
    std::string username(AccountUid id);
    std::string shortName(AccountUid id);
}

#endif