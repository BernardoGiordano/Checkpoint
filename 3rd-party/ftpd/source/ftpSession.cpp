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

#include "ftpSession.h"

#include "ftpServer.h"
#include "log.h"
#include "ftpPlatform.h"

#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <string>
using namespace std::chrono_literals;

#if defined(__3DS__) || defined(__SWITCH__)
#define lstat stat
#endif

namespace
{
/// \brief Idle timeout
constexpr auto IDLE_TIMEOUT = 60;

/// \brief Check if string view is a C string
/// \param str_ String to check
bool isCString (std::string_view const str_)
{
	return str_.find_first_of ('\0') != std::string_view::npos;
}

/// \brief Case-insensitive string compare
/// \param lhs_ Left string
/// \param rhs_ Right string
int compare (std::string_view const lhs_, std::string_view const rhs_)
{
	if (isCString (lhs_) && isCString (rhs_))
		return ::strcasecmp (lhs_.data (), rhs_.data ());

	auto const maxLen = std::min (lhs_.size (), rhs_.size ());
	for (unsigned i = 0; i < maxLen; ++i)
	{
		auto const l = std::tolower (lhs_[i]);
		auto const r = std::tolower (rhs_[i]);
		if (l != r)
			return l - r;
	}

	return static_cast<int> (lhs_.size ()) - static_cast<int> (rhs_.size ());
}

/// \brief Parse command
/// \param buffer_ Buffer to parse
/// \param size_ Size of buffer
/// \returns {delimiterPos, nextPos}
std::pair<char *, char *> parseCommand (char *const buffer_, std::size_t const size_)
{
	// look for \r\n or \n delimiter
	auto const end = &buffer_[size_];
	for (auto p = buffer_; p < end; ++p)
	{
		if (p[0] == '\r' && p < end - 1 && p[1] == '\n')
			return {p, &p[2]};

		if (p[0] == '\n')
			return {p, &p[1]};
	}

	return {nullptr, nullptr};
}

/// \brief Decode path
/// \param buffer_ Buffer to decode
/// \param size_ Size of buffer
void decodePath (char *const buffer_, std::size_t const size_)
{
	auto const end = &buffer_[size_];
	for (auto p = buffer_; p < end; ++p)
	{
		// this is an encoded \n
		if (*p == '\0')
			*p = '\n';
	}
}

/// \brief Encode path
/// \param buffer_ Buffer to encode
/// \param quotes_ Whether to encode quotes
std::string encodePath (std::string_view const buffer_, bool const quotes_ = false)
{
	// check if the buffer has \n
	bool const lf = std::memchr (buffer_.data (), '\n', buffer_.size ());

	auto end = std::end (buffer_);

	std::size_t numQuotes = 0;
	if (quotes_)
	{
		// check for \" that needs to be encoded
		auto p = buffer_.data ();
		do
		{
			p = static_cast<char const *> (std::memchr (p, '"', end - p));
			if (p)
			{
				++p;
				++numQuotes;
			}
		} while (p);
	}

	// if nothing needs escaping, return it as-is
	if (!lf && !numQuotes)
		return std::string (buffer_);

	// reserve output buffer
	std::string path (buffer_.size () + numQuotes, '\0');
	auto in  = buffer_.data ();
	auto out = path.data ();

	// encode into the output buffer
	while (in < end)
	{
		if (*in == '\n')
		{
			// encoded \n is \0
			*out++ = '\0';
		}
		else if (quotes_ && *in == '"')
		{
			// encoded \" is \"\"
			*out++ = '"';
			*out++ = '"';
		}
		else
			*out++ = *in;
		++in;
	}

	return path;
}

/// \brief Get parent directory name of a path
/// \param path_ Path to get parent of
std::string dirName (std::string_view const path_)
{
	// remove last path component
	auto const dir = std::string (path_.substr (0, path_.rfind ('/')));
	if (dir.empty ())
		return "/";

	return dir;
}

/// \brief Resolve path
/// \param path_ Path to resolve
std::string resolvePath (std::string_view const path_)
{
	assert (!path_.empty ());
	assert (path_[0] == '/');

	// make sure parent is a directory
	stat_t st;
	if (::stat (dirName (path_).c_str (), &st) != 0)
		return {};

	if (!S_ISDIR (st.st_mode))
	{
		errno = ENOTDIR;
		return {};
	}

	// split path components
	std::vector<std::string_view> components;

	std::size_t pos = 1;
	auto next       = path_.find ('/', pos);
	while (next != std::string::npos)
	{
		if (next != pos)
			components.emplace_back (path_.substr (pos, next - pos));
		pos  = next + 1;
		next = path_.find ('/', pos);
	}

	if (pos != path_.size ())
		components.emplace_back (path_.substr (pos));

	// collapse . and ..
	auto it = std::begin (components);
	while (it != std::end (components))
	{
		if (*it == ".")
		{
			it = components.erase (it);
			continue;
		}

		if (*it == "..")
		{
			if (it != std::begin (components))
				it = components.erase (std::prev (it));
			it = components.erase (it);
			continue;
		}

		++it;
	}

	// join path components
	std::string outPath = "/";
	for (auto const &component : components)
	{
		outPath += component;
		outPath.push_back ('/');
	}

	if (outPath.size () > 1)
		outPath.pop_back ();

	return outPath;
}

/// \brief Build path from a parent and child
/// \param cwd_ Parent directory
/// \param args_ Child component
std::string buildPath (std::string_view const cwd_, std::string_view const args_)
{
	std::string path;

	// absolute path
	if (args_[0] == '/')
		path = std::string (args_);
	// relative path
	else
		path = std::string (cwd_) + '/' + std::string (args_);

	// coalesce consecutive slashes
	auto it = std::begin (path);
	while (it != std::end (path))
	{
		if (it != std::begin (path) && *it == '/' && *std::prev (it) == '/')
			it = path.erase (it);
		else
			++it;
	}

	return path;
}

/// \brief Build resolved path from a parent and child
/// \param cwd_ Parent directory
/// \param args_ Child component
std::string buildResolvedPath (std::string_view const cwd_, std::string_view const args_)
{
	return resolvePath (buildPath (cwd_, args_));
}
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
FtpSession::~FtpSession ()
{
	// Network loss can destroy a session without routing it through COMMAND.
	// Keep the platform's active-transfer count balanced in that path too.
	if (m_stats.active)
	{
		platform::transferEnd ();
		m_stats.active = false;
	}

	closeCommand ();
	closePasv ();
	closeData ();
}

FtpSession::FtpSession (FtpConfig const &config_, UniqueSocket commandSocket_)
    : m_config (config_),
      m_commandSocket (std::move (commandSocket_)),
      m_commandBuffer (COMMAND_BUFFERSIZE),
      m_responseBuffer (RESPONSE_BUFFERSIZE),
      m_xferBuffer (FILE_BUFFERSIZE),
      m_authorizedUser (false),
      m_authorizedPass (false),
      m_pasv (false),
      m_port (false),
      m_recv (false),
      m_send (false),
      m_urgent (false),
      m_eof (false),
      m_mlstType (true),
      m_mlstSize (true),
      m_mlstModify (true),
      m_mlstPerm (true),
      m_mlstUnixMode (false)
{
	if (m_config.user ().empty ())
		m_authorizedUser = true;
	if (m_config.pass ().empty ())
		m_authorizedPass = true;

	m_commandSocket->setNonBlocking ();

	sendResponse ("220 Hello!\r\n");
}

bool FtpSession::dead ()
{
	if (m_commandSocket || m_pasvSocket || m_dataSocket)
		return false;

	return true;
}

UniqueFtpSession FtpSession::create (FtpConfig const &config_, UniqueSocket commandSocket_)
{
	return UniqueFtpSession (new FtpSession (config_, std::move (commandSocket_)));
}

bool FtpSession::poll (std::vector<UniqueFtpSession> const &sessions_)
{
	// poll for pending close sockets first
	std::vector<Socket::PollInfo> pollInfo;
	for (auto &session : sessions_)
	{
		for (auto &pending : session->m_pendingCloseSocket)
		{
			assert (pending.unique ());
			pollInfo.emplace_back (*pending, POLLIN, 0);
		}
	}

	if (!pollInfo.empty ())
	{
		auto const rc = Socket::poll (pollInfo.data (), pollInfo.size (), 0ms);
		if (rc < 0)
		{
			error ("poll: %s\n", std::strerror (errno));
			return false;
		}
		else
		{
			for (auto const &i : pollInfo)
			{
				if (!i.revents)
					continue;

				for (auto &session : sessions_)
				{
					for (auto it = std::begin (session->m_pendingCloseSocket);
					     it != std::end (session->m_pendingCloseSocket);)
					{
						auto &socket = *it;
						if (&i.socket.get () != socket.get ())
						{
							++it;
							continue;
						}

						it = session->m_pendingCloseSocket.erase (it);
					}
				}
			}
		}
	}

	// poll for everything else
	pollInfo.clear ();
	for (auto &session : sessions_)
	{
		if (session->m_commandSocket)
		{
			pollInfo.emplace_back (*session->m_commandSocket, POLLIN | POLLPRI, 0);
			if (session->m_responseBuffer.usedSize () != 0)
				pollInfo.back ().events |= POLLOUT;
		}

		switch (session->m_state)
		{
		case State::COMMAND:
			// we are waiting to read a command
			break;

		case State::DATA_CONNECT:
			if (session->m_pasv)
			{
				assert (!session->m_port);
				// we are waiting for a PASV connection
				pollInfo.emplace_back (*session->m_pasvSocket, POLLIN, 0);
			}
			else
			{
				// we are waiting to complete a PORT connection
				pollInfo.emplace_back (*session->m_dataSocket, POLLOUT, 0);
			}
			break;

		case State::DATA_TRANSFER:
			// we need to transfer data
			if (session->m_recv)
			{
				assert (!session->m_send);
				pollInfo.emplace_back (*session->m_dataSocket, POLLIN, 0);
			}
			else
			{
				assert (session->m_send);
				pollInfo.emplace_back (*session->m_dataSocket, POLLOUT, 0);
			}
			break;
		}
	}

	if (pollInfo.empty ())
		return true;

	// poll for activity
	auto const rc = Socket::poll (pollInfo.data (), pollInfo.size (), 100ms);
	if (rc < 0)
	{
		error ("poll: %s\n", std::strerror (errno));
		return false;
	}

	auto const now = std::time (nullptr);

	for (auto &session : sessions_)
	{
		bool handled = false;
		for (auto const &i : pollInfo)
		{
			if (!i.revents)
				continue;

			handled = true;

			// check command socket
			if (&i.socket.get () == session->m_commandSocket.get ())
			{
				if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
					debug ("Command revents 0x%X\n", i.revents);

				if (!session->m_dataSocket && (i.revents & POLLOUT))
					session->writeResponse ();

				if (i.revents & (POLLIN | POLLPRI))
					session->readCommand (i.revents);

				if (i.revents & (POLLERR | POLLHUP))
					session->closeCommand ();
			}

			// check the data socket
			if (&i.socket.get () == session->m_pasvSocket.get () ||
			    &i.socket.get () == session->m_dataSocket.get ())
			{
				switch (session->m_state)
				{
				case State::COMMAND:
					std::abort ();
					break;

				case State::DATA_CONNECT:
					if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
						debug ("Data revents 0x%X\n", i.revents);

					if (i.revents & (POLLERR | POLLHUP))
					{
						session->sendResponse ("426 Data connection failed\r\n");
						session->setState (State::COMMAND, true, true);
					}
					else if (i.revents & POLLIN)
					{
						// we need to accept the PASV connection
						session->dataAccept ();
					}
					else if (i.revents & POLLOUT)
					{
						// PORT connection completed
						auto const &sockName = session->m_dataSocket->peerName ();
						info ("Connected to [%s]:%u\n", sockName.name (), sockName.port ());

						session->sendResponse ("150 Ready\r\n");
						session->setState (State::DATA_TRANSFER, true, false);
					}
					break;

				case State::DATA_TRANSFER:
					if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
						debug ("Data revents 0x%X\n", i.revents);

					// we need to transfer data
					if (i.revents & (POLLERR | POLLHUP))
					{
						session->sendResponse ("426 Data connection failed\r\n");
						session->setState (State::COMMAND, true, true);
					}
					else if (i.revents & (POLLIN | POLLOUT))
					{
						session->statsWake ();

						for (unsigned i = 0; i < 10; ++i)
						{
							if (!((*session).*(session->m_transfer)) ())
								break;
						}
					}
					break;
				}
			}
		}

		if (!handled && now - session->m_timestamp >= IDLE_TIMEOUT)
		{
			session->closeCommand ();
			session->closePasv ();
			session->closeData ();
		}
	}

	return true;
}

bool FtpSession::authorized () const
{
	return m_authorizedUser && m_authorizedPass;
}

void FtpSession::setState (State const state_, bool const closePasv_, bool const closeData_)
{
	m_state     = state_;
	m_timestamp = std::time (nullptr);

	if (closePasv_)
		closePasv ();
	if (closeData_)
		closeData ();

	if (state_ == State::COMMAND)
	{
		m_restartPosition = 0;
		m_filePosition    = 0;

		if (m_stats.active)
		{
			auto const t0 = monotonicUs ();
			m_file.close ();
			m_stats.diskUs += monotonicUs () - t0;
		}
		else
			m_file.close ();

		m_dir.close ();
		statsReport ();
	}
}

void FtpSession::closeSocket (SharedSocket &socket_)
{
	if (socket_ && socket_.unique ())
	{
		socket_->shutdown (SHUT_WR);
		socket_->setLinger (true, 0s);
		m_pendingCloseSocket.emplace_back (std::move (socket_));
	}
	else
		socket_.reset ();
}

void FtpSession::closeCommand ()
{
	closeSocket (m_commandSocket);
}

void FtpSession::closePasv ()
{
	m_pasvSocket.reset ();
}

void FtpSession::closeData ()
{
	closeSocket (m_dataSocket);

	m_recv = false;
	m_send = false;
}

bool FtpSession::changeDir (char const *const args_)
{
	if (std::strcmp (args_, "..") == 0)
	{
		// cd up
		auto const pos = m_cwd.find_last_of ('/');
		assert (pos != std::string::npos);
		if (pos == 0)
			m_cwd = "/";
		else
			m_cwd = m_cwd.substr (0, pos);
		return true;
	}

	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
		return false;

	stat_t st;
	if (tzStat (path.c_str (), &st) != 0)
		return false;

	if (!S_ISDIR (st.st_mode))
	{
		errno = ENOTDIR;
		return false;
	}

	m_cwd = path;
	return true;
}

bool FtpSession::dataAccept ()
{
	if (!m_pasv)
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	m_pasv = false;

	auto peer = m_pasvSocket->accept ();
	m_dataSocket = std::move (peer);
	if (!m_dataSocket)
	{
		sendResponse ("425 Failed to establish connection\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

#ifndef __3DS__
	m_dataSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_dataSocket->setSendBufferSize (SOCK_BUFFERSIZE);
#endif

	if (!m_dataSocket->setNonBlocking ())
	{
		sendResponse ("425 Failed to establish connection\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	// we are ready to transfer data
	sendResponse ("150 Ready\r\n");
	setState (State::DATA_TRANSFER, true, false);
	return true;
}

bool FtpSession::dataConnect ()
{
	assert (m_port);

	m_port = false;

	auto data = Socket::create ();
	m_dataSocket = std::move (data);
	if (!m_dataSocket)
		return false;

	m_dataSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_dataSocket->setSendBufferSize (SOCK_BUFFERSIZE);

	if (!m_dataSocket->setNonBlocking ())
		return false;

	if (!m_dataSocket->connect (m_portAddr))
	{
		if (errno != EINPROGRESS)
			return false;

		return true;
	}

	// we are ready to transfer data
	sendResponse ("150 Ready\r\n");
	setState (State::DATA_TRANSFER, true, false);
	return true;
}

fs::Dir::Detail FtpSession::listDetail () const
{
	// NLST prints names and nothing else, so no entry is worth resolving
	if (m_xferDirMode == XferDirMode::NLST)
		return fs::Dir::Detail::Name;

	// MLSD/MLST only print a modify fact when the client asked for one
	if ((m_xferDirMode == XferDirMode::MLSD || m_xferDirMode == XferDirMode::MLST) && !m_mlstModify)
		return fs::Dir::Detail::Status;

#ifdef __3DS__
	// On 3DS an mtime is an FSUSER round trip of its own per entry, which is the
	// cost this setting exists to switch off
	if (!m_config.getMTime ())
		return fs::Dir::Detail::Status;
#endif

	return fs::Dir::Detail::StatusMTime;
}

std::time_t FtpSession::tzOffset ()
{
#ifdef __3DS__
	return FtpServer::tzOffset ();
#else
	return 0;
#endif
}

int FtpSession::tzStat (char const *const path_, stat_t *st_)
{
	auto const rc = ::stat (path_, st_);
	if (rc != 0)
		return rc;

#ifdef __3DS__
	if (m_config.getMTime ())
	{
		std::uint64_t mtime = 0;
		auto const rc       = archive_getmtime (path_, &mtime);
		if (rc != 0)
			error ("sdmc_getmtime %s 0x%lx\n", path_, rc);
		else
			st_->st_mtime = mtime - FtpServer::tzOffset ();
	}
#endif

	return 0;
}

int FtpSession::tzLStat (char const *const path_, stat_t *st_)
{
	auto const rc = ::lstat (path_, st_);
	if (rc != 0)
		return rc;

#ifdef __3DS__
	if (m_config.getMTime ())
	{
		std::uint64_t mtime = 0;
		auto const rc       = archive_getmtime (path_, &mtime);
		if (rc != 0)
			error ("sdmc_getmtime %s 0x%lx\n", path_, rc);
		else
			st_->st_mtime = mtime - FtpServer::tzOffset ();
	}
#endif

	return 0;
}

int FtpSession::fillDirent (stat_t const &st_, std::string_view const path_, char const *type_)
{
	auto const buffer = m_xferBuffer.freeArea ();
	auto const size   = m_xferBuffer.freeSize ();

	std::size_t pos = 0;

	if (m_xferDirMode == XferDirMode::MLSD || m_xferDirMode == XferDirMode::MLST)
	{
		if (m_xferDirMode == XferDirMode::MLST)
		{
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = ' ';
		}

		// type fact
		if (m_mlstType)
		{
			if (!type_)
			{
				type_ = "???";
				if (S_ISREG (st_.st_mode))
					type_ = "file";
				else if (S_ISDIR (st_.st_mode))
					type_ = "dir";
#if !defined(__3DS__) && !defined(__SWITCH__)
				else if (S_ISLNK (st_.st_mode))
					type_ = "os.unix=symlink";
				else if (S_ISCHR (st_.st_mode))
					type_ = "os.unix=character";
				else if (S_ISBLK (st_.st_mode))
					type_ = "os.unix=block";
				else if (S_ISFIFO (st_.st_mode))
					type_ = "os.unix=fifo";
				else if (S_ISSOCK (st_.st_mode))
					type_ = "os.unix=socket";
#endif
			}

			auto const rc = std::snprintf (&buffer[pos], size - pos, "Type=%s;", type_);
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// size fact
		if (m_mlstSize)
		{
			auto const rc = std::snprintf (&buffer[pos],
			    size - pos,
			    "Size=%llu;",
			    static_cast<unsigned long long> (st_.st_size));
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// mtime fact
		if (m_mlstModify)
		{
			auto const tm = std::gmtime (&st_.st_mtime);
			if (!tm)
				return errno;

			auto const rc = std::strftime (&buffer[pos], size - pos, "Modify=%Y%m%d%H%M%S;", tm);
			if (rc == 0)
				return EAGAIN;

			pos += rc;
		}

		// permission fact
		if (m_mlstPerm)
		{
			auto const header = "Perm=";
			if (size - pos < std::strlen (header))
				return EAGAIN;

			std::strcpy (&buffer[pos], header);
			pos += std::strlen (header);

			// append permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'a';
			}

			// create permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'c';
			}

			// delete permission
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = 'd';

			// chdir permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IXUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'e';
			}

			// rename permission
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = 'f';

			// list permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IRUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'l';
			}

			// mkdir permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'm';
			}

			// purge permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'p';
			}

			// read permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IRUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'r';
			}

			// write permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'w';
			}

			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = ';';
		}

