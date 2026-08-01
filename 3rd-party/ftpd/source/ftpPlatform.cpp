// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2024 Michael Theall
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Checkpoint's replacement for ftpd's per-target source/{3ds,switch}/platform.cpp.
// Those files own the whole application (services, graphics, applet hooks, the
// access point menu); Checkpoint already owns all of that, so this file keeps
// only the four things the FTP core asks the platform for: is the network up,
// what address do we bind, a thread and a mutex.
//
// The network probe deliberately uses gethostid() on both consoles rather than
// ac:u / nifm: Checkpoint does not initialize those services, and a non-zero
// host id is exactly the condition under which bind() can succeed.

#include "ftpPlatform.h"

#include "log.h"

#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#ifdef __SWITCH__
#include <mutex>
#include <thread>
#endif

namespace
{
#ifdef __3DS__
/// \brief Thread stack size
/// \note ftpd's own value; the session code has multi-KB stack frames
constexpr auto STACK_SIZE = 0x8000;

// 3DS scheduling is strict-priority (smaller number wins). Checkpoint's main
// thread is normally 0x30 and its general worker pool is 0x2F; upstream ftpd's
// caller+1 policy therefore put the FTP pump at 0x31, below everything else in
// this process. 0x2E lets a ready socket preempt both, while poll()/send()/recv()
// still block and leave the UI the core whenever the pump has no work.
#ifndef FTPD_THREAD_PRIORITY_OFFSET
#define FTPD_THREAD_PRIORITY_OFFSET -2
#endif

#ifndef FTPD_THREAD_CORE
#define FTPD_THREAD_CORE 0
#endif

#ifndef FTPD_SYSCORE_TIME_LIMIT
#define FTPD_SYSCORE_TIME_LIMIT 30
#endif

// Controlled app-core helper modes, active only during RETR/STOR/APPE:
//   0: disabled;
//   1: atomic spin, with no service call (falsified at priority 0x3F);
//   2: repeated read-only stat() through devoptab/FS (hardware-tested);
//   3: bounded read-only directory scan through devoptab/FS (hardware-tested).
//   4: priority-wake interval sweep, with negligible active work (falsified).
//   5: priority-matched high-duty sweep with a fixed 1 ms sleep (measured).
//   6: fixed priority-matched duty with a 1 ms sleep (confirmed candidate).
// The mode-1/priority-0x2F run proved that the real worker's scheduler
// relationship is sufficient, but mode 4 proved wakes alone are not. Mode 5
// measured how much continuously runnable time is needed while still giving
// the 0x30 UI a regular scheduling window. Mode 6 confirms the lowest phase
// that approached the continuous-spin receive latency without transition logs.
#ifndef FTPD_APP_CORE_HELPER_MODE
#define FTPD_APP_CORE_HELPER_MODE 6
#endif

#if FTPD_APP_CORE_HELPER_MODE < 0 || FTPD_APP_CORE_HELPER_MODE > 6
#error "FTPD_APP_CORE_HELPER_MODE must be between 0 and 6"
#endif

#ifndef FTPD_APP_CORE_HELPER_PRIORITY
#define FTPD_APP_CORE_HELPER_PRIORITY 0x2F
#endif

#if FTPD_APP_CORE_HELPER_PRIORITY < 0x18 || FTPD_APP_CORE_HELPER_PRIORITY > 0x3F
#error "FTPD_APP_CORE_HELPER_PRIORITY must be between 0x18 and 0x3F"
#endif

constexpr auto HELPER_STACK_SIZE = 0x8000;
constexpr auto HELPER_PRIORITY   = FTPD_APP_CORE_HELPER_PRIORITY;
constexpr auto HELPER_IDLE_NS    = 100'000'000LL;
constexpr auto HELPER_MAX_ENTRIES = 8U;
constexpr char HELPER_DIRECTORY[] = "sdmc:/3ds/Checkpoint";
#if FTPD_APP_CORE_HELPER_MODE == 4
constexpr std::int64_t HELPER_WAKE_PHASE_NS = 30'000'000'000LL;
constexpr std::int64_t HELPER_WAKE_INTERVALS_NS[] = {
    32'000'000LL, 16'000'000LL, 8'000'000LL, 4'000'000LL, 1'000'000LL};
constexpr auto HELPER_WAKE_PHASES =
    sizeof (HELPER_WAKE_INTERVALS_NS) / sizeof (HELPER_WAKE_INTERVALS_NS[0]);
#endif
#if FTPD_APP_CORE_HELPER_MODE == 5
constexpr std::uint64_t HELPER_DUTY_PHASE_TICKS =
    static_cast<std::uint64_t> (SYSCLOCK_ARM11) * 30ULL;
constexpr std::uint32_t HELPER_DUTY_ITERATIONS[] = {
    262'144U, 393'216U, 524'288U, 786'432U, 1'048'576U};
constexpr auto HELPER_DUTY_PHASES =
    sizeof (HELPER_DUTY_ITERATIONS) / sizeof (HELPER_DUTY_ITERATIONS[0]);
#endif
#if FTPD_APP_CORE_HELPER_MODE == 5 || FTPD_APP_CORE_HELPER_MODE == 6
constexpr std::int64_t HELPER_DUTY_SLEEP_NS = 1'000'000LL;
#endif
#if FTPD_APP_CORE_HELPER_MODE == 6
#ifndef FTPD_APP_CORE_HELPER_ITERATIONS
#define FTPD_APP_CORE_HELPER_ITERATIONS 786432
#endif
#if FTPD_APP_CORE_HELPER_ITERATIONS <= 0
#error "FTPD_APP_CORE_HELPER_ITERATIONS must be positive"
#endif
constexpr std::uint32_t HELPER_FIXED_ITERATIONS = FTPD_APP_CORE_HELPER_ITERATIONS;
#endif

constexpr char const *helperModeName ()
{
#if FTPD_APP_CORE_HELPER_MODE == 1
	return "spin";
#elif FTPD_APP_CORE_HELPER_MODE == 2
	return "fs-stat";
#elif FTPD_APP_CORE_HELPER_MODE == 3
	return "fs-dirscan";
#elif FTPD_APP_CORE_HELPER_MODE == 4
	return "priority-wake";
#elif FTPD_APP_CORE_HELPER_MODE == 5
	return "priority-duty-high";
#elif FTPD_APP_CORE_HELPER_MODE == 6
	return "priority-duty-fixed";
#else
	return "disabled";
#endif
}

#if FTPD_APP_CORE_HELPER_MODE != 0
/// \brief Number of active RETR/STOR/APPE transfers
std::atomic_uint s_activeTransfers = 0;
/// \brief Metadata operations performed during the current transfer window
std::atomic_uint s_helperOps = 0;
/// \brief Failed metadata operations during the current transfer window
std::atomic_uint s_helperFailures = 0;
#if FTPD_APP_CORE_HELPER_MODE == 3
/// \brief Non-dot directory entries inspected during the current transfer window
std::atomic_uint s_helperEntries = 0;
#endif
#endif
#endif
}

