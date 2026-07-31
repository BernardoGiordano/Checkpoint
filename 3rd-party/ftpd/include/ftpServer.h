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
// Adapted for Checkpoint: no ImGui/console rendering, no settings menu, no log
// upload (cURL), no mDNS. What is left is the headless server — a background
// thread that binds the listen socket when the network comes up, accepts
// sessions and pumps them.
//
// Two Checkpoint-specific additions:
//   * an `enabled` predicate the loop consults every pass, so Checkpoint's FTP
//     toggle can take the server down (sockets closed, sessions dropped)
//     without destroying the object;
//   * address(), so the settings screen can show where to connect.

#pragma once

#include "ftpConfig.h"
#include "ftpSession.h"
#include "ftpPlatform.h"
#include "socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class FtpServer;
using UniqueFtpServer = std::unique_ptr<FtpServer>;

/// \brief FTP server
class FtpServer
{
public:
	~FtpServer ();

	/// \brief Create server and start its thread
	/// \param port_ Listen port
	/// \param enabled_ Predicate polled by the loop; when it returns false the
	///        server closes its sockets and idles. Empty means always enabled.
	static UniqueFtpServer create (std::uint16_t port_, std::function<bool ()> enabled_ = {});

	/// \brief Listening address as "<ip>:<port>", or empty when not listening
	std::string address ();

	/// \brief Server start time
	static std::time_t startTime ();

#ifdef __3DS__
	/// \brief Get timezone offset in seconds (only used on 3DS)
	static int tzOffset ();
#endif

private:
	/// \brief Paramterized constructor
	/// \param config_ FTP settings
	/// \param enabled_ Enable predicate
	FtpServer (FtpConfig config_, std::function<bool ()> enabled_);

	/// \brief Handle when network is found
	void handleNetworkFound ();

	/// \brief Handle when network is lost
	void handleNetworkLost ();

	/// \brief Sleep, waking early if the server is quitting
	/// \param timeout_ Time to sleep
	void nap (std::chrono::milliseconds timeout_);

	/// \brief Server loop
	void loop ();

	/// \brief Thread entry point
	void threadFunc ();

	/// \brief Thread
	platform::Thread m_thread;

	/// \brief Mutex guarding m_socket and m_name
	platform::Mutex m_lock;

	/// \brief Settings
	/// \note Immutable and owned here; sessions hold a const reference to it
	FtpConfig const m_config;

	/// \brief Whether the server should be serving
	std::function<bool ()> m_enabled;

	/// \brief Listen socket
	UniqueSocket m_socket;

	/// \brief Listen address, as "<ip>:<port>"
	std::string m_name;

	/// \brief Sessions
	std::vector<UniqueFtpSession> m_sessions;

	/// \brief Whether thread should quit
	std::atomic_bool m_quit = false;
};
