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

#include "account.hpp"

static std::map<u128, User> mUsers;

Result Account::init(void)
{
    return accountInitialize();
}

void Account::exit(void)
{
    for (auto& value : mUsers)
    {
        SDL_DestroyTexture(value.second.icon);
    }
    accountExit();
}

std::vector<u128> Account::ids(void)
{
    std::vector<u128> v;
    for (auto& value : mUsers)
    {
        v.push_back(value.second.id);
    }
    return v;
}

static User getUser(u128 id)
{
    User user{ id, "", NULL };
    AccountProfile profile;
    AccountProfileBase profilebase;
    memset(&profilebase, 0, sizeof(profilebase));

    if (R_SUCCEEDED(accountGetProfile(&profile, id)) && 
        R_SUCCEEDED(accountProfileGet(&profile, NULL, &profilebase)))
    {
        user.name = std::string(profilebase.username, 0x20);

        //load icon
        u8* buffer;
        size_t image_size, real_size;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &image_size)) &&
            (buffer = (u8*)malloc(image_size)) != NULL &&
            R_SUCCEEDED(accountProfileLoadImage(&profile, buffer, image_size, &real_size)))
        {
            SDLH_LoadImage(&user.icon, buffer, image_size);
            free(buffer);
        }
    }

    accountProfileClose(&profile);
    return user;
}

std::string Account::username(u128 id)
{
    std::map<u128, User>::const_iterator got = mUsers.find(id);
    if (got == mUsers.end())
    {
        User user = getUser(id);
        mUsers.insert({id, user});
        return user.name;
    }

    return got->second.name;
}

SDL_Texture* Account::icon(u128 id)
{
    std::map<u128, User>::const_iterator got = mUsers.find(id);
    // TODO: remove the null check
    // the null check in the icon is to handle the bad memory issue that isn't yet fixed
    if (got == mUsers.end() || got->second.icon == NULL)
    {
        User user = getUser(id);
        mUsers.insert({id, user});
        return user.icon;
    }
    return got->second.icon;
}