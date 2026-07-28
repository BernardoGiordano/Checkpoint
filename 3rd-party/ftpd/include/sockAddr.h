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

#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <compare>
#include <cstdint>

/// \brief Socket address
class SockAddr
{
public:
	enum class Domain
	{
		IPv4 = AF_INET,
#ifndef NO_IPV6
		IPv6 = AF_INET6,
#endif
	};

	/// \brief 0.0.0.0
	static SockAddr const AnyIPv4;

#ifndef NO_IPV6
	/// \brief ::
	static SockAddr const AnyIPv6;
#endif

	~SockAddr ();

	SockAddr ();

	/// \brief Parameterized constructor
	/// \param domain_ Socket domain
	/// \note Initial address is INADDR_ANY/in6addr_any
	SockAddr (Domain domain_);

	/// \brief Parameterized constructor
	/// \param addr_ Socket address (network byte order)
	/// \param port_ Socket port (host byte order)
	SockAddr (in_addr_t addr_, std::uint16_t port_ = 0);

	/// \brief Parameterized constructor
	/// \param addr_ Socket address (network byte order)
	/// \param port_ Socket port (host byte order)
	SockAddr (in_addr const &addr_, std::uint16_t port_ = 0);

#ifndef NO_IPV6
	/// \brief Parameterized constructor
	/// \param addr_ Socket address
	/// \param port_ Socket port (host byte order)
	SockAddr (in6_addr const &addr_, std::uint16_t port_ = 0);
#endif

	/// \brief Copy constructor
	/// \param that_ Object to copy
	SockAddr (SockAddr const &that_);

	/// \brief Move constructor
	/// \param that_ Object to move from
	SockAddr (SockAddr &&that_);

	/// \brief Copy assignment
	/// \param that_ Object to copy
	SockAddr &operator= (SockAddr const &that_);

	/// \brief Move assignment
	/// \param that_ Object to move from
	SockAddr &operator= (SockAddr &&that_);

	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_in const &addr_);

#ifndef NO_IPV6
	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_in6 const &addr_);
#endif

	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_storage const &addr_);

	/// \brief sockaddr_in cast operator (network byte order)
	operator sockaddr_in const & () const;

#ifndef NO_IPV6
	/// \brief sockaddr_in6 cast operator (network byte order)
	operator sockaddr_in6 const & () const;
#endif

	/// \brief sockaddr_storage cast operator (network byte order)
	operator sockaddr_storage const & () const;

	/// \brief sockaddr* cast operator (network byte order)
	operator sockaddr * ();

	/// \brief sockaddr const* cast operator (network byte order)
	operator sockaddr const * () const;

	/// \brief Equality operator
	bool operator== (SockAddr const &that_) const;

	/// \brief Comparison operator
	std::strong_ordering operator<=> (SockAddr const &that_) const;

	/// \brief sockaddr domain
	Domain domain () const;

	/// \brief sockaddr size
	socklen_t size () const;

	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in_addr_t addr_);

	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in_addr const &addr_);

#ifndef NO_IPV6
	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in6_addr const &addr_);
#endif

	/// \brief Address port (host byte order)
	std::uint16_t port () const;

	/// \brief Set address port
	/// \param port_ Port to set (host byte order)
	void setPort (std::uint16_t port_);

	/// \brief Address name
	/// \param buffer_ Buffer to hold name
	/// \param size_ Size of buffer_
	/// \retval buffer_ success
	/// \retval nullptr failure
	char const *name (char *buffer_, std::size_t size_) const;

	/// \brief Address name
	/// \retval nullptr failure
	/// \note This function is not reentrant
	char const *name () const;

private:
	/// \brief Address storage (network byte order)
	sockaddr_storage m_addr = {};
};
