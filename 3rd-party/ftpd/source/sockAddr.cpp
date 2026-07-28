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

#include "sockAddr.h"

#include <arpa/inet.h>

#include <cassert>
#include <cstdlib>
#include <cstring>

#ifdef __3DS__
static_assert (sizeof (sockaddr_storage) == 0x1c);
#endif

namespace
{
in_addr inaddr_any = {.s_addr = htonl (INADDR_ANY)};

std::strong_ordering
    strongMemCompare (void const *const a_, void const *const b_, std::size_t const size_)
{
	auto const cmp = std::memcmp (a_, b_, size_);
	if (cmp < 0)
		return std::strong_ordering::less;
	if (cmp > 0)
		return std::strong_ordering::greater;
	return std::strong_ordering::equal;
}
}

///////////////////////////////////////////////////////////////////////////
SockAddr const SockAddr::AnyIPv4{inaddr_any};

#ifndef NO_IPV6
SockAddr const SockAddr::AnyIPv6{in6addr_any};
#endif

SockAddr::~SockAddr () = default;

SockAddr::SockAddr () = default;

SockAddr::SockAddr (Domain const domain_)
{
	switch (domain_)
	{
	case Domain::IPv4:
		*this = AnyIPv4;
		break;

#ifndef NO_IPV6
	case Domain::IPv6:
		*this = AnyIPv6;
		break;
#endif

	default:
		std::abort ();
	}
}

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

#ifndef NO_IPV6
SockAddr::SockAddr (in6_addr const &addr_, std::uint16_t const port_)
{
	std::memset (&m_addr, 0, sizeof (m_addr));
	m_addr.ss_family = AF_INET6;
	setAddr (addr_);
	setPort (port_);
}
#endif

SockAddr::SockAddr (SockAddr const &that_) = default;

SockAddr::SockAddr (SockAddr &&that_) = default;

SockAddr &SockAddr::operator= (SockAddr const &that_) = default;

SockAddr &SockAddr::operator= (SockAddr &&that_) = default;

SockAddr::SockAddr (sockaddr_in const &addr_)
{
	assert (addr_.sin_family == AF_INET);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
}

#ifndef NO_IPV6
SockAddr::SockAddr (sockaddr_in6 const &addr_)
{
	assert (addr_.sin6_family == AF_INET6);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
}
#endif

SockAddr::SockAddr (sockaddr_storage const &addr_)
{
	switch (addr_.ss_family)
	{
	case AF_INET:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
		break;

#ifndef NO_IPV6
	case AF_INET6:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
		break;
#endif

	default:
		std::abort ();
	}
}

SockAddr::operator sockaddr_in const & () const
{
	assert (m_addr.ss_family == AF_INET);
	return reinterpret_cast<sockaddr_in const &> (m_addr);
}

#ifndef NO_IPV6
SockAddr::operator sockaddr_in6 const & () const
{
	assert (m_addr.ss_family == AF_INET6);
	return reinterpret_cast<sockaddr_in6 const &> (m_addr);
}
#endif

SockAddr::operator sockaddr_storage const & () const
{
	return m_addr;
}

SockAddr::operator sockaddr * ()
{
	return reinterpret_cast<sockaddr *> (&m_addr);
}

SockAddr::operator sockaddr const * () const
{
	return reinterpret_cast<sockaddr const *> (&m_addr);
}

bool SockAddr::operator== (SockAddr const &that_) const
{
	if (m_addr.ss_family != that_.m_addr.ss_family)
		return false;

	switch (m_addr.ss_family)
	{
	case AF_INET:
		if (port () != that_.port ())
			return false;

		// ignore sin_zero
		return static_cast<sockaddr_in const &> (*this).sin_addr.s_addr ==
		       static_cast<sockaddr_in const &> (that_).sin_addr.s_addr;

#ifndef NO_IPV6
	case AF_INET6:
		return std::memcmp (&m_addr, &that_.m_addr, sizeof (sockaddr_in6)) == 0;
#endif

	default:
		std::abort ();
	}
}

