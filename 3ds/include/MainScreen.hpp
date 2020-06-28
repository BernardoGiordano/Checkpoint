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

#ifndef MAINSCREEN_HPP
#define MAINSCREEN_HPP

#include <citro2d.h>

#include "appdata.hpp"
#include "ihid.hpp"
#include "scrollable.hpp"
#include "clickable.hpp"
#include "dualscreen.hpp"

class MainScreen : public DualScreenScreen {
public:
    MainScreen(DrawDataHolder& d);

private:
    virtual void update(InputDataHolder& input) final;
    virtual void drawTop(DrawDataHolder& d) const final;
    virtual void drawBottom(DrawDataHolder& d) const final;

    void drawSelector(DrawDataHolder& d, int x, int y) const;
    void updateButtons(InputDataHolder& input);
    void reloadDirectoryList(InputDataHolder& input);
    void performBackup(InputDataHolder& input);
    void performMultiBackup(InputDataHolder& input);
    void performRestore(InputDataHolder& input);

    Hid<HidDirection::HORIZONTAL, HidDirection::VERTICAL> hid;
    Clickable buttonBackup, buttonRestore, buttonExtra;
    Scrollable directoryList;

    char ver[10];
    bool inInstructions;
    bool enteredTarget;

    C2D_Text ins1, ins3, ins4, c2dId, c2dMediatype;
    C2D_Text checkpoint, version;
    // instructions text
    C2D_Text top_move, top_a, top_y, top_my, top_b, bot_ts, bot_x, coins;
    static constexpr float scaleInst = 0.7f;
    C2D_ImageTint checkboxTint;
    size_t countForCurrentFrame, previousIndex;
    u64 holdStartTime;
};

#endif
