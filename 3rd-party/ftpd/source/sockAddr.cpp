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

#include "sockAddr.h"

#include <arpa/inet.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#ifdef __3DS__
static_assert (sizeof (sockaddr_storage) == 0x1c);
#endif

///////////////////////////////////////////////////////////////////////////
SockAddr::~SockAddr () = default;

SockAddr::SockAddr () = default;

SockAddr::SockAddr (in_addr_t const addr_, std::uint16_t const port_)
    : SockAddr (in_addr{.s_addr = addr_}, port_)
{
}

SockAddr::SockAddr (in_addr const &addr_, std::uint16_t const port_)
{
	std::memset (&m_addr, 0, sizeof (m_addr));
	m_addr.ss_family = AF_INET;
	setAddr (addr_);
	setPort (port_);
}

SockAddr::SockAddr (SockAddr const &that_) = default;

SockAddr::SockAddr (SockAddr &&that_) = default;

SockAddr &SockAddr::operator= (SockAddr const &that_) = default;

SockAddr &SockAddr::operator= (SockAddr &&that_) = default;

SockAddr::SockAddr (sockaddr_in const &addr_)
{
	assert (addr_.sin_family == AF_INET);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
}

SockAddr::operator sockaddr_in const & () const
{
	assert (m_addr.ss_family == AF_INET);
	return reinterpret_cast<sockaddr_in const &> (m_addr);
}

SockAddr::operator sockaddr * ()
{
	return reinterpret_cast<sockaddr *> (&m_addr);
}

SockAddr::operator sockaddr const * () const
{
	return reinterpret_cast<sockaddr const *> (&m_addr);
}

void SockAddr::setAddr (in_addr_t const addr_)
{
	setAddr (in_addr{.s_addr = addr_});
}

void SockAddr::setAddr (in_addr const &addr_)
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	std::memcpy (&reinterpret_cast<sockaddr_in &> (m_addr).sin_addr, &addr_, sizeof (addr_));
}

std::uint16_t SockAddr::port () const
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	return ntohs (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_port);
}

void SockAddr::setPort (std::uint16_t const port_)
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	reinterpret_cast<sockaddr_in *> (&m_addr)->sin_port = htons (port_);
}

socklen_t SockAddr::size () const
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	return sizeof (sockaddr_in);
}

char const *SockAddr::name (char *buffer_, std::size_t size_) const
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	return inet_ntop (
	    AF_INET, &reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr, buffer_, size_);
}

char const *SockAddr::name () const
{
	thread_local static char buffer[INET_ADDRSTRLEN];

	return name (buffer, sizeof (buffer));
}