std::strong_ordering SockAddr::operator<=> (SockAddr const &that_) const
{
	if (m_addr.ss_family != that_.m_addr.ss_family)
		return m_addr.ss_family <=> that_.m_addr.ss_family;

	switch (m_addr.ss_family)
	{
	case AF_INET:
	{
		auto const cmp =
		    strongMemCompare (&static_cast<sockaddr_in const &> (*this).sin_addr.s_addr,
		        &static_cast<sockaddr_in const &> (that_).sin_addr.s_addr,
		        sizeof (in_addr_t));

		if (cmp != std::strong_ordering::equal)
			return cmp;

		return port () <=> that_.port ();
	}

#ifndef NO_IPV6
	case AF_INET6:
	{
		auto const &addr1 = static_cast<sockaddr_in6 const &> (*this);
		auto const &addr2 = static_cast<sockaddr_in6 const &> (that_);

		if (auto const cmp =
		        strongMemCompare (&addr1.sin6_addr, &addr2.sin6_addr, sizeof (in6_addr));
		    cmp != std::strong_ordering::equal)
			return cmp;

		auto const p1 = port ();
		auto const p2 = that_.port ();

		if (p1 < p2)
			return std::strong_ordering::less;
		else if (p1 > p2)
			return std::strong_ordering::greater;

		if (auto const cmp = strongMemCompare (
		        &addr1.sin6_flowinfo, &addr2.sin6_flowinfo, sizeof (std::uint32_t));
		    cmp != std::strong_ordering::equal)
			return cmp;

		return strongMemCompare (
		    &addr1.sin6_flowinfo, &addr2.sin6_flowinfo, sizeof (std::uint32_t));
	}
#endif

	default:
		std::abort ();
	}
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
	;
}

#ifndef NO_IPV6
void SockAddr::setAddr (in6_addr const &addr_)
{
	if (m_addr.ss_family != AF_INET6)
		std::abort ();

	std::memcpy (&reinterpret_cast<sockaddr_in6 &> (m_addr).sin6_addr, &addr_, sizeof (addr_));
	;
}
#endif

std::uint16_t SockAddr::port () const
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return ntohs (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_port);

#ifndef NO_IPV6
	case AF_INET6:
		return ntohs (reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_port);
#endif

	default:
		std::abort ();
	}
}

void SockAddr::setPort (std::uint16_t const port_)
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		reinterpret_cast<sockaddr_in *> (&m_addr)->sin_port = htons (port_);
		break;

#ifndef NO_IPV6
	case AF_INET6:
		reinterpret_cast<sockaddr_in6 *> (&m_addr)->sin6_port = htons (port_);
		break;
#endif

	default:
		std::abort ();
	}
}

SockAddr::Domain SockAddr::domain () const
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
#ifndef NO_IPV6
	case AF_INET6:
#endif
		return static_cast<Domain> (m_addr.ss_family);

	default:
		std::abort ();
	}
}

socklen_t SockAddr::size () const
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return sizeof (sockaddr_in);

#ifndef NO_IPV6
	case AF_INET6:
		return sizeof (sockaddr_in6);
#endif

	default:
		std::abort ();
	}
}

char const *SockAddr::name (char *buffer_, std::size_t size_) const
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
#ifdef __NDS__
		(void)buffer_;
		(void)size_;
		return inet_ntoa (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr);
#else
		return inet_ntop (
		    AF_INET, &reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr, buffer_, size_);
#endif

#ifndef NO_IPV6
	case AF_INET6:
		return inet_ntop (
		    AF_INET6, &reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_addr, buffer_, size_);
#endif

	default:
		std::abort ();
	}
}

char const *SockAddr::name () const
{
#ifdef __NDS__
	return inet_ntoa (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr);
#else
#ifdef NO_IPV6
	thread_local static char buffer[INET_ADDRSTRLEN];
#else
	thread_local static char buffer[INET6_ADDRSTRLEN];
#endif

	return name (buffer, sizeof (buffer));
#endif
}