bool platform::networkVisible ()
{
	return ::gethostid () != 0;
}

bool platform::networkAddress (SockAddr &addr_)
{
	auto const hostId = ::gethostid ();
	if (hostId == 0)
		return false;

	sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_port        = 0;
	addr.sin_addr.s_addr = hostId;

	addr_ = addr;
	return true;
}

void platform::transferBegin ()
{
#if defined(__3DS__) && FTPD_APP_CORE_HELPER_MODE != 0
	// There is one FTP pump, so the zero-to-one transition is serialized. Reset
	// before publishing the active state, ensuring the helper cannot count an
	// operation and then have it erased at the start of a normal transfer.
	if (s_activeTransfers.load (std::memory_order_relaxed) == 0)
	{
		s_helperOps.store (0, std::memory_order_relaxed);
		s_helperFailures.store (0, std::memory_order_relaxed);
#if FTPD_APP_CORE_HELPER_MODE == 3
		s_helperEntries.store (0, std::memory_order_relaxed);
#endif
	}
	s_activeTransfers.fetch_add (1, std::memory_order_relaxed);
#endif
}

void platform::transferEnd ()
{
#if defined(__3DS__) && FTPD_APP_CORE_HELPER_MODE != 0
	auto count = s_activeTransfers.load (std::memory_order_relaxed);
	while (count != 0)
	{
		if (s_activeTransfers.compare_exchange_weak (
		        count, count - 1, std::memory_order_relaxed))
		{
			if (count == 1)
#if FTPD_APP_CORE_HELPER_MODE == 3
				info ("FTP app-core helper result: mode=%s scans=%lu entries=%lu failures=%lu\n",
				    helperModeName (),
				    static_cast<unsigned long> (s_helperOps.load (std::memory_order_relaxed)),
				    static_cast<unsigned long> (
				        s_helperEntries.load (std::memory_order_relaxed)),
				    static_cast<unsigned long> (
				        s_helperFailures.load (std::memory_order_relaxed)));
#else
				info ("FTP app-core helper result: mode=%s ops=%lu failures=%lu\n",
				    helperModeName (),
				    static_cast<unsigned long> (s_helperOps.load (std::memory_order_relaxed)),
				    static_cast<unsigned long> (
				        s_helperFailures.load (std::memory_order_relaxed)));
#endif
			break;
		}
	}
#endif
}

