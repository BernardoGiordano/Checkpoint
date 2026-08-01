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

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
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

// A ready FTP socket preempts the UI and worker pool; blocking socket calls
// return the application core whenever there is no transfer work.
constexpr auto THREAD_PRIORITY_OFFSET = -2;

constexpr auto HELPER_STACK_SIZE                = 0x8000;
constexpr auto HELPER_PRIORITY                  = 0x2F;
constexpr std::int64_t HELPER_IDLE_NS           = 100'000'000LL;
constexpr std::int64_t HELPER_SLEEP_NS          = 1'000'000LL;
constexpr std::uint32_t HELPER_PULSE_ITERATIONS = 786'432U;

/// \brief Number of active RETR/STOR/APPE transfers
std::atomic_uint s_activeTransfers = 0;
/// \brief Helper pulses performed during the current transfer window
std::atomic_uint s_helperOps = 0;
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
#ifdef __3DS__
	// There is one FTP pump, so the zero-to-one transition is serialized. Reset
	// before publishing the active state, ensuring the helper cannot count an
	// operation and then have it erased at the start of a normal transfer.
	if (s_activeTransfers.load (std::memory_order_relaxed) == 0)
		s_helperOps.store (0, std::memory_order_relaxed);
	s_activeTransfers.fetch_add (1, std::memory_order_relaxed);
#endif
}

void platform::transferEnd ()
{
#ifdef __3DS__
	auto count = s_activeTransfers.load (std::memory_order_relaxed);
	while (count != 0)
	{
		if (s_activeTransfers.compare_exchange_weak (
		        count, count - 1, std::memory_order_relaxed))
		{
			if (count == 1)
				info ("FTP app-core helper result: pulses=%lu\n",
				    static_cast<unsigned long> (
				        s_helperOps.load (std::memory_order_relaxed)));
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
	}

	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : func (std::move (func_))
	{
		s32 callerPriority = 0x30;
		svcGetThreadPriority (&callerPriority, CUR_THREAD_HANDLE);
		auto const priority = std::clamp<s32> (
		    callerPriority + THREAD_PRIORITY_OFFSET, 0x18, 0x3F);

		thread = threadCreate (&privateData_t::threadFunc, this, STACK_SIZE, priority, 0, false);

		if (thread)
			info ("FTP thread: core=0 priority=0x%02lX caller=0x%02lX\n",
			    static_cast<unsigned long> (priority),
			    static_cast<unsigned long> (callerPriority));
		else
			error ("FTP thread creation failed on core 0\n");

		if (thread)
			helperThread = threadCreate (&privateData_t::helperThreadFunc,
			    this,
			    HELPER_STACK_SIZE,
			    HELPER_PRIORITY,
			    0,
			    false);

		if (helperThread)
			info ("FTP app-core helper: iterations=%lu sleep=1ms "
			      "active-transfer-only core=0 priority=0x%02X\n",
			    static_cast<unsigned long> (HELPER_PULSE_ITERATIONS),
			    HELPER_PRIORITY);
		else if (thread)
			error ("FTP app-core helper thread creation failed\n");
	}

	/// \brief Underlying thread entry point
	/// \param arg_ Thread pimpl object
	static void threadFunc (void *const arg_)
	{
		static_cast<privateData_t *> (arg_)->func ();
	}

	/// \brief Boost FTP transfers with fixed application-core work pulses
	/// \param arg_ Thread pimpl object
	static void helperThreadFunc (void *const arg_)
	{
		auto const self = static_cast<privateData_t *> (arg_);
		auto activeWindow = false;
		auto startTick     = std::uint64_t {0};
		auto spanTicks     = std::uint64_t {0};
		auto pulses        = std::uint64_t {0};
		auto const report = [&] (std::uint64_t const now_)
		{
			auto const elapsedTicks = now_ - startTick;
			auto const spanMs = spanTicks * 1'000ULL / SYSCLOCK_ARM11;
			auto const spanPermille =
			    elapsedTicks == 0 ? 0 : spanTicks * 1'000ULL / elapsedTicks;
			info ("FTP app-core helper: iterations=%lu pulses=%lu "
			      "span=%lums span-ratio=%lu/1000 state=complete\n",
			    static_cast<unsigned long> (HELPER_PULSE_ITERATIONS),
			    static_cast<unsigned long> (pulses),
			    static_cast<unsigned long> (spanMs),
			    static_cast<unsigned long> (spanPermille));
		};

		while (!self->helperQuit.load (std::memory_order_relaxed))
		{
			if (s_activeTransfers.load (std::memory_order_relaxed) == 0)
			{
				if (activeWindow)
					report (svcGetSystemTick ());
				activeWindow = false;
				svcSleepThread (HELPER_IDLE_NS);
				continue;
			}

			if (!activeWindow)
			{
				activeWindow = true;
				startTick     = svcGetSystemTick ();
				spanTicks     = 0;
				pulses        = 0;
			}

			auto const pulseStart = svcGetSystemTick ();
			for (auto iteration = 0U; iteration < HELPER_PULSE_ITERATIONS; ++iteration)
				std::atomic_signal_fence (std::memory_order_seq_cst);
			auto const pulseEnd = svcGetSystemTick ();
			// The span includes time preempted by the higher-priority FTP thread.
			spanTicks += pulseEnd - pulseStart;
			++pulses;
			s_helperOps.fetch_add (1, std::memory_order_relaxed);
			svcSleepThread (HELPER_SLEEP_NS);
		}
	}

	/// \brief Underlying thread
	::Thread thread = nullptr;
	/// \brief Thread entry point
	std::function<void ()> func;
	/// \brief Application-core transfer helper
	::Thread helperThread = nullptr;
	/// \brief Stop flag for helperThread
	std::atomic_bool helperQuit = false;
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
