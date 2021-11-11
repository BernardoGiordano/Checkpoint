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

static std::map<AccountUid, User> mUsers;

Result Account::init(void)
{
    return accountInitialize(AccountServiceType_Application);
}

void Account::exit(void)
{
    for (auto& value : mUsers) {
        SDL_DestroyTexture(value.second.icon);
    }
    accountExit();
}

std::vector<AccountUid> Account::ids(void)
{
    std::vector<AccountUid> v;
    for (auto& value : mUsers) {
        v.push_back(value.second.id);
    }
    return v;
}

static User getUser(AccountUid id)
{
    User user{id, "", "", NULL};
    AccountProfile profile;
    AccountProfileBase profilebase;
    memset(&profilebase, 0, sizeof(profilebase));

    if (R_SUCCEEDED(accountGetProfile(&profile, id)) && R_SUCCEEDED(accountProfileGet(&profile, NULL, &profilebase))) {
        user.name      = std::string(profilebase.nickname, 0x20);
        user.shortName = trimToFit(user.name, 96 - g_username_dotsize * 2, 13);

        // load icon
        u8* buffer;
        u32 image_size, real_size;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &image_size)) && (buffer = (u8*)malloc(image_size)) != NULL &&
            R_SUCCEEDED(accountProfileLoadImage(&profile, buffer, image_size, &real_size))) {
            SDLH_LoadImage(&user.icon, buffer, image_size);
            free(buffer);
        }
    }

    accountProfileClose(&profile);
    return user;
}

std::string Account::username(AccountUid id)
{
    std::map<AccountUid, User>::const_iterator got = mUsers.find(id);
    if (got == mUsers.end()) {
        User user = getUser(id);
        mUsers.insert({id, user});
        return user.name;
    }

    return got->second.name;
}

std::string Account::shortName(AccountUid id)
{
    std::map<AccountUid, User>::const_iterator got = mUsers.find(id);
    if (got == mUsers.end()) {
        User user = getUser(id);
        mUsers.insert({id, user});
        return user.shortName;
    }

    return got->second.shortName;
}

SDL_Texture* Account::icon(AccountUid id)
{
    std::map<AccountUid, User>::const_iterator got = mUsers.find(id);
    if (got == mUsers.end()) {
        User user = getUser(id);
        mUsers.insert({id, user});
        return user.icon;
    }
    return got->second.icon;
}

AccountUid Account::selectAccount(void)
{
    AccountUid out_id;
    LibAppletArgs args;
    libappletArgsCreate(&args, 0x10000);
    u8 st_in[0xA0]  = {0};
    u8 st_out[0x18] = {0};
    size_t repsz;

    Result res = libappletLaunch(AppletId_LibraryAppletPlayerSelect, &args, st_in, 0xA0, st_out, 0x18, &repsz);
    if (R_SUCCEEDED(res)) {
        u64 lres       = *(u64*)st_out;
        AccountUid uid = *(AccountUid*)&st_out[8];
        if (lres == 0)
            out_id = uid;
    }

    return out_id;
}