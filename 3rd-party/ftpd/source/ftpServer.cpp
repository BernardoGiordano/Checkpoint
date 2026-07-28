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
// Adapted for Checkpoint (see ftpServer.h).

#include "ftpServer.h"

#include "fs.h"
#include "ftpConfig.h"
#include "ftpSession.h"
#include "log.h"
#include "ftpPlatform.h"
#include "sockAddr.h"
#include "socket.h"

#ifdef __3DS__
#include <3ds.h>
#endif

#include <sys/statvfs.h>
using statvfs_t = struct statvfs;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
using namespace std::chrono_literals;

#define LOCKED(x)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		auto const lock = std::scoped_lock (m_lock);                                               \
		x;                                                                                         \
	} while (0)

namespace
{
/// \brief Application start time
auto const s_startTime = std::time (nullptr);

#ifdef __3DS__
/// \brief Timezone offset in seconds (only used on 3DS)
int s_tzOffset = 0;
#endif

/// \brief Mutex for s_freeSpace
platform::Mutex s_lock;

/// \brief Free space string
std::string s_freeSpace;
}

///////////////////////////////////////////////////////////////////////////
FtpServer::~FtpServer ()
{
	m_quit = true;

	m_thread.join ();
}

FtpServer::FtpServer (UniqueFtpConfig config_, std::function<bool ()> enabled_)
    : m_config (std::move (config_)), m_enabled (std::move (enabled_))
{
#ifdef __3DS__
	s64 tzOffsetMinutes;
	if (R_SUCCEEDED (svcGetSystemInfo (&tzOffsetMinutes, 0x10000, 0x103)))
		s_tzOffset = tzOffsetMinutes * 60;
#endif

	m_thread = platform::Thread (std::bind (&FtpServer::threadFunc, this));
}

UniqueFtpServer FtpServer::create (std::uint16_t const port_, std::function<bool ()> enabled_)
{
	updateFreeSpace ();

	auto config = FtpConfig::create ();
	if (!config->setPort (port_))
		error ("Invalid listen port %u\n", port_);

	return UniqueFtpServer (new FtpServer (std::move (config), std::move (enabled_)));
}

std::string FtpServer::address ()
{
	auto const lock = std::scoped_lock (m_lock);
	if (!m_socket)
		return {};

	return m_name;
}

std::string FtpServer::getFreeSpace ()
{
	auto const lock = std::scoped_lock (s_lock);
	return s_freeSpace;
}

void FtpServer::updateFreeSpace ()
{
	statvfs_t st = {};
#if defined(__3DS__) || defined(__SWITCH__)
	if (::statvfs ("sdmc:/", &st) != 0)
#else
	if (::statvfs ("/", &st) != 0)
#endif
		return;

	auto freeSpace = fs::printSize (static_cast<std::uint64_t> (st.f_bsize) * st.f_bfree);

	auto const lock = std::scoped_lock (s_lock);
	if (freeSpace != s_freeSpace)
		s_freeSpace = std::move (freeSpace);
}

std::time_t FtpServer::startTime ()
{
	return s_startTime;
}

#ifdef __3DS__
int FtpServer::tzOffset ()
{
	return s_tzOffset;
}
#endif

void FtpServer::handleNetworkFound ()
{
	SockAddr addr;
	if (!platform::networkAddress (addr))
		return;

	std::uint16_t port;

	{
		auto const lock = m_config->lockGuard ();
		port            = m_config->port ();
	}

	addr.setPort (port);

	auto socket = Socket::create (Socket::eStream);
	if (!socket)
		return;

	if (port != 0 && !socket->setReuseAddress (true))
		return;

	if (!socket->bind (addr))
		return;

	if (!socket->listen (10))
		return;

	auto const &sockName = socket->sockName ();
	auto const name      = sockName.name ();

	auto listenName = std::string ();
	listenName.resize (std::strlen (name) + 1 + 5);
	listenName.resize (std::sprintf (listenName.data (), "%s:%u", name, sockName.port ()));

	info ("Started server at %s\n", listenName.c_str ());

	{
		auto const lock = std::scoped_lock (m_lock);
		m_socket        = std::move (socket);
		m_name          = std::move (listenName);
	}
}

void FtpServer::handleNetworkLost ()
{
	{
		// destroy sessions
		std::vector<UniqueFtpSession> sessions;
		LOCKED (sessions = std::move (m_sessions));
	}

	{
		UniqueSocket sock;

		// destroy listen socket
		LOCKED (sock = std::move (m_socket));
	}

	info ("Stopped server at %s\n", m_name.c_str ());
}

void FtpServer::nap (std::chrono::milliseconds const timeout_)
{
	// Sleep in slices so teardown never waits out a whole idle backoff: the
	// destructor raises m_quit and then joins this thread.
	constexpr auto SLICE = 100ms;

	for (auto remaining = timeout_; remaining > 0ms && !m_quit; remaining -= SLICE)
		platform::Thread::sleep (std::min (remaining, SLICE));
}

void FtpServer::loop ()
{
	// Checkpoint's FTP toggle. When it goes off, drop everything we hold: the
	// listen socket, the sessions, and (on 3DS) the slice of the soc:u buffer
	// pool they were sitting on.
	if (m_enabled && !m_enabled ())
	{
		if (m_socket)
			handleNetworkLost ();

		nap (100ms);
		return;
	}

	if (!m_socket)
	{
		if (!platform::networkVisible ())
		{
			nap (100ms);
			return;
		}

		handleNetworkFound ();

		// no listen socket after a network-found pass: back off instead of
		// retrying the bind as fast as the CPU allows
		if (!m_socket)
		{
			nap (1000ms);
			return;
		}
	}

	// poll listen socket
	if (m_socket)
	{
		Socket::PollInfo info{*m_socket, POLLIN, 0};
		auto const rc = Socket::poll (&info, 1, 0ms);
		if (rc < 0)
		{
			handleNetworkLost ();
			return;
		}

		if (rc > 0 && (info.revents & POLLIN))
		{
			auto socket = m_socket->accept ();
			if (socket)
			{
				auto session = FtpSession::create (*m_config, std::move (socket));
				LOCKED (m_sessions.emplace_back (std::move (session)));
			}
			else
			{
				handleNetworkLost ();
				return;
			}
		}
	}

	{
		std::vector<UniqueFtpSession> deadSessions;
		{
			// remove dead sessions
			auto const lock = std::scoped_lock (m_lock);

			auto it = std::begin (m_sessions);
			while (it != std::end (m_sessions))
			{
				auto &session = *it;
				if (session->dead ())
				{
					deadSessions.emplace_back (std::move (session));
					it = m_sessions.erase (it);
				}
				else
					++it;
			}
		}
	}

	// poll sessions
	if (!m_sessions.empty ())
	{
		if (!FtpSession::poll (m_sessions))
			handleNetworkLost ();
	}
	// avoid busy polling in background thread
	else
		platform::Thread::sleep (16ms);
}

void FtpServer::threadFunc ()
{
	while (!m_quit)
		loop ();
}
