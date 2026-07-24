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

#ifndef SCRIPTREQUESTOVERLAYS_HPP
#define SCRIPTREQUESTOVERLAYS_HPP

#include "ListCursor.hpp"
#include "Overlay.hpp"
#include "ScriptTile.hpp"
#include "clickable.hpp"
#include "scriptuibridge.hpp"
#include <memory>
#include <string>
#include <vector>

// The overlays ScriptScreen raises for a script's blocking UI requests. Each one
// answers exactly one pending ScriptUiBridge request: it responds *before*
// dismissing itself, so the script thread wakes as the overlay goes away.
//
// All of them own the bottom-screen tile and nothing else — drawTop() is empty
// on purpose, because the top screen is the log tile and a dialog must never
// cover the transcript the user is reading. That is why these are not
// ChoiceOverlay / ListPickerOverlay configurations: those dim and draw across
// both screens.
//
// They share one shape — header, body, hint line — so a message, a confirm and
// the two pickers read as the same dialog changing its body, and the progress
// bars the idle tile shows are the only thing a dialog takes away.

// Shared chrome: the tile frame, its title header, and the hint line.
// Subclasses fill the body and decide the input policy.
class ScriptTileOverlay : public Overlay {
public:
    void drawTop(void) const override {}
    void drawBottom(void) const override;

protected:
    // `title` is the header line. A short prompt is a good title; a whole
    // message is not, which is why the message/confirm dialogs title themselves
    // with the script's name and wrap their text in the body instead.
    ScriptTileOverlay(Screen& screen, std::string title);

    // The tile's body, between the header hairline and the hint line.
    virtual void drawBody(int bodyY) const = 0;
    virtual std::string hints(void) const  = 0;
    // Right-aligned header note (a "n / total" counter for the list pickers).
    virtual std::string headerNote(void) const { return ""; }

    // Answers the bridge, then dismisses this overlay. Touch no member after it.
    void answer(UiResponse resp);

    // Wrapped body text, from `bodyY` down to the button row. Text taller than
    // that is cut to what fits, ending in "...".
    void drawWrappedBody(const std::string& text, int bodyY) const;

    std::string mTitle;
};

// gui_message: one wrapped message and an OK button.
class ScriptMessageOverlay : public ScriptTileOverlay {
public:
    ScriptMessageOverlay(Screen& screen, std::string text);
    void update(const InputState& input) override;

private:
    void drawBody(int bodyY) const override;
    std::string hints(void) const override;

    std::string mText;
    std::unique_ptr<Clickable> mButton;
};

// gui_confirm: confirm / cancel, A activates the highlighted button, B cancels.
// Same button order and default highlight as YesNoOverlay, which this replaces
// for scripts (YesNoOverlay draws across both screens).
class ScriptConfirmOverlay : public ScriptTileOverlay {
public:
    ScriptConfirmOverlay(Screen& screen, std::string text);
    void update(const InputState& input) override;

private:
    void drawBody(int bodyY) const override;
    std::string hints(void) const override;

    std::string mText;
    std::unique_ptr<Clickable> mConfirm, mCancel;
    bool mConfirmSelected = true;
};

// Shared list body for the two pickers: rows, selection highlight and paging.
class ScriptListOverlay : public ScriptTileOverlay {
public:
    // Rows that fit between the header hairline and the hint band. The pickers
    // have no button row (A / START / B do the work), so they get one more row
    // than the message and confirm dialogs have body.
    static constexpr size_t VISIBLE = 6;

protected:
    ScriptListOverlay(Screen& screen, std::string prompt, size_t count);

    void drawBody(int bodyY) const override;
    std::string headerNote(void) const override;

    // Row `k`, whose band starts at `rowY` and is ROW_H tall.
    virtual void drawRow(size_t k, int rowY, bool selected) const = 0;

    static constexpr int ROW_H = 26;

    size_t mCount;
    // ListCursor, not Hid: Hid binds L/R to page the list, and during a script
    // run L/R belong to the log pane. D-Pad only here.
    ListCursor mCursor;
};

// gui_pick_one: A responds with the row index, B with -1.
class ScriptPickOneOverlay : public ScriptListOverlay {
public:
    ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items);
    void update(const InputState& input) override;

private:
    void drawRow(size_t k, int rowY, bool selected) const override;
    std::string hints(void) const override;

    std::vector<std::string> mItems;
};

// gui_pick_many: A toggles the row, X confirms the set, B cancels.
class ScriptPickManyOverlay : public ScriptListOverlay {
public:
    ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected);
    void update(const InputState& input) override;

private:
    void drawRow(size_t k, int rowY, bool selected) const override;
    std::string hints(void) const override;

    std::vector<std::string> mItems;
    std::vector<bool> mSelected;
};

#endif
