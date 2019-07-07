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

#include "CheatManagerOverlay.hpp"
#include "ErrorOverlay.hpp"
#include "InfoOverlay.hpp"
#include "Screen.hpp"
#include "YesNoOverlay.hpp"
#include "clickable.hpp"
#include "gui.hpp"
#include "multiselection.hpp"
#include "scrollable.hpp"
#include "thread.hpp"
#include <memory>
#include <tuple>

class MainScreen : public Screen {
public:
    MainScreen(void);
    ~MainScreen(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(touchPosition* touch) override;

protected:
    int selectorX(size_t i) const;
    int selectorY(size_t i) const;
    void drawSelector(void) const;
    void handleEvents(touchPosition* touch);
    void updateSelector(void);
    void updateButtons(void);
    std::string nameFromCell(size_t index) const;

private:
    HidHorizontal hid;
    std::unique_ptr<Clickable> buttonBackup, buttonRestore, buttonCheats, buttonPlayCoins;
    std::unique_ptr<Scrollable> directoryList;
    char ver[10];

    C2D_Text ins1, ins2, ins3, ins4, c2dId, c2dMediatype;
    C2D_Text checkpoint, version;
    // instructions text
    C2D_Text top_move, top_a, top_y, top_my, top_b, bot_ts, bot_x;
    C2D_TextBuf dynamicBuf, staticBuf;

    const float scaleInst = 0.7f;
    C2D_ImageTint checkboxTint;
    int selectionTimer;
    int refreshTimer;
};

#endif