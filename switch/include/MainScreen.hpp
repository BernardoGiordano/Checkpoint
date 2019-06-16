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

#ifndef MAINSCREEN_HPP
#define MAINSCREEN_HPP

#include "Screen.hpp"
#include "clickable.hpp"
#include "gui.hpp"
#include "hid.hpp"
#include "main.hpp"
#include "scrollable.hpp"

typedef enum { TITLES, CELLS } entryType_t;

class Clickable;
class Scrollable;

class MainScreen : public Screen {
public:
    MainScreen();
    void draw(void) const override;
    void update(touchPosition* touch) override;

protected:
    int selectorX(size_t i) const;
    int selectorY(size_t i) const;
    void updateSelector(touchPosition* touch);
    void handleEvents(touchPosition* touch);
    bool isBackupReleased(void) const;
    bool isRestoreReleased(void) const;
    bool isCheatReleased(void) const;
    std::string nameFromCell(size_t index) const;
    void entryType(entryType_t type);
    size_t index(entryType_t type) const;
    void index(entryType_t type, size_t i);
    void resetIndex(entryType_t type);
    bool getPKSMBridgeFlag(void) const;
    void setPKSMBridgeFlag(bool f);
    void updateButtons(void);

private:
    entryType_t type;
    int selectionTimer;
    bool pksmBridge;
    HidHorizontal hid;
    std::unique_ptr<Scrollable> backupList;
    std::unique_ptr<Clickable> buttonCheats, buttonBackup, buttonRestore;
    char ver[8];
};

#endif