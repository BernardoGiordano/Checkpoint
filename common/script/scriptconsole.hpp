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

#ifndef SCRIPTCONSOLE_HPP
#define SCRIPTCONSOLE_HPP

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// What a running script has said, and how far along it is. The one-way half of
// the script/UI seam: the script worker thread writes, the UI thread reads
// snapshots, and a writer never waits on a frame — the counterpart to
// ScriptUiBridge, which is the *blocking* half (a request parks the script
// until the user answers). Shaped after TransferStatus for the same reason:
// long IO on a worker thread has to keep running at full speed while the bars
// animate on the main thread.
//
// Lines are hard-wrapped as they arrive, to the column count the platform sets
// once at startup from its log tile width. The pane renders in a monospace
// font, so one stored line is exactly one rendered row: scrolling needs no wrap
// cache and the UI copies only the rows it is about to draw.
class ScriptConsole {
public:
    // Progress layers a script may drive, outermost (0) first. Three is what
    // the deepest existing UI (the multi-backup modal: saves / files / bytes)
    // needs, and more than that stops reading as progress.
    static constexpr size_t MAX_LAYERS = 3;

    // One progress bar. `total <= 0` means "no known total": the pane draws an
    // indeterminate bar and shows the raw `done` count instead of a percentage.
    struct Layer {
        std::string label;
        long long done  = 0;
        long long total = 0;
        bool active     = false;
    };

    // Flat copy for the UI, cheap enough to take every frame.
    struct ProgressSnapshot {
        Layer layers[MAX_LAYERS];
        // The reserved innermost bar, driven by the native long-running
        // bindings (uploads, zip/unzip) rather than by script code, so a script
        // gets a byte-level bar under its own item-level bar for free.
        Layer io;
        // One past the deepest active script layer (0 when none are active).
        size_t layerCount = 0;
        // How many bar slots the run has claimed so far: one past the deepest
        // layer ever begun, and whether the IO bar was ever begun. The UI draws
        // a slot even while its layer is idle — a script that begins an inner
        // bar per item would otherwise make the whole stack jump by a row
        // between items, which reads as flicker and moves the status line
        // under the user's eyes. Cleared by reset() and clearProgress().
        size_t layerSlots = 0;
        bool ioSlot       = false;
    };

    static ScriptConsole& get(void);

    // ---- main thread, setup -------------------------------------------------

    // Column count new lines are wrapped to. Set once at startup from the log
    // tile geometry; lines already stored keep the wrapping they arrived with.
    void setWidth(size_t columns);

    // Drops every line and every bar. Called before each run.
    void reset(void);

    // ---- script thread (and native bindings): never blocks -----------------

    // Raw output as it is produced. Splits on '\n', expands tabs, drops other
    // control characters, and wraps to the configured width.
    void write(const char* data, size_t len);

    // One complete line (the `log()` binding). Embedded newlines split as usual.
    void log(const std::string& text);

    // Starts (or restarts) `layer` at 0 of `total`. Deeper layers are ended,
    // so a script cannot leave a stale inner bar behind when it moves on to the
    // next outer item. Out-of-range layers are ignored.
    void beginLayer(size_t layer, std::string label, long long total);
    void setLayer(size_t layer, long long done);
    void setLayerLabel(size_t layer, std::string label);
    // Ends `layer` and every layer deeper than it.
    void endLayer(size_t layer);
    void clearProgress(void);

    // The reserved innermost bar. Native bindings frame their own IO with
    // these; scripts never touch them.
    void beginIo(std::string label, long long total);
    void setIo(long long done);
    void addIo(long long delta);
    void endIo(void);

    // ---- main thread: reads ------------------------------------------------

    // Bumped on every line appended or replaced, so a view can tell "nothing
    // new" apart from "same count, different content" without copying.
    unsigned generation(void) const;

    size_t lineCount(void) const;

    // Copies at most `count` lines starting at index `first` (0 = oldest held
    // line) into `out`, which is cleared first. Out-of-range requests clamp.
    void copyWindow(size_t first, size_t count, std::vector<std::string>& out) const;

    ProgressSnapshot progress(void) const;

    // The last `maxBytes` of output as one string, for the run's Outcome.
    std::string tail(size_t maxBytes) const;

private:
    ScriptConsole(void) = default;

    // Appends one character to the last row, opening a new row when the current
    // one is closed (a '\n' arrived) or full. Caller holds the lock.
    void appendChar(char c);

    // Most recent lines kept. Past this the oldest are dropped: a script in a
    // print loop must not grow the heap without bound, and no one scrolls back
    // further than this anyway.
    static constexpr size_t MAX_LINES = 250;
    // Fallback until setWidth() runs.
    static constexpr size_t DEFAULT_WIDTH = 56;

    mutable std::mutex mMutex;
    // Every row, wrapped. Columns are counted in bytes, so a non-ASCII line
    // wraps early — the pane's monospace font is ASCII anyway, and script
    // output is overwhelmingly ASCII.
    std::deque<std::string> mLines;
    // Whether the last row is still open: output that has not been
    // newline-terminated yet keeps landing in it, so an unterminated printf is
    // visible as it is written rather than only once the line ends.
    bool mOpen    = false;
    size_t mWidth = DEFAULT_WIDTH;
    unsigned mGen = 0;
    Layer mLayers[MAX_LAYERS];
    Layer mIo;
    // High-water marks behind ProgressSnapshot's slot counts.
    size_t mLayerSlots = 0;
    bool mIoSlot       = false;
};

#endif
