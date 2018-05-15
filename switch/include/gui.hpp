/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

#include <math.h>
#include <switch.h>
#include <string.h>
#include "account.hpp"
#include "colors.hpp"
#include "clickable.hpp"
#include "draw.hpp"
#include "info.hpp"
#include "hid.hpp"
#include "messagebox.hpp"
#include "scrollable.hpp"
#include "title.hpp"
#include "types.hpp"
#include "util.hpp"

#include "checkbox_grey_bin.h"
#include "checkbox_white_bin.h"
#include "flag_bin.h"

namespace Gui
{
    void        init(void);
    void        exit(void);
    void        draw(void);

    void        createInfo(const std::string& title, const std::string& message);
    void        createError(Result res, const std::string& message);

    bool        backupScroll(void);
    void        backupScroll(bool enable);
    size_t      count(entryType_t type);
    size_t      index(entryType_t type);
    void        index(entryType_t type, size_t i);
    bool        isBackupReleased(void);
    bool        isRestoreReleased(void);
    std::string nameFromCell(entryType_t type, size_t index);
    void        resetIndex(entryType_t type);
    void        updateButtonsColor(void);
    void        updateSelector(void);

    bool        askForConfirmation(const std::string& text);
    void        drawCopy(const std::string& src, u64 offset, u64 size);

    void        addSelectedEntry(size_t idx);
    void        clearSelectedEntries(void);
    bool        multipleSelectionEnabled(void);
    std::vector
    <size_t>    selectedEntries(void);
}

#endif