#ifdef __3DS__
///////////////////////////////////////////////////////////////////////////
platform::steady_clock::time_point platform::steady_clock::now () noexcept
{
	return time_point (duration (svcGetSystemTick ()));
}
#endif

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
#ifdef __3DS__
	~privateData_t ()
	{
		helperQuit = true;
		if (helperThread)
		{
			threadJoin (helperThread, UINT64_MAX);
			threadFree (helperThread);
		}

		if (thread)
			threadFree (thread);

		// The FTP thread has finished before its pimpl is destroyed (FtpServer's
		// destructor joins it). Return the system-core reservation only after the
		// handle is gone, then restore exactly what the application had before.
		if (restoreCpuTimeLimit)
		{
			auto const rc = APT_SetAppCpuTimeLimit (oldCpuTimeLimit);
			if (R_FAILED (rc))
				error ("APT_SetAppCpuTimeLimit(%lu) restore failed: 0x%08lX\n",
				    static_cast<unsigned long> (oldCpuTimeLimit),
				    static_cast<unsigned long> (rc));
		}
	}

	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : func (std::move (func_))
	{
		s32 callerPriority = 0x30;
		svcGetThreadPriority (&callerPriority, CUR_THREAD_HANDLE);
		auto const priority = std::clamp<s32> (
		    callerPriority + FTPD_THREAD_PRIORITY_OFFSET, 0x18, 0x3F);

		int core = 0;
#if FTPD_THREAD_CORE == 1
		// A non-zero APT limit is required before an application may create its
		// single thread on core 1. Only use the core when both calls succeed: if
		// the old value is unknown, it cannot be restored safely at shutdown.
		auto const getLimit = APT_GetAppCpuTimeLimit (&oldCpuTimeLimit);
		auto const setLimit = R_SUCCEEDED (getLimit) ?
		                          APT_SetAppCpuTimeLimit (FTPD_SYSCORE_TIME_LIMIT) :
		                          getLimit;
		if (R_SUCCEEDED (getLimit) && R_SUCCEEDED (setLimit))
		{
			restoreCpuTimeLimit = true;
			core                    = 1;
		}
		else
		{
			error ("FTP core-1 reservation failed (get=0x%08lX set=0x%08lX); using core 0\n",
			    static_cast<unsigned long> (getLimit),
			    static_cast<unsigned long> (setLimit));
		}
#elif FTPD_THREAD_CORE != 0
#error "FTPD_THREAD_CORE must be 0 or 1 on 3DS"
#endif

		thread = threadCreate (&privateData_t::threadFunc, this, STACK_SIZE, priority, core, false);

		// Core 1 is an optimisation, never a reason for the FTP server to vanish.
		// Restore the previous APT limit before retrying on the application core.
		if (!thread && core == 1)
		{
			error ("FTP thread creation on core 1 failed; using core 0\n");
			if (restoreCpuTimeLimit)
			{
				auto const rc = APT_SetAppCpuTimeLimit (oldCpuTimeLimit);
				if (R_SUCCEEDED (rc))
					restoreCpuTimeLimit = false;
				else
					error ("APT_SetAppCpuTimeLimit(%lu) before fallback failed: 0x%08lX\n",
					    static_cast<unsigned long> (oldCpuTimeLimit),
					    static_cast<unsigned long> (rc));
			}
			core   = 0;
			thread = threadCreate (
			    &privateData_t::threadFunc, this, STACK_SIZE, priority, core, false);
		}

		if (thread)
			info ("FTP thread: core=%d priority=0x%02lX caller=0x%02lX syscore=%d%%\n",
			    core,
			    static_cast<unsigned long> (priority),
			    static_cast<unsigned long> (callerPriority),
			    core == 1 ? FTPD_SYSCORE_TIME_LIMIT : 0);
		else
			error ("FTP thread creation failed on core 0\n");

#if FTPD_APP_CORE_HELPER_MODE != 0
		if (thread)
			helperThread = threadCreate (&privateData_t::helperThreadFunc,
			    this,
			    HELPER_STACK_SIZE,
			    HELPER_PRIORITY,
			    0,
			    false);

		if (helperThread)
#if FTPD_APP_CORE_HELPER_MODE == 6
			info ("FTP app-core helper: mode=%s iterations=%lu sleep=1ms "
			      "active-transfer-only core=0 priority=0x%02X\n",
			    helperModeName (),
			    static_cast<unsigned long> (HELPER_FIXED_ITERATIONS),
			    HELPER_PRIORITY);
#else
			info ("FTP app-core helper: mode=%s active-transfer-only core=0 priority=0x%02X\n",
			    helperModeName (), HELPER_PRIORITY);
#endif
		else if (thread)
			error ("FTP app-core helper thread creation failed (mode=%s)\n",
			    helperModeName ());
#endif
	}

	/// \brief Underlying thread entry point
	/// \param arg_ Thread pimpl object
	static void threadFunc (void *const arg_)
	{
		static_cast<privateData_t *> (arg_)->func ();
	}

	/// \brief Run the selected controlled helper during file transfers
	/// \param arg_ Thread pimpl object
