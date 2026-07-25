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

#include "Overlay.hpp"
#include "ScriptTile.hpp"
#include "scriptuibridge.hpp"
#include <string>
#include <vector>

// The overlays ScriptScreen raises for a script's blocking UI requests. Each one
// answers exactly one pending ScriptUiBridge request: it responds *before*
// dismissing itself, so the script thread wakes as the overlay goes away.
//
// All of them draw the same centred card over a dimmed transcript, so a
// message, a confirm and the two pickers are one dialog that changes its body
// rather than four differently shaped boxes. They are not ModalChrome dialogs
// because ModalChrome sizes itself to a paragraph of text: a picker needs a
// fixed row band and a stable header counter, which is what ScriptTile gives.

// Shared chrome: the tile frame, its title header, and the hint line.
// Subclasses fill the body and decide the input policy.
class ScriptTileOverlay : public Overlay {
public:
    void draw(void) const override;
    // Y is the same key in every script dialog — it hands the screen to the
    // transcript without answering — so it is handled once here and the
    // per-dialog keys live in handleInput. While the log has focus this
    // overlay draws nothing and eats nothing: the request just stays pending.
    void update(const InputState& input) final;

protected:
    // `title` is the header line. A short prompt is a good title; a whole
    // message is not, which is why the message/confirm dialogs title themselves
    // with the script's name and wrap their text in the body instead.
    ScriptTileOverlay(Screen& screen, std::string title);

    // The tile's body, between the header hairline and the hint line.
    virtual void drawBody(int bodyY) const = 0;
    virtual std::string hints(void) const  = 0;
    // This dialog's own keys. Only called while the card is on screen.
    virtual void handleInput(const InputState& input) = 0;
    // Right-aligned header note (a "n / total" counter for the list pickers).
    virtual std::string headerNote(void) const { return ""; }

    // Answers the bridge, then dismisses this overlay. Touch no member after it.
    void answer(UiResponse resp);

    // Wrapped body text, from `bodyY` down towards the button row.
    void drawWrappedBody(const std::string& text, int bodyY) const;

    // The button row along the tile's lower edge, above the hint line.
    static constexpr int BTN_H = 56, BTN_GAP = 16;
    static int buttonRowY(void) { return ScriptTile::uiBodyBottom() - BTN_H - 12; }
    void drawButton(int x, int w, const std::string& label, bool focused) const;

    std::string mTitle;
};

// gui_message: one wrapped message and an OK button.
class ScriptMessageOverlay : public ScriptTileOverlay {
public:
    ScriptMessageOverlay(Screen& screen, std::string text);
    void handleInput(const InputState& input) override;

private:
    void drawBody(int bodyY) const override;
    std::string hints(void) const override;

    std::string mText;
};

// gui_confirm: confirm / cancel, A activates the highlighted button, B cancels.
class ScriptConfirmOverlay : public ScriptTileOverlay {
public:
    ScriptConfirmOverlay(Screen& screen, std::string text);
    void handleInput(const InputState& input) override;

private:
    void drawBody(int bodyY) const override;
    std::string hints(void) const override;

    std::string mText;
    bool mConfirmSelected = true;
};

// Shared list body for the two pickers: rows, focus ring and scrolling.
class ScriptListOverlay : public ScriptTileOverlay {
protected:
    ScriptListOverlay(Screen& screen, std::string prompt, size_t count);

    void drawBody(int bodyY) const override;
    std::string headerNote(void) const override;

    // Row `k`, whose band starts at `rowY` and is ROW_H tall.
    virtual void drawRow(size_t k, int rowY, bool focused) const = 0;

    // D-Pad movement, wrapping at both ends; keeps the cursor in view.
    void moveCursor(const InputState& input);

    static constexpr int ROW_H = 52, ROW_GAP = 6;
    int visibleRows(void) const;

    size_t mCount;
    size_t mCursor = 0;
    size_t mScroll = 0;
};

// gui_pick_one: A responds with the row index, B with -1.
class ScriptPickOneOverlay : public ScriptListOverlay {
public:
    ScriptPickOneOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items);
    void handleInput(const InputState& input) override;

private:
    void drawRow(size_t k, int rowY, bool focused) const override;
    std::string hints(void) const override;

    std::vector<std::string> mItems;
};

// gui_pick_many: A toggles the row, X confirms the set, B cancels.
class ScriptPickManyOverlay : public ScriptListOverlay {
public:
    ScriptPickManyOverlay(Screen& screen, const std::string& prompt, std::vector<std::string> items, std::vector<bool> preselected);
    void handleInput(const InputState& input) override;

private:
    void drawRow(size_t k, int rowY, bool focused) const override;
    std::string hints(void) const override;

    std::vector<std::string> mItems;
    std::vector<bool> mSelected;
};

#endif
