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

#ifndef SCRIPTLOGVIEW_HPP
#define SCRIPTLOGVIEW_HPP

#include <cstddef>
#include <string>
#include <vector>

// Which part of the transcript the log pane is looking at. ScriptConsole holds
// every line; this holds the viewport over them — how many rows fit, how far
// back the user scrolled, and which rows that makes visible this frame.
//
// The scroll arithmetic is the part with the off-by-ones in it (tail pinning,
// page steps that keep a row of context, thumb geometry), and it is the same on
// both consoles: only the pixels differ. So it lives here once, and each
// ScriptScreen keeps nothing but its draw calls.
//
// Main thread only. Exactly one script runs at a time, so the viewport is a
// singleton the same way the console is: main.cpp scrolls it without holding
// the screen, and the screen reads it from a const draw.
class ScriptLogView {
public:
    static ScriptLogView& get(void);

    // Everything a frame needs to draw the pane, from rows() plus the geometry
    // below. Thumb metrics are fractions rather than pixels because the two
    // platforms differ only in their track rectangle and their minimum thumb
    // size, which they apply themselves.
    struct Window {
        // Index into the console of the first visible line, and how many lines
        // it holds in total.
        size_t first = 0;
        size_t total = 0;
        // Whether there is more than one screenful, i.e. whether to draw a
        // scrollbar at all.
        bool scrollbar = false;
        // Thumb height as a fraction of the track (0..1).
        float thumbSpan = 1.0f;
        // How far down its free travel the thumb sits: 0 at the oldest held
        // line, 1 pinned to the newest.
        float thumbTravel = 0.0f;
    };

    // Pins back to the tail. Called when a run's screen opens, so a new run
    // never starts scrolled into the previous one's output.
    void reset(void);

    // How many rows the pane can draw. The screens derive it from their own tile
    // geometry and font metrics each frame, and it feeds the page step and the
    // tail-pinning below. Values below 1 clamp to 1.
    void setViewportRows(int rows);
    int viewportRows(void) const { return mViewport; }

    // Positive scrolls back into history, negative towards the newest line.
    // Clamped so the oldest held line is the furthest back the pane can go.
    void scrollLines(int lines);
    // A page keeps one row of context, the way a pager does.
    void scrollPages(int pages);

    // Rows scrolled back from the tail; 0 means pinned to the newest line. The
    // header's "-N" marker.
    int scrollBack(void) const { return mScrollBack; }

    // Recomputes the visible window and refreshes rows() if the content or the
    // window moved. Cheap to call every frame: at 60 fps a chatty script would
    // otherwise cost a few hundred string copies a frame.
    Window window(void);

    // The lines window() selected, oldest first. Valid until the next window().
    const std::vector<std::string>& rows(void) const { return mRows; }

private:
    ScriptLogView(void) = default;

    int mViewport   = 1;
    int mScrollBack = 0;

    // Cached copy of the visible lines, plus what it was copied for.
    std::vector<std::string> mRows;
    unsigned mSeenGeneration = 0;
    size_t mSeenFirst        = 0;
};

#endif