#if FTPD_APP_CORE_HELPER_MODE != 0
	static void helperThreadFunc (void *const arg_)
	{
		auto const self = static_cast<privateData_t *> (arg_);
#if FTPD_APP_CORE_HELPER_MODE == 1 || FTPD_APP_CORE_HELPER_MODE == 4 || \
    FTPD_APP_CORE_HELPER_MODE == 5 || FTPD_APP_CORE_HELPER_MODE == 6
		// Record one entry into each normally separated transfer window without
		// adding an atomic operation to mode 1's hot spin itself. Modes 4 through
		// 6 reuse the edge to initialize their controlled run.
		auto activeWindow = false;
#endif
#if FTPD_APP_CORE_HELPER_MODE == 4
		auto wakePhase      = std::size_t {0};
		auto wakesThisPhase = std::uint64_t {0};
#endif
#if FTPD_APP_CORE_HELPER_MODE == 5
		auto dutyPhase      = std::size_t {0};
		auto phaseStartTick = std::uint64_t {0};
		auto phaseBusyTicks = std::uint64_t {0};
		auto phasePulses    = std::uint64_t {0};
		auto const reportDutyPhase = [&] (char const *const state_, std::uint64_t const now_)
		{
			auto const elapsedTicks = now_ - phaseStartTick;
			auto const busyMs = phaseBusyTicks * 1'000ULL / SYSCLOCK_ARM11;
			auto const dutyPermille =
			    elapsedTicks == 0 ? 0 : phaseBusyTicks * 1'000ULL / elapsedTicks;
			info ("FTP app-core helper duty: mode=%s iterations=%lu pulses=%lu "
			      "busy=%lums duty=%lu/1000 phase=%lu/%lu state=%s\n",
			    helperModeName (),
			    static_cast<unsigned long> (HELPER_DUTY_ITERATIONS[dutyPhase]),
			    static_cast<unsigned long> (phasePulses),
			    static_cast<unsigned long> (busyMs),
			    static_cast<unsigned long> (dutyPermille),
			    static_cast<unsigned long> (dutyPhase + 1),
			    static_cast<unsigned long> (HELPER_DUTY_PHASES),
			    state_);
		};
#endif
#if FTPD_APP_CORE_HELPER_MODE == 6
		auto fixedStartTick = std::uint64_t {0};
		auto fixedSpanTicks = std::uint64_t {0};
		auto fixedPulses    = std::uint64_t {0};
		auto const reportFixedPulse = [&] (std::uint64_t const now_)
		{
			auto const elapsedTicks = now_ - fixedStartTick;
			auto const spanMs = fixedSpanTicks * 1'000ULL / SYSCLOCK_ARM11;
			auto const spanPermille =
			    elapsedTicks == 0 ? 0 : fixedSpanTicks * 1'000ULL / elapsedTicks;
			info ("FTP app-core helper pulse: mode=%s iterations=%lu pulses=%lu "
			      "span=%lums span-ratio=%lu/1000 state=complete\n",
			    helperModeName (),
			    static_cast<unsigned long> (HELPER_FIXED_ITERATIONS),
			    static_cast<unsigned long> (fixedPulses),
			    static_cast<unsigned long> (spanMs),
			    static_cast<unsigned long> (spanPermille));
		};
#endif
		while (!self->helperQuit.load (std::memory_order_relaxed))
		{
			if (s_activeTransfers.load (std::memory_order_relaxed) == 0)
			{
#if FTPD_APP_CORE_HELPER_MODE == 5
				if (activeWindow)
					reportDutyPhase ("partial", svcGetSystemTick ());
#endif
#if FTPD_APP_CORE_HELPER_MODE == 6
				if (activeWindow)
					reportFixedPulse (svcGetSystemTick ());
#endif
#if FTPD_APP_CORE_HELPER_MODE == 1 || FTPD_APP_CORE_HELPER_MODE == 4 || \
    FTPD_APP_CORE_HELPER_MODE == 5 || FTPD_APP_CORE_HELPER_MODE == 6
				activeWindow = false;
#endif
				svcSleepThread (HELPER_IDLE_NS);
			}
			else
			{
#if FTPD_APP_CORE_HELPER_MODE == 1
				if (!activeWindow)
				{
					s_helperOps.fetch_add (1, std::memory_order_relaxed);
					activeWindow = true;
				}
				// Stay runnable without touching FS, SOCU, APT, or another service.
				std::atomic_signal_fence (std::memory_order_seq_cst);
#elif FTPD_APP_CORE_HELPER_MODE == 2
				// Checkpoint creates this directory before starting FTP. stat() is
				// harmless and read-only, but traverses the same newlib/devoptab FS
				// boundary used by BackupSizeCache's per-entry metadata queries.
				struct stat metadata;
				auto const rc = ::stat (HELPER_DIRECTORY, &metadata);
				s_helperOps.fetch_add (1, std::memory_order_relaxed);
				if (rc != 0)
					s_helperFailures.fetch_add (1, std::memory_order_relaxed);
#elif FTPD_APP_CORE_HELPER_MODE == 3
				// Open and close a real directory session, inspect a bounded number
				// of entries, and perform the same per-entry stat used by the backup-
				// size walker. The root is always created before FTP starts, and the
				// cap prevents the diagnostic from recursively scanning user data.
				auto failures = 0U;
				auto entries   = 0U;
				auto const dir = ::opendir (HELPER_DIRECTORY);
				if (!dir)
					++failures;
				else
				{
					while (entries < HELPER_MAX_ENTRIES)
					{
						errno           = 0;
						auto const entry = ::readdir (dir);
						if (!entry)
						{
							if (errno != 0)
								++failures;
							break;
						}

						if (std::strcmp (entry->d_name, ".") == 0 ||
						    std::strcmp (entry->d_name, "..") == 0)
							continue;

						char path[512];
						auto const length = std::snprintf (
						    path, sizeof (path), "%s/%s", HELPER_DIRECTORY, entry->d_name);
						if (length < 0 || static_cast<std::size_t> (length) >= sizeof (path))
							++failures;
						else
						{
							struct stat metadata;
							if (::stat (path, &metadata) != 0)
								++failures;
						}
						++entries;
					}

					if (::closedir (dir) != 0)
						++failures;
				}

				s_helperOps.fetch_add (1, std::memory_order_relaxed);
				s_helperEntries.fetch_add (entries, std::memory_order_relaxed);
				if (failures != 0)
					s_helperFailures.fetch_add (failures, std::memory_order_relaxed);
#elif FTPD_APP_CORE_HELPER_MODE == 4
				if (!activeWindow)
				{
					activeWindow   = true;
					wakePhase      = 0;
					wakesThisPhase = 0;
					info ("FTP app-core helper phase: mode=%s interval=%lums phase=1/%lu\n",
					    helperModeName (),
					    static_cast<unsigned long> (HELPER_WAKE_INTERVALS_NS[0] / 1'000'000LL),
					    static_cast<unsigned long> (HELPER_WAKE_PHASES));
				}

				auto const interval = HELPER_WAKE_INTERVALS_NS[wakePhase];
				s_helperOps.fetch_add (1, std::memory_order_relaxed);
				++wakesThisPhase;
				svcSleepThread (interval);

				auto const phaseWakes = static_cast<std::uint64_t> (
				    (HELPER_WAKE_PHASE_NS + interval - 1) / interval);
				if (s_activeTransfers.load (std::memory_order_relaxed) != 0 &&
				    wakesThisPhase >= phaseWakes && wakePhase + 1 < HELPER_WAKE_PHASES)
				{
					++wakePhase;
					wakesThisPhase = 0;
					info ("FTP app-core helper phase: mode=%s interval=%lums phase=%lu/%lu\n",
					    helperModeName (),
					    static_cast<unsigned long> (
					        HELPER_WAKE_INTERVALS_NS[wakePhase] / 1'000'000LL),
					    static_cast<unsigned long> (wakePhase + 1),
					    static_cast<unsigned long> (HELPER_WAKE_PHASES));
				}
#elif FTPD_APP_CORE_HELPER_MODE == 5
				if (!activeWindow)
				{
					activeWindow   = true;
					dutyPhase      = 0;
					phaseStartTick = svcGetSystemTick ();
					phaseBusyTicks = 0;
					phasePulses    = 0;
					info ("FTP app-core helper phase: mode=%s iterations=%lu sleep=1ms "
					      "phase=1/%lu\n",
					    helperModeName (),
					    static_cast<unsigned long> (HELPER_DUTY_ITERATIONS[0]),
					    static_cast<unsigned long> (HELPER_DUTY_PHASES));
				}

				auto const pulseStart = svcGetSystemTick ();
				for (auto iteration = 0U; iteration < HELPER_DUTY_ITERATIONS[dutyPhase];
				     ++iteration)
					std::atomic_signal_fence (std::memory_order_seq_cst);
				auto const pulseEnd = svcGetSystemTick ();
				phaseBusyTicks += pulseEnd - pulseStart;
				++phasePulses;
				s_helperOps.fetch_add (1, std::memory_order_relaxed);
				svcSleepThread (HELPER_DUTY_SLEEP_NS);

				auto const now = svcGetSystemTick ();
				if (s_activeTransfers.load (std::memory_order_relaxed) != 0 &&
				    now - phaseStartTick >= HELPER_DUTY_PHASE_TICKS &&
				    dutyPhase + 1 < HELPER_DUTY_PHASES)
				{
					reportDutyPhase ("complete", now);
					++dutyPhase;
					phaseStartTick = now;
					phaseBusyTicks = 0;
					phasePulses    = 0;
					info ("FTP app-core helper phase: mode=%s iterations=%lu sleep=1ms "
					      "phase=%lu/%lu\n",
					    helperModeName (),
					    static_cast<unsigned long> (HELPER_DUTY_ITERATIONS[dutyPhase]),
					    static_cast<unsigned long> (dutyPhase + 1),
					    static_cast<unsigned long> (HELPER_DUTY_PHASES));
				}
#elif FTPD_APP_CORE_HELPER_MODE == 6
				if (!activeWindow)
				{
					activeWindow   = true;
					fixedStartTick = svcGetSystemTick ();
					fixedSpanTicks = 0;
					fixedPulses    = 0;
				}

				auto const pulseStart = svcGetSystemTick ();
				for (auto iteration = 0U; iteration < HELPER_FIXED_ITERATIONS; ++iteration)
					std::atomic_signal_fence (std::memory_order_seq_cst);
				auto const pulseEnd = svcGetSystemTick ();
				// This wall-clock span includes intervals preempted by the stronger
				// FTP thread. It is useful for reproducibility, but is not thread CPU time.
				fixedSpanTicks += pulseEnd - pulseStart;
				++fixedPulses;
				s_helperOps.fetch_add (1, std::memory_order_relaxed);
				svcSleepThread (HELPER_DUTY_SLEEP_NS);
#endif
			}
		}
	}
