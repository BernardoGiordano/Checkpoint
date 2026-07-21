/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

#include "scriptuibridge.hpp"

UiResponse ScriptUiBridge::request(UiRequest req)
{
    std::unique_lock<std::mutex> lock(mMutex);
    if (mCancelled) {
        // Abort in progress: don't park, hand back "cancelled" so the script
        // reaches its next statement (where picoc's abort hook fails the run).
        return UiResponse{};
    }
    mReq = std::move(req);
    mCv.wait(lock, [this] { return mResp.has_value(); });
    UiResponse out = std::move(*mResp);
    mReq.reset();
    mResp.reset();
    return out;
}

void ScriptUiBridge::setStatus(std::string text)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mStatus = std::move(text);
}

const UiRequest* ScriptUiBridge::pending(void)
{
    std::lock_guard<std::mutex> lock(mMutex);
    return (mReq && !mResp) ? &*mReq : nullptr;
}

void ScriptUiBridge::respond(UiResponse resp)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mResp = std::move(resp);
    }
    mCv.notify_all();
}

void ScriptUiBridge::cancelAll(void)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCancelled = true;
        if (mReq && !mResp) {
            mResp = UiResponse{};
        }
    }
    mCv.notify_all();
}

std::string ScriptUiBridge::statusText(void)
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mStatus;
}

void ScriptUiBridge::reset(void)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mStatus.clear();
    mReq.reset();
    mResp.reset();
    mCancelled = false;
}
