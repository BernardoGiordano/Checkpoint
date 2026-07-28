// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
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

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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

std::string const &platform::hostname ()
{
	static std::string const hostname = "checkpoint";
	return hostname;
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
		if (thread)
			threadFree (thread);
	}

	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : func (std::move (func_))
	{
		// use next-lower priority than the caller
		s32 priority = 0x30;
		svcGetThreadPriority (&priority, CUR_THREAD_HANDLE);
		priority = std::clamp<s32> (priority, 0x18, 0x3F - 1) + 1;

		// use appcore
		thread = threadCreate (&privateData_t::threadFunc, this, STACK_SIZE, priority, 0, false);
	}

	/// \brief Underlying thread entry point
	/// \param arg_ Thread pimpl object
	static void threadFunc (void *const arg_)
	{
		static_cast<privateData_t *> (arg_)->func ();
	}

	/// \brief Underlying thread
	::Thread thread = nullptr;
	/// \brief Thread entry point
	std::function<void ()> func;
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