#endif

	/// \brief Underlying thread
	::Thread thread = nullptr;
	/// \brief Thread entry point
	std::function<void ()> func;
	/// \brief Application-core experimental helper
	::Thread helperThread = nullptr;
	/// \brief Stop flag for helperThread
	std::atomic_bool helperQuit = false;
	/// \brief CPU-time limit to restore after releasing core 1
	u32 oldCpuTimeLimit = 0;
	/// \brief Whether this thread changed the application's CPU-time limit
	bool restoreCpuTimeLimit = false;
#else
	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : thread (std::move (func_))
	{
	}

	/// \brief Underlying thread
	std::thread thread;
#endif
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> &&func_)
    : m_d (new privateData_t (std::move (func_)))
{
}

platform::Thread::Thread (Thread &&that_) : m_d (new privateData_t ())
{
	std::swap (m_d, that_.m_d);
}

platform::Thread &platform::Thread::operator= (Thread &&that_)
{
	std::swap (m_d, that_.m_d);
	return *this;
}

void platform::Thread::join ()
{
#ifdef __3DS__
	if (!m_d->thread)
		return;

	threadJoin (m_d->thread, UINT64_MAX);
#else
	if (!m_d->thread.joinable ())
		return;

	m_d->thread.join ();
#endif
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
#ifdef __3DS__
	svcSleepThread (std::chrono::nanoseconds (timeout_).count ());
#else
	std::this_thread::sleep_for (timeout_);
#endif
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
#ifdef __3DS__
	/// \brief Underlying mutex
	LightLock mutex;
#else
	/// \brief Underlying mutex
	std::mutex mutex;
#endif
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex () = default;

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
#ifdef __3DS__
	LightLock_Init (&m_d->mutex);
#endif
}

void platform::Mutex::lock ()
{
#ifdef __3DS__
	LightLock_Lock (&m_d->mutex);
#else
	m_d->mutex.lock ();
#endif
}

void platform::Mutex::unlock ()
{
#ifdef __3DS__
	LightLock_Unlock (&m_d->mutex);
#else
	m_d->mutex.unlock ();
#endif
}
