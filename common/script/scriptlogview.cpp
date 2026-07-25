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

#include "scriptlogview.hpp"
#include "scriptconsole.hpp"
#include <algorithm>

ScriptLogView& ScriptLogView::get(void)
{
    static ScriptLogView view;
    return view;
}

void ScriptLogView::reset(void)
{
    mScrollBack = 0;
    mRows.clear();
    mSeenGeneration = 0;
    mSeenFirst      = 0;
}

void ScriptLogView::setViewportRows(int rows)
{
    mViewport = std::max(1, rows);
}

void ScriptLogView::scrollLines(int lines)
{
    // The furthest back the pane can go is the oldest line the console still
    // holds sitting on the top row.
    const int total   = (int)ScriptConsole::get().lineCount();
    const int maxBack = std::max(0, total - mViewport);
    mScrollBack       = std::clamp(mScrollBack + lines, 0, maxBack);
}

void ScriptLogView::scrollPages(int pages)
{
    scrollLines(pages * std::max(1, mViewport - 1));
}

ScriptLogView::Window ScriptLogView::window(void)
{
    ScriptConsole& console = ScriptConsole::get();

    Window w;
    w.total = console.lineCount();
    if (w.total == 0) {
        mRows.clear();
        return w;
    }

    // Pinned to the tail unless the user scrolled back, so a talking script
    // pushes the view along instead of scrolling out from under them. The
    // scrollback is clamped again here rather than trusted: the console drops
    // its oldest lines as it grows, which can move the tail under a scrollback
    // that was in range when it was set.
    const size_t maxFirst = w.total > (size_t)mViewport ? w.total - (size_t)mViewport : 0;
    const size_t back     = std::min((size_t)mScrollBack, maxFirst);
    w.first               = maxFirst - back;

    // Re-copy only when the content or the window actually moved.
    const unsigned gen = console.generation();
    if (gen != mSeenGeneration || w.first != mSeenFirst || mRows.size() != (size_t)mViewport) {
        console.copyWindow(w.first, (size_t)mViewport, mRows);
        mSeenGeneration = gen;
        mSeenFirst      = w.first;
    }

    // Scrollbar only once there is more than one screenful. maxFirst is then
    // non-zero, so the travel fraction is safe to divide.
    w.scrollbar = w.total > (size_t)mViewport;
    if (w.scrollbar) {
        w.thumbSpan   = (float)mViewport / (float)w.total;
        w.thumbTravel = (float)w.first / (float)maxFirst;
    }
    return w;
}
