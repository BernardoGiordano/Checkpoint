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
// Adapted for Checkpoint: the platform layer no longer owns the application
// (no init/loop/render/exit, no console, no access point). Checkpoint brings up
// soc:u / bsd and the UI itself, so all that is left here is what the FTP core
// needs from the system: the network address, a thread and a mutex.

#pragma once

#include "sockAddr.h"

#if defined(__3DS__)
#include <3ds.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace platform
{
/// \brief Whether network is visible
bool networkVisible ();

/// \brief Get network address
/// \param[out] addr_ Network address
bool networkAddress (SockAddr &addr_);

/// \brief Get hostname
std::string const &hostname ();

#ifdef __3DS__
/// \brief Steady clock
struct steady_clock
{
	/// \brief Type representing number of ticks
	using rep = std::uint64_t;

	/// \brief Type representing ratio of clock period in seconds
	using period = std::ratio<1, SYSCLOCK_ARM11>;

	/// \brief Duration type
	using duration = std::chrono::duration<rep, period>;

	/// \brief Timestamp type
	using time_point = std::chrono::time_point<steady_clock>;

	/// \brief Whether clock is steady
	constexpr static bool is_steady = true;

	/// \brief Current timestamp
	static time_point now () noexcept;
};
#else
/// \brief Steady clock
using steady_clock = std::chrono::steady_clock;
#endif

/// \brief Platform thread
class Thread
{
public:
	~Thread ();
	Thread ();

	/// \brief Parameterized constructor
	/// \param func_ Thread entrypoint
	Thread (std::function<void ()> &&func_);

	Thread (Thread const &that_) = delete;

	/// \brief Move constructor
	/// \param that_ Object to move from
	Thread (Thread &&that_);

	Thread &operator= (Thread const &that_) = delete;

	/// \brief Move assignment
	/// \param that_ Object to move from
	Thread &operator= (Thread &&that_);

	/// \brief Join thread
	void join ();

	/// \brief Suspend current thread
	/// \param timeout_ Minimum time to sleep
	static void sleep (std::chrono::milliseconds timeout_);

private:
	class privateData_t;

	/// \brief pimpl
	std::unique_ptr<privateData_t> m_d;
};

/// \brief Platform mutex
class Mutex
{
public:
	~Mutex ();
	Mutex ();

	/// \brief Lock mutex
	void lock ();

	/// \brief Unlock mutex
	void unlock ();

private:
	class privateData_t;

	/// \brief pimpl
	std::unique_ptr<privateData_t> m_d;
};
}