		// unix mode fact
		if (m_mlstUnixMode)
		{
			auto const mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX | S_ISGID | S_ISUID;

			auto const rc = std::snprintf (&buffer[pos],
			    size - pos,
			    "UNIX.mode=0%lo;",
			    static_cast<unsigned long> (st_.st_mode & mask));
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// make sure space precedes name
		if (buffer[pos - 1] != ' ')
		{
			if (pos >= size)
				return EAGAIN;

			buffer[pos++] = ' ';
		}
	}
	else if (m_xferDirMode != XferDirMode::NLST)
	{
		if (m_xferDirMode == XferDirMode::STAT)
		{
			if (pos >= size)
				return EAGAIN;

			buffer[pos++] = ' ';
		}

#ifdef __3DS__
		auto const owner = "3DS";
		auto const group = "3DS";
#elif defined(__SWITCH__)
		auto const owner = "Switch";
		auto const group = "Switch";
#else
		char owner[32];
		char group[32];
		std::sprintf (owner, "%d", st_.st_uid);
		std::sprintf (group, "%d", st_.st_gid);
#endif
		// perms nlinks owner group size
		auto rc = std::snprintf (&buffer[pos],
		    size - pos,
		    "%c%c%c%c%c%c%c%c%c%c %lu %s %s %llu ",
		    // clang-format off
		    S_ISREG (st_.st_mode)  ? '-' :
		    S_ISDIR (st_.st_mode)  ? 'd' :
#if !defined(__3DS__) && !defined(__SWITCH__)
		    S_ISLNK (st_.st_mode)  ? 'l' :
		    S_ISCHR (st_.st_mode)  ? 'c' :
		    S_ISBLK (st_.st_mode)  ? 'b' :
		    S_ISFIFO (st_.st_mode) ? 'p' :
		    S_ISSOCK (st_.st_mode) ? 's' :
#endif
		    '?',
		    // clang-format on
		    st_.st_mode & S_IRUSR ? 'r' : '-',
		    st_.st_mode & S_IWUSR ? 'w' : '-',
		    st_.st_mode & S_IXUSR ? 'x' : '-',
		    st_.st_mode & S_IRGRP ? 'r' : '-',
		    st_.st_mode & S_IWGRP ? 'w' : '-',
		    st_.st_mode & S_IXGRP ? 'x' : '-',
		    st_.st_mode & S_IROTH ? 'r' : '-',
		    st_.st_mode & S_IWOTH ? 'w' : '-',
		    st_.st_mode & S_IXOTH ? 'x' : '-',
		    static_cast<unsigned long> (st_.st_nlink),
		    owner,
		    group,
		    static_cast<unsigned long long> (st_.st_size));
		if (rc < 0)
			return errno;

		if (static_cast<std::size_t> (rc) > size - pos)
			return EAGAIN;

		pos += rc;

		// timestamp
		auto const tm = std::gmtime (&st_.st_mtime);
		if (!tm)
			return errno;

		auto fmt = "%b %e %Y ";
		if (m_timestamp > st_.st_mtime && m_timestamp - st_.st_mtime < (60 * 60 * 24 * 365 / 2))
			fmt = "%b %e %H:%M ";
		rc = std::strftime (&buffer[pos], size - pos, fmt, tm);
		if (rc < 0)
			return errno;
		if (static_cast<std::size_t> (rc) > size - pos)
			return EAGAIN;

		pos += rc;
	}

