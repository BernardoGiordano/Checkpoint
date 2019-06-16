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

#ifndef GUI_HPP
#define GUI_HPP

#include "MainScreen.hpp"
#include "SDLHelper.hpp"
#include "Screen.hpp"
#include "account.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "hid.hpp"
#include "main.hpp"
#include "multiselection.hpp"
#include "scrollable.hpp"
#include "title.hpp"
#include "util.hpp"
#include <math.h>
#include <stack>
#include <string.h>
#include <switch.h>

namespace Gui {
    void showInfo(const std::string& message);
    void showError(Result res, const std::string& message);

    bool askForConfirmation(const std::string& text);
    void drawCopy(const std::string& src, u64 offset, u64 size);

    void setScreen(std::unique_ptr<Screen> screen);
}

#endif