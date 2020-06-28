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

#ifndef APPDATA_HPP
#define APPDATA_HPP

#include <3ds.h>

#include <atomic>

#include "drawdata.hpp"
#include "inputdata.hpp"
#include "cheatmanager.hpp"
#include "title.hpp"
#include "backupable.hpp"
#include "action.hpp"

struct DataHolder {
    friend struct Checkpoint;
    friend void Action::performActionThreadFunc(void* arg);

    DataHolder() : draw(*this), input(*this) { }

    CheatManager& getCheats();

    void beginBackup(Backupable* on);
    void beginMultiBackup();
    void beginRestore(Backupable* on);

    void setAllMultiSelection();
    void clearMultiSelection();

    Backupable::ActionResult resultInfo;

    Thread actionThread;
    LightEvent shouldPerformAction;
    std::atomic_flag actionThreadKeepGoing = ATOMIC_FLAG_INIT;
    std::atomic_bool actionOngoing = false;

    std::vector<Title> titles;
    std::array<std::vector<std::unique_ptr<Backupable>>, BackupTypes::End> thingsToActOn;
    std::array<size_t, BackupTypes::End> multiSelectedCount;
    int selectedType = BackupTypes::First;

    Thread titleLoadingThread;
    LightEvent titleLoadingThreadBeginEvent;
    std::atomic_flag titleLoadingThreadKeepGoing = ATOMIC_FLAG_INIT;
    std::atomic_bool titleLoadingComplete = false;
    LightLock backupableVectorLock;

private: // sub-holders, they have a public reference to the parent holder, but not accessible from the parent to prevent loops
    std::unique_ptr<CheatManager> cheats;

    Action::Type actionType = Action::Type::Invalid;
    Backupable* actOn = nullptr;

    DrawDataHolder draw;
    InputDataHolder input;
};

#endif