	if (size - pos < path_.size () + 2)
		return EAGAIN;

	// path
	std::memcpy (&buffer[pos], path_.data (), path_.size ());
	pos += path_.size ();
	buffer[pos++] = '\r';
	buffer[pos++] = '\n';

	m_xferBuffer.markUsed (pos);
	m_filePosition += pos;

	return 0;
}

int FtpSession::fillDirent (std::string const &path_, char const *type_)
{
	stat_t st;
	if (tzStat (path_.c_str (), &st) != 0)
		return errno;

	return fillDirent (st, encodePath (path_), type_);
}

void FtpSession::xferFile (char const *const args_, XferFileMode const mode_)
{
	m_eof = false;

	m_xferBuffer.clear ();

	// build the path of the file to transfer
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		setState (State::COMMAND, true, true);
		return;
	}

	if (mode_ == XferFileMode::RETR)
	{
		if (!m_file.open (path.c_str (), fs::File::Mode::Read))
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		if (m_restartPosition != 0 && !m_file.seek (m_restartPosition))
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		m_filePosition = m_restartPosition;
	}
	else
	{
		auto const append = mode_ == XferFileMode::APPE;

		auto mode = fs::File::Mode::Truncate;
		if (append)
			mode = fs::File::Mode::Append;
		else if (m_restartPosition != 0)
			mode = fs::File::Mode::Modify;

		// open file in write mode
		if (!m_file.open (path.c_str (), mode))
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		// check if this had REST but not APPE
		if (m_restartPosition != 0 && !append)
		{
			// seek to the REST offset
			if (!m_file.seek (m_restartPosition))
			{
				sendResponse ("450 %s\r\n", std::strerror (errno));
				return;
			}
		}

		m_filePosition = m_restartPosition;
	}

	if (!m_port && !m_pasv)
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	setState (State::DATA_CONNECT, false, true);

	// setup connection
	if (m_port && !dataConnect ())
	{
		sendResponse ("425 Can't open data connection\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	// set up the transfer
	if (mode_ == XferFileMode::RETR)
	{
		m_recv     = false;
		m_send     = true;
		m_transfer = &FtpSession::retrieveTransfer;
		statsBegin (true);
	}
	else
	{
		m_recv     = true;
		m_send     = false;
		m_transfer = &FtpSession::storeTransfer;
		statsBegin (false);
	}
}

void FtpSession::xferDir (char const *const args_, XferDirMode const mode_, bool const workaround_)
{
	// set up the transfer
	m_xferDirMode = mode_;
	m_recv        = false;
	m_send        = true;
	m_eof         = false;

	m_filePosition = 0;
	m_xferBuffer.clear ();

	m_transfer = &FtpSession::listTransfer;

	if (std::strlen (args_) > 0)
	{
		// work around broken clients that think LIST -a/-l is valid
		auto const needWorkaround = workaround_ && args_[0] == '-' &&
		                            (args_[1] == 'a' || args_[1] == 'l') &&
		                            (args_[2] == '\0' || args_[2] == ' ');

		// an argument was provided
		auto const path = buildResolvedPath (m_cwd, args_);
		if (path.empty ())
		{
			if (needWorkaround)
			{
				xferDir (args_ + 2 + (args_[2] == ' '), mode_, false);
				return;
			}

			sendResponse ("550 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return;
		}

		stat_t st;
		if (tzStat (path.c_str (), &st) != 0)
		{
			if (needWorkaround)
			{
				xferDir (args_ + 2 + (args_[2] == ' '), mode_, false);
				return;
			}

			sendResponse ("550 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return;
		}

		if (mode_ == XferDirMode::MLST)
		{
			auto const rc = fillDirent (st, path);
			if (rc != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (rc));
				setState (State::COMMAND, true, true);
				return;
			}
		}
		else if (S_ISDIR (st.st_mode))
		{
			if (!m_dir.open (path.c_str (), listDetail (), tzOffset ()))
			{
				sendResponse ("550 %s\r\n", std::strerror (errno));
				setState (State::COMMAND, true, true);
				return;
			}

			// set as lwd
			m_lwd = std::move (path);

			if (mode_ == XferDirMode::MLSD && m_mlstType)
			{
				// send this directory as type=cdir
				auto const rc = fillDirent (st, m_lwd, "cdir");
				if (rc != 0)
				{
					sendResponse ("550 %s\r\n", std::strerror (rc));
					setState (State::COMMAND, true, true);
					return;
				}
			}
		}
		else if (mode_ == XferDirMode::MLSD)
		{
			// specified file instead of directory for MLSD
			sendResponse ("501 %s\r\n", std::strerror (ENOTDIR));
			setState (State::COMMAND, true, true);
			return;
		}
		else
		{
			std::string name;
			if (mode_ == XferDirMode::NLST)
			{
				// NLST uses full path name
				name = encodePath (path);
			}
			else
			{
				// everything else uses basename
				auto const pos = path.find_last_of ('/');
				assert (pos != std::string::npos);
				name = encodePath (std::string_view (path).substr (pos + 1));
			}

			auto const rc = fillDirent (st, name);
			if (rc != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (rc));
				setState (State::COMMAND, true, true);
				return;
			}
		}
	}
	else if (mode_ == XferDirMode::MLST)
	{
		auto const rc = fillDirent (m_cwd);
		if (rc != 0)
		{
			sendResponse ("550 %s\r\n", std::strerror (rc));
			setState (State::COMMAND, true, true);
			return;
		}
	}
	else if (!m_dir.open (m_cwd.c_str (), listDetail (), tzOffset ()))
	{
		// no argument, but opening cwd failed
		sendResponse ("550 %s\r\n", std::strerror (errno));
		setState (State::COMMAND, true, true);
		return;
	}
	else
	{
		// set the cwd as the lwd
		m_lwd = m_cwd;

		if (mode_ == XferDirMode::MLSD && m_mlstType)
		{
			// send this directory as type=cdir
			auto const rc = fillDirent (m_lwd, "cdir");
			if (rc != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (rc));
				setState (State::COMMAND, true, true);
				return;
			}
		}
	}

	if (mode_ == XferDirMode::MLST || mode_ == XferDirMode::STAT)
	{
		// this is a little different; we have to send the data over the command socket
		sendResponse ("250-Status\r\n");
		setState (State::DATA_TRANSFER, true, true);
		m_dataSocket = m_commandSocket;
		m_send = true;
		return;
	}

	if (!m_port && !m_pasv)
	{
		// Prior PORT or PASV required
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	setState (State::DATA_CONNECT, false, true);
	m_send = true;

	// setup connection
	if (m_port && !dataConnect ())
	{
		sendResponse ("425 Can't open data connection\r\n");
		setState (State::COMMAND, true, true);
	}
}

void FtpSession::readCommand (int const events_)
{
	// check out-of-band data
	if (events_ & POLLPRI)
	{
		m_urgent = true;

		// check if we are at the urgent marker
		auto const atMark = m_commandSocket->atMark ();
		if (atMark < 0)
		{
			closeCommand ();
			return;
		}

		if (!atMark)
		{
			// discard in-band data
			m_commandBuffer.clear ();
			auto const rc = m_commandSocket->read (m_commandBuffer);
			if (rc < 0 && errno != EWOULDBLOCK)
				closeCommand ();
			else
				m_timestamp = std::time (nullptr);

			return;
		}

		// retrieve the urgent data
		m_commandBuffer.clear ();
		auto const rc = m_commandSocket->read (m_commandBuffer, true);
		if (rc < 0)
		{
			// EWOULDBLOCK means out-of-band data is on the way
			if (errno != EWOULDBLOCK)
				closeCommand ();
			else
				m_timestamp = std::time (nullptr);

			return;
		}
		else
			m_timestamp = std::time (nullptr);

		// reset the command buffer
		m_commandBuffer.clear ();
		return;
	}

	if (events_ & POLLIN)
	{
		// prepare to receive data
		if (m_commandBuffer.freeSize () == 0)
		{
			error ("Exceeded command buffer size\n");
			closeCommand ();
			return;
		}

		auto const rc = m_commandSocket->read (m_commandBuffer);
		if (rc < 0)
		{
			closeCommand ();
			return;
		}

		if (rc == 0)
		{
			// peer closed connection
			info ("Peer closed connection\n");
			closeCommand ();
			return;
		}

		m_timestamp = std::time (nullptr);

		if (m_urgent)
		{
			// look for telnet data mark
			auto const buffer = m_commandBuffer.usedArea ();
			auto const size   = m_commandBuffer.usedSize ();
			auto const mark   = static_cast<char const *> (std::memchr (buffer, 0xF2, size));
			if (!mark)
				return;

			// ignore all data that precedes the data mark
			m_commandBuffer.markFree (mark + 1 - buffer);
			m_commandBuffer.coalesce ();
			m_urgent = false;
		}
	}

	// loop through commands
	while (true)
	{
		// must have at least enough data for the delimiter
		auto const size = m_commandBuffer.usedSize ();
		if (size < 1)
			return;

		auto const buffer        = m_commandBuffer.usedArea ();
		auto const [delim, next] = parseCommand (buffer, size);
		if (!next)
			return;

		*delim = '\0';
		decodePath (buffer, delim - buffer);
		if (::strncasecmp ("USER ", buffer, 5) == 0 || ::strncasecmp ("PASS ", buffer, 5) == 0)
			command ("%.*s ******\n", 5, buffer);
		else
			command ("%s\n", buffer);

		char const *const command = buffer;

		char *args = buffer;
		while (*args && !std::isspace (*args))
			++args;
		if (*args)
			*args++ = 0;

		auto const it = std::lower_bound (std::begin (handlers),
		    std::end (handlers),
		    command,
		    [] (auto const &lhs_, auto const &rhs_) { return compare (lhs_.first, rhs_) < 0; });

		m_timestamp = std::time (nullptr);
		if (it == std::end (handlers) || compare (it->first, command) != 0)
		{
			std::string response = "502 Invalid command \"";
			response += encodePath (command);

			if (*args)
			{
				response.push_back (' ');
				response += encodePath (args);
			}

			response += "\"\r\n";

			sendResponse (response);
		}
		else if (m_state != State::COMMAND)
		{
			// only some commands are available during data transfer
			if (compare (command, "ABOR") != 0 && compare (command, "NOOP") != 0 &&
			    compare (command, "PWD") != 0 && compare (command, "QUIT") != 0 &&
			    compare (command, "STAT") != 0 && compare (command, "XPWD") != 0)
			{
				sendResponse ("503 Invalid command during transfer\r\n");
				setState (State::COMMAND, true, true);
				closeCommand ();
			}
			else
			{
				auto const handler = it->second;
				(this->*handler) (args);
			}
		}
		else
		{
			// clear rename for all commands except RNTO
			if (compare (command, "RNTO") != 0)
				m_rename.clear ();

			auto const handler = it->second;
			(this->*handler) (args);
		}

		m_commandBuffer.markFree (next - buffer);
		m_commandBuffer.coalesce ();
	}
}

void FtpSession::writeResponse ()
{
	auto const rc = m_commandSocket->write (m_responseBuffer);
	if (rc <= 0)
	{
		closeCommand ();
		return;
	}

	m_timestamp = std::time (nullptr);

	m_responseBuffer.coalesce ();
}

void FtpSession::sendResponse (char const *fmt_, ...)
{
	if (!m_commandSocket)
		return;

	auto const buffer = m_responseBuffer.freeArea ();
	auto const size   = m_responseBuffer.freeSize ();

	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_RESPONSE, fmt_, ap);
	va_end (ap);

	va_start (ap, fmt_);
	auto const rc = std::vsnprintf (buffer, size, fmt_, ap);
	va_end (ap);

	if (rc < 0)
	{
		error ("vsnprintf: %s\n", std::strerror (errno));
		closeCommand ();
		return;
	}

	if (static_cast<std::size_t> (rc) > size)
	{
		error ("Not enough space for response\n");
		closeCommand ();
		return;
	}

	m_responseBuffer.markUsed (rc);

	// try to write data immediately
	assert (m_commandSocket);
	auto const bytes = m_commandSocket->write (m_responseBuffer);
	if (bytes <= 0)
	{
		if (bytes == 0 || errno != EWOULDBLOCK)
			closeCommand ();
	}
	else
	{
		m_timestamp = std::time (nullptr);
		m_responseBuffer.coalesce ();
	}
}

