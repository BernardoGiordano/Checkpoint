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

#include "ioBuffer.h"
#include "sockAddr.h"

#include <chrono>
#include <memory>

#ifdef __NDS__
struct pollfd
{
	int fd;
	int events;
	int revents;
};

using nfds_t = unsigned int;

extern "C" int poll (pollfd *fds_, nfds_t nfds_, int timeout_);

#define POLLIN (1 << 0)
#define POLLPRI (1 << 1)
#define POLLOUT (1 << 2)
#define POLLERR (1 << 3)
#define POLLHUP (1 << 4)
#else
#include <poll.h>
#endif

class Socket;
using UniqueSocket = std::unique_ptr<Socket>;
using SharedSocket = std::shared_ptr<Socket>;

/// \brief Socket object
class Socket
{
public:
	enum Type
	{
		eStream   = SOCK_STREAM, ///< Stream socket
		eDatagram = SOCK_DGRAM,  ///< Datagram socket
	};

	/// \brief Poll info
	struct PollInfo
	{
		/// \brief Socket to poll
		std::reference_wrapper<Socket> socket;

		/// \brief Input events
		int events;

		/// \brief Output events
		int revents;
	};

	~Socket ();

	/// \brief Accept connection
	UniqueSocket accept ();

	/// \brief Whether socket is at out-of-band mark
	int atMark ();

	/// \brief Bind socket to address
	/// \param addr_ Address to bind
	bool bind (SockAddr const &addr_);

	/// \brief Connect to a peer
	/// \param addr_ Peer address
	bool connect (SockAddr const &addr_);

	/// \brief Listen for connections
	/// \param backlog_ Queue size for incoming connections
	bool listen (int backlog_);

	/// \brief Shutdown socket
	/// \param how_ Type of shutdown (\sa ::shutdown)
	bool shutdown (int how_);

	/// \brief Set linger option
	/// \param enable_ Whether to enable linger
	/// \param time_ Linger timeout
	bool setLinger (bool enable_, std::chrono::seconds time_);

	/// \brief Set non-blocking
	/// \param nonBlocking_ Whether to set non-blocking
	bool setNonBlocking (bool nonBlocking_ = true);

	/// \brief Set reuse address in subsequent bind
	/// \param reuse_ Whether to reuse address
	bool setReuseAddress (bool reuse_ = true);

	/// \brief Set recv buffer size
	/// \param size_ Buffer size
	bool setRecvBufferSize (std::size_t size_);

	/// \brief Set send buffer size
	/// \param size_ Buffer size
	bool setSendBufferSize (std::size_t size_);

#ifndef __NDS__
	/// \brief Join multicast group
	/// \param addr_ Multicast group address
	/// \param iface_ Interface address
	bool joinMulticastGroup (SockAddr const &addr_, SockAddr const &iface_);

	/// \brief Drop multicast group
	/// \param addr_ Multicast group address
	/// \param iface_ Interface address
	bool dropMulticastGroup (SockAddr const &addr_, SockAddr const &iface_);
#endif

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \param oob_ Whether to read from out-of-band
	std::make_signed_t<std::size_t> read (void *buffer_, std::size_t size_, bool oob_ = false);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param oob_ Whether to read from out-of-band
	std::make_signed_t<std::size_t> read (IOBuffer &buffer_, bool oob_ = false);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \param[out] addr_ Source address
	std::make_signed_t<std::size_t> readFrom (void *buffer_, std::size_t size_, SockAddr &addr_);

	/// \brief Write data
	/// \param buffer_ Input buffer
	/// \param size_ Size to write
	std::make_signed_t<std::size_t> write (void const *buffer_, std::size_t size_);

	/// \brief Write data
	/// \param buffer_ Input buffer
	/// \param size_ Size to write
	std::make_signed_t<std::size_t> write (IOBuffer &buffer_);

	/// \brief Write data
	/// \param buffer_ Input buffer
	/// \param size_ Size to write
	/// \param[out] addr_ Destination address
	std::make_signed_t<std::size_t>
	    writeTo (void const *buffer_, std::size_t size_, SockAddr const &addr_);

	/// \brief Local name
	SockAddr const &sockName () const;
	/// \brief Peer name
	SockAddr const &peerName () const;

	/// \brief Create socket
	/// \param type_ Socket type
	static UniqueSocket create (Type type_);

	/// \brief Poll sockets
	/// \param info_ Poll info
	/// \param count_ Number of poll entries
	/// \param timeout_ Poll timeout
	static int poll (PollInfo *info_, std::size_t count_, std::chrono::milliseconds timeout_);

private:
	Socket () = delete;

	/// \brief Parameterized constructor
	/// \param fd_ Socket fd
	Socket (int fd_);

	/// \brief Parameterized constructor
	/// \param fd_ Socket fd
	/// \param sockName_ Local name
	/// \param peerName_ Peer name
	Socket (int fd_, SockAddr const &sockName_, SockAddr const &peerName_);

	Socket (Socket const &that_) = delete;

	Socket (Socket &&that_) = delete;

	Socket &operator= (Socket const &that_) = delete;

	Socket &operator= (Socket &&that_) = delete;

	/// \param Local name
	SockAddr m_sockName;
	/// \param Peer name
	SockAddr m_peerName;

	/// \param Socket fd
	int const m_fd;

	/// \param Whether listening
	bool m_listening : 1;

	/// \param Whether connected
	bool m_connected : 1;
};
