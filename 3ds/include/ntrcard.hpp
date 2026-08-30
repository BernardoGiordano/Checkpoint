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
 */

#ifndef NTRCARD_HPP
#define NTRCARD_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace NtrCard {
    // 3DS IO registers at physical 0x10100000 are mapped into an ARM11 process
    // by adding 0x0EB00000. Checkpoint's CIA requests this page in app.rsf.
    inline constexpr std::uintptr_t physicalRegisterBase = 0x10164000;
    inline constexpr std::uintptr_t processRegisterBase  = 0x1EC64000;
    inline constexpr std::size_t sectorSize              = 0x200;
    inline constexpr std::size_t nandWindowSize          = 128 * 1024;
    inline constexpr std::size_t sectorWords             = sectorSize / sizeof(std::uint32_t);
    inline constexpr std::size_t nandWindowSectors       = nandWindowSize / sectorSize;
    inline constexpr std::size_t nandWindowWords         = nandWindowSize / sizeof(std::uint32_t);
    // The cartridge programs NAND a page at a time, and a page is four sectors.
    // Reads may be any sector count; a write is all 0x800 bytes or nothing.
    inline constexpr std::size_t nandPageSize    = 0x800;
    inline constexpr std::size_t nandPageWords   = nandPageSize / sizeof(std::uint32_t);
    inline constexpr std::size_t nandPageSectors = nandPageSize / sectorSize;

    // ROMCTRL's data block size field holds n, and one transfer moves
    // 0x100 << n bytes. A single sector is n=1 (0x200). Larger blocks read
    // several consecutive sectors per B7, which is the only lever that shortens
    // a dump without changing which bytes are asked of the cartridge, so it is
    // the one thing worth trying against a bus this module cannot hold for
    // long. Whether this cartridge streams past a sector boundary is a
    // hardware question: callers must compare a large-block read against a
    // known-good single-sector read before trusting one.
    inline constexpr std::size_t maxNandBlockSectors = 8;

    enum class RegisterOffset : std::uintptr_t {
        MCardControl = 0x00,
        RomControl   = 0x04,
        // Command is big-endian on the wire: first four bytes live at +0x08.
        CommandHigh = 0x08,
        CommandLow  = 0x0C,
        SeedXLow    = 0x10,
        SeedYLow    = 0x14,
        SeedXHigh   = 0x18,
        SeedYHigh   = 0x1A,
        Fifo        = 0x1C,
    };

    struct RegisterSnapshot {
        std::uint16_t mCardControl;
        std::uint32_t romControl;
        std::uint32_t commandHigh;
        std::uint32_t commandLow;
        std::uint32_t seedXLow;
        std::uint32_t seedYLow;
        std::uint16_t seedXHigh;
        std::uint16_t seedYHigh;
    };

    // Internal Seam. MappedCardBus is production Adapter;
    // RecordingRegisterReader in host test proves capture() reads only passive
    // registers and never consumes FIFO or writes hardware.
    class RegisterReader {
    public:
        virtual ~RegisterReader()                                 = default;
        virtual std::uint16_t read16(RegisterOffset offset) const = 0;
        virtual std::uint32_t read32(RegisterOffset offset) const = 0;
    };

    // Internal Seam for bounded transfers. Production maps the shared ARM11 IO
    // page; host tests use a deterministic state machine. No cartridge-write
    // operation is exposed.
    class CardBus : public RegisterReader {
    public:
        virtual std::array<std::uint8_t, 8> readCommand(void) const           = 0;
        virtual void write16(RegisterOffset offset, std::uint16_t value)      = 0;
        virtual void write32(RegisterOffset offset, std::uint32_t value)      = 0;
        virtual void writeCommand(const std::array<std::uint8_t, 8>& command) = 0;
        virtual std::uint64_t ticks(void) const                               = 0;
    };

    enum class SectorReadStatus {
        Ok,
        InvalidArgument,
        ControllerDisabled,
        ControllerBusy,
        // Someone else took the controller: MCNT no longer holds the value this
        // module set, or ROMCNT changed while a transfer was running.
        ControllerStateChanged,
        // ROMCNT lost its nRESET bit mid-transfer: the host reset the card
        // interface. Distinct from the above because a reset cartridge is back
        // in its power-on ROM mode, so there is nothing to return to ROM mode
        // and no reason to warn about unknown cartridge state.
        ControllerReset,
        Timeout,
        WordCountMismatch,
    };

    struct SectorReadResult {
        SectorReadStatus status = SectorReadStatus::InvalidArgument;
        std::array<std::uint32_t, sectorSize / sizeof(std::uint32_t)> data{};
        std::size_t wordsRead = 0;
        // Words pulled out of the FIFO purely to unwedge an abandoned transfer.
        std::size_t drainedWords           = 0;
        std::uint16_t initialMCardControl  = 0;
        std::uint32_t initialRomControl    = 0;
        std::uint16_t observedMCardControl = 0;
        std::uint32_t observedRomControl   = 0;
        bool commandReadbackAvailable      = false;
        bool commandIdentityChanged        = false;
        std::uint32_t expectedCommandHigh  = 0;
        std::uint32_t expectedCommandLow   = 0;
        std::uint32_t observedCommandHigh  = 0;
        std::uint32_t observedCommandLow   = 0;
        std::uint32_t transferRomControl   = 0;
        std::uint32_t completedRomControl  = 0;
    };

    enum class NandReadStage {
        Preflight,
        SelectWindow,
        ReadSector,
        ReturnToRom,
        Complete,
    };

    struct NandSectorReadResult {
        SectorReadStatus status            = SectorReadStatus::InvalidArgument;
        NandReadStage stage                = NandReadStage::Preflight;
        SectorReadStatus returnToRomStatus = SectorReadStatus::InvalidArgument;
        bool returnToRomAttempted          = false;
        std::array<std::uint32_t, sectorSize / sizeof(std::uint32_t)> data{};
        std::size_t wordsRead                   = 0;
        std::size_t drainedWords                = 0;
        std::uint16_t initialMCardControl       = 0;
        std::uint32_t initialRomControl         = 0;
        bool commandReadbackAvailable           = false;
        bool commandIdentityChanged             = false;
        std::uint32_t expectedCommandHigh       = 0;
        std::uint32_t expectedCommandLow        = 0;
        std::uint32_t observedCommandHigh       = 0;
        std::uint32_t observedCommandLow        = 0;
        std::uint32_t selectTransferRomControl  = 0;
        std::uint32_t selectCompletedRomControl = 0;
        std::uint32_t readTransferRomControl    = 0;
        std::uint32_t readCompletedRomControl   = 0;
        std::uint32_t returnTransferRomControl  = 0;
        std::uint32_t returnCompletedRomControl = 0;
    };

    // A run of consecutive sectors inside one 128-KiB RW window: the same
    // B2/B7/8B shape as the single-sector probe, with `sectorCount` B7 reads
    // between the mode commands. Register captures describe the mode-select,
    // the last B7 attempted, and the return to ROM mode. `observed*` hold the
    // values that tripped a ControllerStateChanged, which is how a lost
    // arbitration is told apart from a cartridge fault.
    struct NandRangeReadResult {
        SectorReadStatus status            = SectorReadStatus::InvalidArgument;
        NandReadStage stage                = NandReadStage::Preflight;
        SectorReadStatus returnToRomStatus = SectorReadStatus::InvalidArgument;
        bool returnToRomAttempted          = false;
        // ROM mode is assured: either 8B completed, or the cartridge was reset.
        bool recoveredToRomMode   = false;
        bool cartridgeWasReset    = false;
        std::uint32_t windowBase  = 0;
        std::uint32_t firstSector = 0;
        std::size_t sectorsRead   = 0;
        std::size_t wordsRead     = 0;
        // How many sectors each B7 asked for, and how many words an abandoned
        // transfer had to give back before the bus was released.
        std::size_t blockSectors = 1;
        std::size_t drainedWords = 0;
        // Process9 parks the controller at its own timing between its own
        // transfers, so the idle configuration found at the start of a burst is
        // reported rather than required.
        bool idleConfigDiffers             = false;
        std::uint16_t initialMCardControl  = 0;
        std::uint32_t initialRomControl    = 0;
        std::uint16_t observedMCardControl = 0;
        std::uint32_t observedRomControl   = 0;
        // Command registers are part of transfer identity when their byte-wide
        // ARM11 readback is available. Process9 can issue a B7 with the same
        // MCNT and ROMCNT flags as this module while asking for a different
        // address; payload sentinels cover hardware that reads the port as zero.
        bool commandReadbackAvailable           = false;
        bool commandIdentityChanged             = false;
        std::uint32_t expectedCommandHigh       = 0;
        std::uint32_t expectedCommandLow        = 0;
        std::uint32_t observedCommandHigh       = 0;
        std::uint32_t observedCommandLow        = 0;
        std::uint32_t selectTransferRomControl  = 0;
        std::uint32_t selectCompletedRomControl = 0;
        std::uint32_t readTransferRomControl    = 0;
        std::uint32_t readCompletedRomControl   = 0;
        std::uint32_t returnTransferRomControl  = 0;
        std::uint32_t returnCompletedRomControl = 0;
    };

    // Stages of the one cartridge-write sequence this module can emit. The
    // boundary that matters is CommitBuffer: everything before it only fills a
    // volatile page buffer on the cartridge, which DiscardBuffer throws away, so
    // a failure up to that point changes nothing in NAND. Once 0x82 is issued
    // the page is being programmed and there is no undo.
    enum class NandWriteStage {
        Preflight,
        SelectWindow,   // 0xB2
        WriteEnable,    // 0x85
        WriteBuffer,    // four 0x81 transfers filling the volatile page buffer
        CommitBuffer,   // 0x82 -- the point of no return
        AwaitReady,     // 0xD6 until bit 5
        DiscardBuffer,  // 0x84
        ReturnToRom,    // 0x8B
        Complete,
    };

    struct NandPageWriteResult {
        SectorReadStatus status            = SectorReadStatus::InvalidArgument;
        NandWriteStage stage               = NandWriteStage::Preflight;
        SectorReadStatus returnToRomStatus = SectorReadStatus::InvalidArgument;
        bool returnToRomAttempted          = false;
        bool recoveredToRomMode            = false;
        bool cartridgeWasReset             = false;
        // True from the moment 0x82 is issued, not from its completion: once the
        // commit is on the wire the cartridge may have begun programming whether
        // or not this module saw the command finish. A failure with this set
        // means the page may be partially programmed and must be read back
        // before it is trusted; a failure without it means NAND is untouched.
        bool committed             = false;
        bool discardAttempted      = false;
        std::uint32_t pageOffset   = 0;
        std::size_t wordsWritten   = 0;
        // Words pushed into an abandoned write transfer purely so BUSY could
        // clear. They land in the page buffer, never in NAND, because the
        // sequence discards the buffer instead of committing it.
        std::size_t flushedWords         = 0;
        std::size_t statusPolls          = 0;
        std::uint32_t statusRegister     = 0;
        bool commandReadbackAvailable    = false;
        bool commandIdentityChanged      = false;
        std::uint32_t expectedCommandHigh = 0;
        std::uint32_t expectedCommandLow  = 0;
        std::uint32_t observedCommandHigh = 0;
        std::uint32_t observedCommandLow  = 0;
        std::uint16_t initialMCardControl = 0;
        std::uint32_t initialRomControl   = 0;
    };

    // What one attempt to unwedge a stalled card controller did. A transfer
    // that was started and then abandoned with words still in the FIFO never
    // clears BUSY, because NTRCARD holds the block until every word is read
    // out. Nothing else on the console reads those words: Process9 drains the
    // FIFO from the card transfer interrupt, so a completion it never sees is
    // a transfer it never finishes. That is the state hardware runs kept
    // reaching -- BUSY and data-ready set, byte-identical across every retry,
    // surviving an FS slot power cycle -- and draining the FIFO is the only
    // thing that can end it without a console power cycle.
    struct StalledControllerRecovery {
        bool attempted                   = false;
        bool cleared                     = false;
        bool restoredTransferIrq         = false;
        std::size_t drainedWords         = 0;
        std::uint16_t mCardControlBefore = 0;
        std::uint16_t mCardControlAfter  = 0;
        std::uint32_t romControlBefore   = 0;
        std::uint32_t romControlAfter    = 0;
    };

    enum class CardPollStatus {
        Observed,
        Timeout,
        ControllerDisabled,
        ControllerReset,
    };

    struct CardPollResult {
        CardPollStatus status            = CardPollStatus::Timeout;
        std::uint32_t observedRomControl = 0;
        std::uint64_t elapsedTicks       = 0;
    };

    RegisterSnapshot capture(const RegisterReader& reader);

    // True only when Process9's normal controller configuration is enabled,
    // interrupt-driven, out of reset, and idle.
    bool process9ControllerReady(const RegisterSnapshot& snapshot);

    // Reads one aligned sector with main-data-mode command B7. The caller must
    // supply header cardControl13 flags and a finite timeout in bus ticks.
    // Controller command, ROMCTRL, MCNT, and IRQ-enable state are restored while
    // ownership remains ours. If the host takes or resets the controller, its
    // newer state is never overwritten with our stale snapshot.
    SectorReadResult readMainModeSector(CardBus& bus, std::uint32_t offset, std::uint32_t normalRomControl, std::uint64_t timeoutTicks);

    // Selects the first 128-KiB NAND RW window with B2, reads its first sector
    // with B7, then returns the cartridge to ROM mode with 8B. This interface
    // cannot address another window and exposes no NAND write command. Each
    // command has the supplied finite timeout; controller state is restored
    // while ownership remains ours and left untouched after host takeover/reset.
    NandSectorReadResult readFirstNandSaveSector(CardBus& bus, std::uint32_t rwStart, std::uint32_t normalRomControl, std::uint64_t timeoutTicks);

    // Reads `sectorCount` sectors starting at sector `firstSector` of window
    // `windowIndex` into `output`, which must hold at least
    // sectorCount * sectorWords words. The run stays inside one window, because
    // that is all B2 selects.
    //
    // The bus is acquired and released per call. Normal completion leaves the
    // cartridge in ROM mode and restores the controller. Host takeover is
    // recovered only when that can be done without overlapping its transfer;
    // host reset is reported without stale-state restoration, and the caller
    // must stop. A caller
    // dumps a whole save as many short bounded runs rather than one long
    // ownership, because
    // Process9 services card IO for the rest of the system and will take the
    // controller back mid-transfer. Short runs bound how much work one such
    // collision costs. Still no NAND write command exists anywhere here.
    //
    // `blockSectors` is how many consecutive sectors each B7 transfers; it must
    // be a power of two no greater than maxNandBlockSectors, and both
    // `firstSector` and `sectorCount` must be multiples of it.
    NandRangeReadResult readNandSaveRange(CardBus& bus, std::uint32_t rwStart, std::uint32_t windowIndex, std::uint32_t firstSector,
        std::size_t sectorCount, std::size_t blockSectors, std::uint32_t normalRomControl, std::uint64_t timeoutTicks, std::uint32_t* output,
        std::size_t outputWords);

    // Drains an abandoned transfer's FIFO so BUSY can clear, and puts the
    // transfer interrupt back if it is still masked. Both repairs address
    // damage this module can itself cause; neither issues a cartridge command,
    // so the cartridge's own mode is untouched. Call it only after the same
    // busy controller state has been observed repeatedly without progress:
    // draining a transfer that is merely in flight would steal its words from
    // their owner.
    StalledControllerRecovery recoverStalledController(CardBus& bus, std::uint64_t timeoutTicks);

    // Program one 0x800-byte page at `pageOffset` (absolute cartridge offset,
    // page-aligned, at or after `rwStart`). The sequence is self-contained: it
    // selects the window with 0xB2, enables writes with 0x85, fills the volatile
    // page buffer with four 0x81 transfers, commits with 0x82, waits for the
    // 0xD6 status bit, discards the buffer with 0x84, and returns the cartridge
    // to ROM mode with 0x8B -- on every path, including every failure path.
    //
    // This does not verify what it wrote. The caller must read the page back and
    // compare before treating a write as done.
    //
    // The caller must also have synchronised to Process9's cartridge poll first.
    // A page write is longer than a page read, and being interrupted between
    // 0x82 and the ready status is the one window in this module where the
    // cartridge can be left in a state no retry can repair.
    NandPageWriteResult writeNandSavePage(CardBus& bus, std::uint32_t rwStart, std::uint32_t pageOffset, std::uint32_t normalRomControl,
        std::uint64_t timeoutTicks, const std::uint32_t* page, std::size_t pageWords);

    // True once the controller has been continuously idle for `quietTicks`.
    // Re-establishing a card session is a sequence of Process9 transfers, not
    // one, so a burst started the moment the last of them finishes will race
    // the next. Waiting for real quiet is what makes resuming safe.
    bool waitForBusQuiet(CardBus& bus, std::uint64_t quietTicks, std::uint64_t timeoutTicks);

    // Waits passively for Process9's four-byte cartridge-ID poll, then for the
    // controller to stay idle for `quietTicks`. Starting a bounded NAND batch
    // immediately after that poll leaves almost a whole poll period in which
    // the cartridge can be returned to ROM mode. Never reads FIFO or writes a
    // controller register.
    CardPollResult waitForCardPoll(CardBus& bus, std::uint64_t quietTicks, std::uint64_t timeoutTicks);

    bool mappedCardAccessAvailable(void);
    std::optional<RegisterSnapshot> captureMapped(void);
    std::optional<SectorReadResult> readMappedMainModeSector(std::uint32_t offset, std::uint32_t normalRomControl);
    std::optional<NandSectorReadResult> readMappedFirstNandSaveSector(std::uint32_t rwStart, std::uint32_t normalRomControl);
    std::optional<NandRangeReadResult> readMappedNandSaveRange(std::uint32_t rwStart, std::uint32_t windowIndex, std::uint32_t firstSector,
        std::size_t sectorCount, std::size_t blockSectors, std::uint32_t normalRomControl, std::uint32_t* output, std::size_t outputWords);
    std::optional<StalledControllerRecovery> recoverMappedStalledController(void);
    std::optional<NandPageWriteResult> writeMappedNandSavePage(
        std::uint32_t rwStart, std::uint32_t pageOffset, std::uint32_t normalRomControl, const std::uint32_t* page, std::size_t pageWords);
    // 20 ms of continuous idle, giving up after a second.
    bool waitForMappedBusQuiet(void);
    std::optional<CardPollResult> waitForMappedCardPoll(void);

    // ROMCTRL as this module will drive it, given the cartridge header's
    // cardControl13. Identity unless a gap override is compiled in.
    std::uint32_t tunedRomControl(std::uint32_t headerRomControl);
}

#endif