void FtpSession::sendResponse (std::string_view const response_)
{
	if (!m_commandSocket)
		return;

	addLog (FTP_RESPONSE, response_);

	auto const buffer = m_responseBuffer.freeArea ();
	auto const size   = m_responseBuffer.freeSize ();

	if (response_.size () > size)
	{
		error ("Not enough space for response\n");
		closeCommand ();
		return;
	}

	std::memcpy (buffer, response_.data (), response_.size ());
	m_responseBuffer.markUsed (response_.size ());
}

bool FtpSession::listTransfer ()
{
	// check if we sent all available data
	while (m_xferBuffer.empty ())
	{
		m_xferBuffer.clear ();

		// check xfer dir type
		int rc = 226;
		if (m_xferDirMode == XferDirMode::MLST || m_xferDirMode == XferDirMode::STAT)
			rc = 250;

		if (m_eof)
		{
			sendResponse ("%d OK\r\n", rc);
			setState (State::COMMAND, true, true);
			return false;
		}

		// check if this was for a file/MLST
		if (!m_dir)
		{
			// we already sent the file's listing
			m_eof = true;
			return true;
		}

		// get the next directory entry
		auto const entry = m_dir.read ();
		if (!entry)
		{
			// we have exhausted the directory listing
			m_eof = true;
			return true;
		}

		// check if this was NLST
		if (m_xferDirMode == XferDirMode::NLST)
		{
			// NLST gives the whole path name
			auto const path = encodePath (buildPath (m_lwd, entry->name)) + "\r\n";
			if (m_xferBuffer.freeSize () < path.size ())
			{
				sendResponse ("501 %s\r\n", std::strerror (ENOMEM));
				setState (State::COMMAND, true, true);
				return false;
			}

			std::memcpy (m_xferBuffer.freeArea (), path.data (), path.size ());
			m_xferBuffer.markUsed (path.size ());
			m_filePosition += path.size ();
		}
		else
		{
			auto const path = encodePath (entry->name);
			auto const rc   = fillDirent (entry->status, path);
			if (rc != 0)
			{
				sendResponse ("425 %s\r\n", std::strerror (errno));
				setState (State::COMMAND, true, true);
				return false;
			}
		}
	}

	// send any pending data
	auto const rc = sendXferBuffer ();
	if (rc <= 0)
	{
		// error sending data
		if (rc < 0 && errno == EWOULDBLOCK)
			return false;

		sendResponse ("426 Connection broken during transfer\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	m_timestamp = std::time (nullptr);

	// we can try to send more data
	return true;
}

bool FtpSession::retrieveTransfer ()
{
	if (m_xferBuffer.empty ())
	{
		m_xferBuffer.clear ();

		if (m_eof)
		{
			sendResponse ("226 OK\r\n");
			setState (State::COMMAND, true, true);
			return false;
		}

		// we have sent all the data, so read some more
		auto const rc = readFile (m_xferBuffer);
		if (rc < 0)
		{
			// failed to read data
			sendResponse ("451 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return false;
		}

		if (rc == 0)
		{
			// reached end of file
			m_eof = true;
			return true;
		}

		m_filePosition += rc;
	}

	// send any pending data
	auto const rc = sendXferBuffer ();
	if (rc <= 0)
	{
		// error sending data
		if (rc < 0 && errno == EWOULDBLOCK)
			return false;

		sendResponse ("426 Connection broken during transfer\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	m_timestamp = std::time (nullptr);

	// we can try to read/send more data
	return true;
}

std::uint64_t FtpSession::monotonicUs ()
{
	auto const now = platform::steady_clock::now ().time_since_epoch ();
	return std::chrono::duration_cast<std::chrono::microseconds> (now).count ();
}

void FtpSession::statsBegin (bool const sending_)
{
	if (m_stats.active)
		platform::transferEnd ();

	m_stats         = {};
	m_stats.active  = true;
	m_stats.sending = sending_;
	platform::transferBegin ();
}

void FtpSession::statsWake ()
{
	if (!m_stats.active)
		return;

	if (m_stats.startUs == 0)
	{
		m_stats.startUs = monotonicUs ();
		m_stats.markUs  = m_stats.startUs;
	}

	++m_stats.wakes;
	statsInterval ();
}

void FtpSession::statsInterval ()
{
	if (!m_stats.active || STATS_INTERVAL <= 0ms || m_stats.markUs == 0)
		return;

	auto const now    = monotonicUs ();
	auto const wallUs = now - m_stats.markUs;
	if (wallUs < static_cast<std::uint64_t> (STATS_INTERVAL.count ()) * 1000)
		return;

	auto const bytes   = m_stats.bytes - m_stats.markBytes;
	auto const diskUs  = m_stats.diskUs - m_stats.markDiskUs;
	auto const netUs   = m_stats.netUs - m_stats.markNetUs;
	auto const blockUs = m_stats.blockUs - m_stats.markBlockUs;
	auto const busyUs  = diskUs + netUs + blockUs;
	auto const wallMs  = wallUs / 1000;

	info ("  slice %s bytes=%" PRIu64 " rate=%" PRIu64 "KB/s disk=%" PRIu64
	      "ms/%u net=%" PRIu64
	      "ms/%u block=%" PRIu64 "ms/%u wait=%" PRIu64 "ms wakes=%u over %" PRIu64
	      "ms\n",
	    m_stats.sending ? "RETR" : "STOR",
	    bytes,
	    wallMs ? bytes * 1000 / wallMs / 1024 : 0,
	    diskUs / 1000,
	    m_stats.diskOps - m_stats.markDiskOps,
	    netUs / 1000,
	    m_stats.netOps - m_stats.markNetOps,
	    blockUs / 1000,
	    m_stats.blockOps - m_stats.markBlockOps,
	    (wallUs > busyUs ? wallUs - busyUs : 0) / 1000,
	    m_stats.wakes - m_stats.markWakes,
	    wallMs);

	m_stats.markUs       = now;
	m_stats.markBytes    = m_stats.bytes;
	m_stats.markDiskUs   = m_stats.diskUs;
	m_stats.markNetUs    = m_stats.netUs;
	m_stats.markBlockUs  = m_stats.blockUs;
	m_stats.markDiskOps  = m_stats.diskOps;
	m_stats.markNetOps   = m_stats.netOps;
	m_stats.markBlockOps = m_stats.blockOps;
	m_stats.markWakes    = m_stats.wakes;
}

void FtpSession::statsReport ()
{
	if (!m_stats.active)
		return;

	platform::transferEnd ();

	// A client can disappear between RETR/STOR and the first data dispatch.
	// There is no meaningful wall clock in that case.
	if (m_stats.startUs == 0)
	{
		m_stats = {};
		return;
	}

	auto const wallUs = monotonicUs () - m_stats.startUs;
	auto const wallMs = wallUs / 1000;
	auto const busyUs = m_stats.diskUs + m_stats.netUs + m_stats.blockUs;

	info ("%s %" PRIu64 " bytes in %" PRIu64 " ms = %" PRIu64 " KB/s\n",
	    m_stats.sending ? "RETR" : "STOR",
	    m_stats.bytes,
	    wallMs,
	    wallMs ? m_stats.bytes * 1000 / wallMs / 1024 : 0);
	info (" disk=%" PRIu64 "ms/%u net=%" PRIu64 "ms/%u block=%" PRIu64
	      "ms/%u wait=%" PRIu64 "ms wakes=%u xfer=%d file=%d sock=%d stats=%lldms\n",
	    m_stats.diskUs / 1000,
	    m_stats.diskOps,
	    m_stats.netUs / 1000,
	    m_stats.netOps,
	    m_stats.blockUs / 1000,
	    m_stats.blockOps,
	    (wallUs > busyUs ? wallUs - busyUs : 0) / 1000,
	    m_stats.wakes,
	    FTPD_XFER_BUFFERSIZE,
	    FTPD_FILE_BUFFERSIZE,
	    FTPD_SOCK_BUFFERSIZE,
	    static_cast<long long> (STATS_INTERVAL.count ()));

	m_stats = {};
}

std::make_signed_t<std::size_t> FtpSession::readFile (IOBuffer &buffer_)
{
	auto const t0 = monotonicUs ();
	auto const rc = m_file.read (buffer_);
	m_stats.diskUs += monotonicUs () - t0;
	++m_stats.diskOps;
	return rc;
}

std::make_signed_t<std::size_t> FtpSession::writeFile (IOBuffer &buffer_)
{
	auto const t0 = monotonicUs ();
	auto const rc = m_file.write (buffer_);
	m_stats.diskUs += monotonicUs () - t0;
	++m_stats.diskOps;
	return rc;
}

std::make_signed_t<std::size_t> FtpSession::sendXferBuffer ()
{
	if (!m_stats.active)
		return m_dataSocket->write (m_xferBuffer);

	auto const t0 = monotonicUs ();
	auto const rc = m_dataSocket->write (m_xferBuffer);
	auto const dt = monotonicUs () - t0;

	if (rc > 0)
	{
		m_stats.bytes += rc;
		m_stats.netUs += dt;
		++m_stats.netOps;
	}
	else
	{
		m_stats.blockUs += dt;
		++m_stats.blockOps;
	}
	return rc;
}

std::make_signed_t<std::size_t> FtpSession::recvXferBuffer ()
{
	auto const t0 = monotonicUs ();
	auto const rc = m_dataSocket->read (m_xferBuffer);
	auto const dt = monotonicUs () - t0;

	// EOF is a successful receive operation; it simply moved no payload bytes.
	if (rc >= 0)
	{
		m_stats.bytes += rc;
		m_stats.netUs += dt;
		++m_stats.netOps;
	}
	else
	{
		m_stats.blockUs += dt;
		++m_stats.blockOps;
	}
	return rc;
}

bool FtpSession::flushStoreBuffer ()
{
	auto const rc = writeFile (m_xferBuffer);
	if (rc <= 0)
	{
		// error writing data
		sendResponse ("426 %s\r\n", rc < 0 ? std::strerror (errno) : "Failed to write data");
		setState (State::COMMAND, true, true);
		return false;
	}

	// a short write leaves the remainder in the buffer for the next pass
	m_filePosition += rc;
	return true;
}

bool FtpSession::storeTransfer ()
{
	// Fill the buffer before touching the card. A recv() only yields about
	// SO_RCVBUF bytes at a time, so writing each one straight through would turn
	// a single FILE_BUFFERSIZE write into a handful of small ones. stdio's
	// setvbuf buffer used to do this coalescing; now that reads and writes go
	// directly against the IOBuffer, the buffer does it explicitly.
	if (!m_eof && m_xferBuffer.freeSize () != 0)
	{
		auto const rc = recvXferBuffer ();
		if (rc < 0)
		{
			// failed to read data
			if (errno == EWOULDBLOCK)
				return false;

			sendResponse ("451 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return false;
		}

		if (rc == 0)
			// peer is done sending; fall through so the tail reaches the card
			m_eof = true;
		else
			m_timestamp = std::time (nullptr);
	}

	// Write once the buffer is full, or once nothing more is coming. These two
	// conditions are also what keeps the buffer from needing coalesce(): a short
	// write can only leave a partly-consumed buffer when it was full (so
	// freeSize() is still 0 and the next pass writes rather than receives) or
	// when m_eof is set (so the receive gate above is closed for good). Relaxing
	// either gate reintroduces the case where usedArea() has drifted forward with
	// free space stranded behind it.
	if (!m_xferBuffer.empty () && (m_eof || m_xferBuffer.freeSize () == 0))
		return flushStoreBuffer ();

	if (m_eof)
	{
		// buffer is drained and the peer is gone: the file is complete
		sendResponse ("226 OK\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////
void FtpSession::ABOR (char const *args_)
{
	(void)args_;

	if (m_state == State::COMMAND)
	{
		sendResponse ("225 No transfer to abort\r\n");
		return;
	}

	// abort the transfer
	sendResponse ("225 Aborted\r\n");
	sendResponse ("425 Transfer aborted\r\n");
	setState (State::COMMAND, true, true);
}

void FtpSession::ALLO (char const *args_)
{
	(void)args_;

	sendResponse ("202 Superfluous command\r\n");
	setState (State::COMMAND, false, false);
}

void FtpSession::APPE (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the file in append mode
	xferFile (args_, XferFileMode::APPE);
}

void FtpSession::CDUP (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	if (!changeDir (".."))
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("200 OK\r\n");
}

void FtpSession::CWD (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	if (!changeDir (args_))
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("200 OK\r\n");
}

void FtpSession::DELE (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// build the path to remove
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// unlink the path
	if (::unlink (path.c_str ()) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("250 OK\r\n");
}
void FtpSession::FEAT (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);
	sendResponse ("211-\r\n"
	              " MDTM\r\n"
	              " MLST Type%s;Size%s;Modify%s;Perm%s;UNIX.mode%s;\r\n"
	              " PASV\r\n"
	              " SIZE\r\n"
	              " TVFS\r\n"
	              " UTF8\r\n"
	              "\r\n"
	              "211 End\r\n",
	    m_mlstType ? "*" : "",
	    m_mlstSize ? "*" : "",
	    m_mlstModify ? "*" : "",
	    m_mlstPerm ? "*" : "",
	    m_mlstUnixMode ? "*" : "");
}

void FtpSession::HELP (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);
	sendResponse ("214-\r\n"
	              "The following commands are recognized\r\n"
	              " ABOR ALLO APPE CDUP CWD DELE FEAT HELP LIST MDTM MKD MLSD MLST MODE\r\n"
	              " NLST NOOP OPTS PASS PASV PORT PWD QUIT REST RETR RMD RNFR RNTO SIZE\r\n"
	              " STAT STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD\r\n"
	              "214 End\r\n");
}

void FtpSession::LIST (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the path in LIST mode
	xferDir (args_, XferDirMode::LIST, true);
}

void FtpSession::MDTM (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	sendResponse ("502 Command not implemented\r\n");
}

void FtpSession::MKD (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// build the path to create
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// create the directory
	if (::mkdir (path.c_str (), 0755) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("250 OK\r\n");
}

void FtpSession::MLSD (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the path in MLSD mode
	xferDir (args_, XferDirMode::MLSD, false);
}

void FtpSession::MLST (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the path in MLST mode
	xferDir (args_, XferDirMode::MLST, false);
}

void FtpSession::MODE (char const *args_)
{
	setState (State::COMMAND, false, false);

	// S (stream) mode is the only mode; Z (deflate) was removed from this port
	// because deflate on a 268 MHz ARM11 is slower than the SD card it feeds.
	if (compare (args_, "S") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	sendResponse ("504 Unavailable\r\n");
}

void FtpSession::NLST (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// Wildcards are not supported: newlib ships glob.h but no glob(), and the
	// FreeBSD glob + locale collation this port used to vendor for that is gone.
	// GUI clients send a plain LIST/NLST and never notice; a command-line
	// `mget *.sav` gets this instead.
	if (std::strchr (args_, '*'))
	{
		sendResponse ("451 Glob unsupported\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	xferDir (args_, XferDirMode::NLST, false);
}

void FtpSession::NOOP (char const *args_)
{
	(void)args_;

	sendResponse ("200 OK\r\n");
}

void FtpSession::OPTS (char const *args_)
{
	setState (State::COMMAND, false, false);

	// check UTF8 options
	if (compare (args_, "UTF8") == 0 || compare (args_, "UTF8 ON") == 0 ||
	    compare (args_, "UTF8 NLST") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	// check MLST options
	if (::strncasecmp (args_, "MLST ", 5) == 0)
	{
		m_mlstType     = false;
		m_mlstSize     = false;
		m_mlstModify   = false;
		m_mlstPerm     = false;
		m_mlstUnixMode = false;

		auto p = args_ + 5;
		while (*p)
		{
			auto const match = [] (auto const &name_, auto const &arg_) {
				return ::strncasecmp (name_, arg_, std::strlen (name_)) == 0;
			};

			if (match ("Type;", p))
				m_mlstType = true;
			else if (match ("Size;", p))
				m_mlstSize = true;
			else if (match ("Modify;", p))
				m_mlstModify = true;
			else if (match ("Perm;", p))
				m_mlstPerm = true;
			else if (match ("UNIX.mode;", p))
				m_mlstUnixMode = true;

			p = std::strchr (p, ';');
			if (!p)
				break;

			++p;
		}

		sendResponse ("200 MLST OPTS%s%s%s%s%s%s\r\n",
		    m_mlstType || m_mlstSize || m_mlstModify || m_mlstPerm || m_mlstUnixMode ? " " : "",
		    m_mlstType ? "Type;" : "",
		    m_mlstSize ? "Size;" : "",
		    m_mlstModify ? "Modify;" : "",
		    m_mlstPerm ? "Perm;" : "",
		    m_mlstUnixMode ? "UNIX.mode;" : "");
		return;
	}

	sendResponse ("504 %s\r\n", std::strerror (EINVAL));
}

void FtpSession::PASS (char const *args_)
{
	setState (State::COMMAND, false, false);

	m_authorizedPass = false;

	auto const &user = m_config.user ();
	auto const &pass = m_config.pass ();

	if (!user.empty () && !m_authorizedUser)
	{
		sendResponse ("430 User not authorized\r\n");
		return;
	}

	if (pass.empty () || pass == args_)
	{
		m_authorizedPass = true;
		sendResponse ("230 OK\r\n");
		return;
	}

	sendResponse ("430 Invalid password\r\n");
}

void FtpSession::PASV (char const *args_)
{
	(void)args_;

	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// reset state
	setState (State::COMMAND, true, true);
	m_pasv = false;
	m_port = false;

	// create a socket to listen on
	auto pasv = Socket::create ();
	m_pasvSocket = std::move (pasv);
	if (!m_pasvSocket)
	{
		sendResponse ("451 Failed to create listening socket\r\n");
		return;
	}

	// set the socket options
	m_pasvSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_pasvSocket->setSendBufferSize (SOCK_BUFFERSIZE);

	// create an address to bind
	sockaddr_in addr = m_commandSocket->sockName ();
#ifdef __3DS__
	static std::uint16_t ephemeralPort = 5001;
	if (ephemeralPort > 10000)
		ephemeralPort = 5001;
	addr.sin_port = htons (ephemeralPort++);
#else
	addr.sin_port = htons (0);
#endif

	// bind to the address
	if (!m_pasvSocket->bind (addr))
	{
		closePasv ();
		sendResponse ("451 Failed to bind address\r\n");
		return;
	}

	// listen on the socket
	if (!m_pasvSocket->listen (1))
	{
		closePasv ();
		sendResponse ("451 Failed to listen on socket\r\n");
		return;
	}

	// we are now listening on the socket
	auto const &sockName = m_pasvSocket->sockName ();
	std::string name     = sockName.name ();
	auto const port      = sockName.port ();
	info ("Listening on [%s]:%u\n", name.c_str (), port);

	// send the address in the ftp format
	for (auto &c : name)
	{
		if (c == '.')
			c = ',';
	}

	m_pasv = true;
	sendResponse (
	    "227 Entering Passive Mode (%s,%u,%u).\r\n", name.c_str (), port >> 8, port & 0xFF);
}

void FtpSession::PORT (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// reset state
	setState (State::COMMAND, true, true);
	m_pasv = false;
	m_port = false;

	std::string addrString = args_;

	// convert a,b,c,d,e,f with a.b.c.d\0e.f
	unsigned commas        = 0;
	char const *portString = nullptr;
	for (auto &p : addrString)
	{
		if (p == ',')
		{
			if (commas++ != 3)
				p = '.';
			else
			{
				p          = '\0';
				portString = &p + 1;
			}
		}
	}

	// check for the expected number of fields
	if (commas != 5)
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	sockaddr_in addr{};

	// parse the address
	if (!inet_aton (addrString.data (), &addr.sin_addr))
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	// parse the port
	int val            = 0;
	std::uint16_t port = 0;
	for (auto p = portString; *p; ++p)
	{
		if (!std::isdigit (*p))
		{
			if (p == portString || *p != '.' || val > 0xFF)
			{
				sendResponse ("501 %s\r\n", std::strerror (EINVAL));
				return;
			}

			port <<= 8;
			port += val;
			val = 0;
		}
		else
		{
			val *= 10;
			val += *p - '0';
		}
	}

	if (val > 0xFF || port > 0xFF)
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	port <<= 8;
	port += val;

	addr.sin_family = AF_INET;
	addr.sin_port   = htons (port);

	// we are ready to connect to the client
	m_portAddr = addr;
	m_port     = true;
	sendResponse ("200 OK\r\n");
}

void FtpSession::PWD (char const *args_)
{
	(void)args_;

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	auto const path = encodePath (m_cwd);

	std::string response = "257 \"";
	response += encodePath (m_cwd, true);
	response += "\"\r\n";

	sendResponse (response);
}

void FtpSession::QUIT (char const *args_)
{
	(void)args_;

	sendResponse ("221 Disconnecting\r\n");
	closeCommand ();
}

void FtpSession::REST (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// parse the offset
	std::uint64_t pos = 0;
	for (auto p = args_; *p; ++p)
	{
		if (!std::isdigit (*p) || UINT64_MAX / 10 < pos)
		{
			sendResponse ("504 %s\r\n", std::strerror (errno));
			return;
		}

		pos *= 10;

		if (UINT64_MAX - (*p - '0') < pos)
		{
			sendResponse ("504 %s\r\n", std::strerror (errno));
			return;
		}

		pos += (*p - '0');
	}

	// set the restart offset
	m_restartPosition = pos;
	sendResponse ("350 OK\r\n");
}

void FtpSession::RETR (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the file to retrieve
	xferFile (args_, XferFileMode::RETR);
}

void FtpSession::RMD (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// build the path to remove
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// remove the directory
	if (::rmdir (path.c_str ()) != 0)
	{
		sendResponse ("550 %d %s\r\n", __LINE__, std::strerror (errno));
		return;
	}

	sendResponse ("250 OK\r\n");
}

void FtpSession::RNFR (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// build the path to rename from
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// make sure the path exists
	stat_t st;
	if (tzLStat (path.c_str (), &st) != 0)
	{
		sendResponse ("450 %s\r\n", std::strerror (errno));
		return;
	}

	// we are ready for RNTO
	m_rename = path;
	sendResponse ("350 OK\r\n");
}

void FtpSession::RNTO (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// make sure the previous command was RNFR
	if (m_rename.empty ())
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		return;
	}

	// build the path to rename to
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		m_rename.clear ();
		sendResponse ("554 %s\r\n", std::strerror (errno));
		return;
	}

	// rename the file
	if (::rename (m_rename.c_str (), path.c_str ()) != 0)
	{
		m_rename.clear ();
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	// clear the rename state
	m_rename.clear ();

	sendResponse ("250 OK\r\n");
}

void FtpSession::SIZE (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!authorized ())
	{
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// build the path to stat
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// stat the path
	stat_t st;
	if (tzStat (path.c_str (), &st) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	if (!S_ISREG (st.st_mode))
	{
		sendResponse ("550 Not a file\r\n");
		return;
	}

	sendResponse ("213 %" PRIu64 "\r\n", static_cast<std::uint64_t> (st.st_size));
}

void FtpSession::STAT (char const *args_)
{
	if (m_state == State::DATA_CONNECT)
	{
		sendResponse ("211-FTP server status\r\n"
		              " Waiting for data connection\r\n"
		              "211 End\r\n");
		return;
	}

	if (m_state == State::DATA_TRANSFER)
	{
		sendResponse ("211-FTP server status\r\n"
		              " Transferred %" PRIu64 " bytes\r\n"
		              "211 End\r\n",
		    m_filePosition);
		return;
	}

	if (std::strlen (args_) == 0)
	{
		// TODO keep track of start time
		auto const uptime =
		    std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ()) -
		    FtpServer::startTime ();
		unsigned const hours   = uptime / 3600;
		unsigned const minutes = (uptime / 60) % 60;
		unsigned const seconds = uptime % 60;

		sendResponse ("211-FTP server status\r\n"
		              " Uptime: %02u:%02u:%02u\r\n"
		              "211 End\r\n",
		    hours,
		    minutes,
		    seconds);
		return;
	}

	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	xferDir (args_, XferDirMode::STAT, false);
}

void FtpSession::STOR (char const *args_)
{
	if (!authorized ())
	{
		setState (State::COMMAND, false, false);
		sendResponse ("530 Not logged in\r\n");
		return;
	}

	// open the file to store
	xferFile (args_, XferFileMode::STOR);
}

void FtpSession::STOU (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);
	sendResponse ("502 Command not implemented\r\n");
}

void FtpSession::STRU (char const *args_)
{
	setState (State::COMMAND, false, false);

	// we only support F (no structure) mode
	if (compare (args_, "F") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	sendResponse ("504 Unavailable\r\n");
}

void FtpSession::SYST (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);
	sendResponse ("215 UNIX Type: L8\r\n");
}

void FtpSession::TYPE (char const *args_)
{
	(void)args_;

	setState (State::COMMAND, false, false);

	// we always transfer in binary mode
	sendResponse ("200 OK\r\n");
}

void FtpSession::USER (char const *args_)
{
	setState (State::COMMAND, false, false);

	m_authorizedUser = false;

	auto const &user = m_config.user ();
	auto const &pass = m_config.pass ();

	if (user.empty () || user == args_)
	{
		m_authorizedUser = true;
		if (pass.empty ())
		{
			sendResponse ("230 OK\r\n");
			return;
		}

		sendResponse ("331 Need password\r\n");
		return;
	}

	sendResponse ("430 Invalid user\r\n");
}

// clang-format off
std::vector<std::pair<std::string_view, void (FtpSession::*) (char const *)>> const
    FtpSession::handlers =
{
	{"ABOR", &FtpSession::ABOR}, 
	{"ALLO", &FtpSession::ALLO}, 
	{"APPE", &FtpSession::APPE}, 
	{"CDUP", &FtpSession::CDUP}, 
	{"CWD",  &FtpSession::CWD},
	{"DELE", &FtpSession::DELE}, 
	{"FEAT", &FtpSession::FEAT}, 
	{"HELP", &FtpSession::HELP}, 
	{"LIST", &FtpSession::LIST}, 
	{"MDTM", &FtpSession::MDTM}, 
	{"MKD",  &FtpSession::MKD},
	{"MLSD", &FtpSession::MLSD}, 
	{"MLST", &FtpSession::MLST}, 
	{"MODE", &FtpSession::MODE}, 
	{"NLST", &FtpSession::NLST}, 
	{"NOOP", &FtpSession::NOOP}, 
	{"OPTS", &FtpSession::OPTS}, 
	{"PASS", &FtpSession::PASS}, 
	{"PASV", &FtpSession::PASV}, 
	{"PORT", &FtpSession::PORT}, 
	{"PWD",  &FtpSession::PWD},
	{"QUIT", &FtpSession::QUIT}, 
	{"REST", &FtpSession::REST}, 
	{"RETR", &FtpSession::RETR}, 
	{"RMD",  &FtpSession::RMD},
	{"RNFR", &FtpSession::RNFR}, 
	{"RNTO", &FtpSession::RNTO}, 
	{"SIZE", &FtpSession::SIZE}, 
	{"STAT", &FtpSession::STAT}, 
	{"STOR", &FtpSession::STOR}, 
	{"STOU", &FtpSession::STOU}, 
	{"STRU", &FtpSession::STRU}, 
	{"SYST", &FtpSession::SYST}, 
	{"TYPE", &FtpSession::TYPE}, 
	{"USER", &FtpSession::USER}, 
	{"XCUP", &FtpSession::CDUP},
	{"XCWD", &FtpSession::CWD},
	{"XMKD", &FtpSession::MKD},
	{"XPWD", &FtpSession::PWD},
	{"XRMD", &FtpSession::RMD},
};
// clang-format on
