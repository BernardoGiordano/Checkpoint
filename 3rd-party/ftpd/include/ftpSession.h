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

#include "fs.h"
#include "ftpConfig.h"
#include "ioBuffer.h"
#include "ftpPlatform.h"
#include "socket.h"

#if __has_include(<glob.h>)
#include <glob.h>
#define FTPD_HAS_GLOB 1
#else
#define FTPD_HAS_GLOB 0
#endif

#include <zlib.h>

#include <sys/stat.h>
using stat_t = struct stat;

#include <chrono>
#include <ctime>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

class FtpSession;
using UniqueFtpSession = std::unique_ptr<FtpSession>;

/// \brief FTP session
class FtpSession
{
public:
	~FtpSession ();

	/// \brief Whether session sockets are all inactive
	bool dead ();

	/// \brief Create session
	/// \param config_ FTP config
	/// \param commandSocket_ Command socket
	static UniqueFtpSession create (FtpConfig &config_, UniqueSocket commandSocket_);

	/// \brief Poll for activity
	/// \param sessions_ Sessions to poll
	static bool poll (std::vector<UniqueFtpSession> const &sessions_);

private:
	/// \brief Command buffer size
	constexpr static auto COMMAND_BUFFERSIZE = 4096;

	/// \brief Response buffer size
	constexpr static auto RESPONSE_BUFFERSIZE = 32768;

// The three transfer sizes are ftpd's, and ftpd's numbers are the ones that
// measure fast on real hardware — do not "tune" them without a before/after on
// the console. They can be overridden from the command line for an A/B, e.g.
//   make 3ds FTPD_XFER_BUFFERSIZE=32768
#ifndef FTPD_XFER_BUFFERSIZE
#define FTPD_XFER_BUFFERSIZE 65536
#endif

#ifndef FTPD_FILE_BUFFERSIZE
#define FTPD_FILE_BUFFERSIZE (4 * FTPD_XFER_BUFFERSIZE)
#endif

// On 3DS every socket's SO_RCVBUF + SO_SNDBUF is carved out of the single 1 MiB
// soc:u pool Checkpoint allocates in util.cpp, so this cannot be raised without
// raising SOC_BUFFERSIZE too — at 65536 a handful of sockets exhaust the pool
// and accept() starts returning ENOMEM.
#ifndef FTPD_SOCK_BUFFERSIZE
#ifdef __3DS__
#define FTPD_SOCK_BUFFERSIZE 32768
#else
#define FTPD_SOCK_BUFFERSIZE FTPD_XFER_BUFFERSIZE
#endif
#endif

	/// \brief Transfer buffersize
	constexpr static auto XFER_BUFFERSIZE = FTPD_XFER_BUFFERSIZE;

	/// \brief File buffersize
	constexpr static auto FILE_BUFFERSIZE = FTPD_FILE_BUFFERSIZE;

	/// \brief Socket buffer size
	constexpr static auto SOCK_BUFFERSIZE = FTPD_SOCK_BUFFERSIZE;

	/// \brief Session state
	enum class State
	{
		COMMAND,
		DATA_CONNECT,
		DATA_TRANSFER,
	};

	/// \brief Transfer file mode
	enum class XferFileMode
	{
		RETR,
		STOR,
		APPE,
	};

	/// \brief Transfer directory mode
	enum class XferDirMode
	{
		LIST,
		MLSD,
		MLST,
		NLST,
		STAT,
	};

	/// \brief Parameterized constructor
	/// \param config_ FTP config
	/// \param commandSocket_ Command socket
	FtpSession (FtpConfig &config_, UniqueSocket commandSocket_);

	/// \brief Whether session is authorized
	bool authorized () const;

	/// \brief Set session state
	/// \param state_ State to set
	/// \param closePasv_ Whether to close listening socket
	/// \param closeData_ Whether to close data socket
	void setState (State state_, bool closePasv_, bool closeData_);

	/// \brief Close socket
	/// \param socket_ Socket to close
	void closeSocket (SharedSocket &socket_);
	/// \brief Close command socket
	void closeCommand ();
	/// \brief Close passive socket
	void closePasv ();
	/// \brief Close data socket
	void closeData ();

	/// \brief Change working directory
	bool changeDir (char const *args_);

	/// \brief Accept connection as data socket
	bool dataAccept ();

	/// \brief Connect data socket
	bool dataConnect ();

	/// \brief Perform stat and apply tz offset to mtime
	/// \param path_ Path to stat
	/// \param st_ Output stat
	int tzStat (char const *const path_, stat_t *st_);

	/// \brief Perform lstat and apply tz offset to mtime
	/// \param path_ Path to lstat
	/// \param st_ Output stat
	int tzLStat (char const *const path_, stat_t *st_);

	/// \brief Fill directory entry
	/// \param st_ Entry status
	/// \param path_ Path name
	/// \param type_ MLST type
	int fillDirent (stat_t const &st_, std::string_view path_, char const *type_ = nullptr);

	/// \brief Fill directory entry
	/// \param path_ Path name
	/// \param type_ MLST type
	int fillDirent (std::string const &path_, char const *type_ = nullptr);

	/// \brief Transfer file
	/// \param args_ Command arguments
	/// \param mode_ Transfer file mode
	void xferFile (char const *args_, XferFileMode mode_);

	/// \brief Transfer directory
	/// \param args_ Command arguments
	/// \param mode_ Transfer directory mode
	/// \param workaround_ Workaround broken clients who use LIST -a/-l
	void xferDir (char const *args_, XferDirMode mode_, bool workaround_);

	/// \brief Read command
	/// \param events_ Poll events
	void readCommand (int events_);

	/// \brief Write response
	void writeResponse ();

	/// \brief Send response
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 2, 3))) void sendResponse (char const *fmt_, ...);

	/// \brief Send response
	/// \param response_ Response message
	void sendResponse (std::string_view response_);

	/// \brief Deflate buffer
	/// \param flush_ Whether to flush
	bool deflateBuffer (bool flush_);

	/// \brief Inflate buffer
	bool inflateBuffer ();

	/// \brief Transfer function
	bool (FtpSession::*m_transfer) () = nullptr;

	/// \brief Transfer directory list
	bool listTransfer ();

#if FTPD_HAS_GLOB
	/// \brief Transfer glob list
	bool globTransfer ();
#endif

	/// \brief Transfer download
	bool retrieveTransfer ();

	/// \brief Transfer upload
	bool storeTransfer ();

#ifndef __NDS__
	/// \brief Mutex
	platform::Mutex m_lock;
#endif

	/// \brief FTP config
	FtpConfig &m_config;

	/// \brief Command socket
	SharedSocket m_commandSocket;

	/// \brief Data listen socker
	UniqueSocket m_pasvSocket;

	/// \brief Data socket
	SharedSocket m_dataSocket;

	/// \brief Sockets pending close
	std::vector<SharedSocket> m_pendingCloseSocket;

	/// \brief Command buffer
	IOBuffer m_commandBuffer;

	/// \brief Response buffer
	IOBuffer m_responseBuffer;

	/// \brief Transfer buffer
	IOBuffer m_xferBuffer;
	/// \brief z-stream buffer
	IOBuffer m_zStreamBuffer;

	/// \brief Address from last PORT command
	SockAddr m_portAddr;

	/// \brief Current working directory
	std::string m_cwd = "/";

	/// \brief List working directory
	std::string m_lwd;

	/// \brief Path from RNFR command
	std::string m_rename;

	/// \brief Current work item
	std::string m_workItem;

	/// \brief Position from REST command
	std::uint64_t m_restartPosition = 0;

	/// \brief Current file position
	std::uint64_t m_filePosition = 0;
	/// \brief Current z-stream position
	std::uint64_t m_zStreamPosition = 0;

	/// \brief File size of current transfer
	std::uint64_t m_fileSize = 0;

	/// \brief Session state
	State m_state = State::COMMAND;

	/// \brief File being transferred
	fs::File m_file;

	/// \brief Directory being transferred
	fs::Dir m_dir;

#if FTPD_HAS_GLOB
	/// \brief Glob wrappre
	class Glob
	{
	public:
		~Glob () noexcept;
		Glob () noexcept;

		/// \brief Perform glob
		/// \param pattern_ Glob pattern
		bool glob (char const *pattern_) noexcept;

		/// \brief Get next glob result
		/// \note returns nullptr when no more entries exist
		char const *next () noexcept;

	private:
		/// \brief Clear glob
		void clear () noexcept;

		/// \brief Glob result
		std::optional<glob_t> m_glob = std::nullopt;

		/// \brief Result counter
		unsigned m_offset = 0;
	};

	/// \brief Glob
	Glob m_glob;
#endif

	/// \brief Directory transfer mode
	XferDirMode m_xferDirMode;

	/// \brief z-stream
	std::unique_ptr<z_stream, int (*) (z_streamp)> m_zStream;

	/// \brief Last activity timestamp
	time_t m_timestamp;

	/// \brief Whether user has been authorized
	bool m_authorizedUser : 1;
	/// \brief Whether password has been authorized
	bool m_authorizedPass : 1;
	/// \brief Whether previous command was PASV
	bool m_pasv : 1;
	/// \brief Whether previous command was PORT
	bool m_port : 1;
	/// \brief Whether receiving data
	bool m_recv : 1;
	/// \brief Whether sending data
	bool m_send : 1;
	/// \brief Whether urgent (out-of-band) data is on the way
	bool m_urgent : 1;
	/// \brief Whether using Z (deflate) mode
	bool m_deflate : 1;
	/// \brief Whether we finished processing z-stream
	bool m_zFlushed : 1;
	/// \brief Whether we finished reading data
	bool m_eof : 1;

	/// \brief Whether MLST type fact is enabled
	bool m_mlstType : 1;
	/// \brief Whether MLST size fact is enabled
	bool m_mlstSize : 1;
	/// \brief Whether MLST modify fact is enabled
	bool m_mlstModify : 1;
	/// \brief Whether MLST perm fact is enabled
	bool m_mlstPerm : 1;
	/// \brief Whether MLST unix.mode fact is enabled
	bool m_mlstUnixMode : 1;

	/// \brief Whether emulating /dev/zero
	bool m_devZero : 1;

	/// \brief Abort a transfer
	/// \param args_ Command arguments
	void ABOR (char const *args_);

	/// \brief Allocate space
	/// \param args_ Command arguments
	void ALLO (char const *args_);

	/// \brief Append data to a file
	/// \param args_ Command arguments
	void APPE (char const *args_);

	/// \brief CWD to parent directory
	/// \param args_ Command arguments
	void CDUP (char const *args_);

	/// \brief Change working directory
	/// \param args_ Command arguments
	void CWD (char const *args_);

	/// \brief Delete a file
	/// \param args_ Command arguments
	void DELE (char const *args_);

	/// \brief List server features
	/// \param args_ Command arguments
	void FEAT (char const *args_);

	/// \brief Print server help
	/// \param args_ Command arguments
	void HELP (char const *args_);

	/// \brief List directory
	/// \param args_ Command arguments
	void LIST (char const *args_);

	/// \brief Last modification time
	/// \param args_ Command arguments
	void MDTM (char const *args_);

	/// \brief Create a directory
	/// \param args_ Command arguments
	void MKD (char const *args_);

	/// \brief Machine list directory
	/// \param args_ Command arguments
	void MLSD (char const *args_);

	/// \brief Machine list
	/// \param args_ Command arguments
	void MLST (char const *args_);

	/// \brief Set transfer mode
	/// \param args_ Command arguments
	void MODE (char const *args_);

	/// \brief Name list
	/// \param args_ Command arguments
	void NLST (char const *args_);

	/// \brief No-op
	/// \param args_ Command arguments
	void NOOP (char const *args_);

	/// \brief Set server options
	/// \param args_ Command arguments
	void OPTS (char const *args_);

	/// \brief Password
	/// \param args_ Command arguments
	void PASS (char const *args_);

	/// \brief Request an address to connect to for data transfers
	/// \param args_ Command arguments
	void PASV (char const *args_);

	/// \brief Provide an address to connect to for data transfers
	/// \param args_ Command arguments
	void PORT (char const *args_);

	/// \brief Print working directory
	/// \param args_ Command arguments
	void PWD (char const *args_);

	/// \brief Terminate session
	/// \param args_ Command arguments
	void QUIT (char const *args_);

	/// \brief Restart a file transfer
	/// \param args_ Command arguments
	void REST (char const *args_);

	/// \brief Retrieve a file
	/// \param args_ Command arguments
	/// \note Requires a PASV or PORT connection
	void RETR (char const *args_);

	/// \brief Remove a directory
	/// \param args_ Command arguments
	void RMD (char const *args_);

	/// \brief Rename from
	/// \param args_ Command arguments
	void RNFR (char const *args_);

	/// \brief Rename to
	/// \param args_ Command arguments
	void RNTO (char const *args_);

	/// \brief Site command
	/// \param args_ Command arguments
	void SITE (char const *args_);

	/// \brief Get file size
	/// \param args_ Command arguments
	void SIZE (char const *args_);

	/// \brief Get status
	/// \param args_ Command arguments
	/// \note If no argument is supplied, and a transfer is occurring, get the current transfer
	/// status. If no argument is supplied, and no transfer is occurring, get the server status. If
	/// an argument is supplied, this is equivalent to LIST, except the data is sent over the
	/// command socket.
	void STAT (char const *args_);

	/// \brief Store a file
	/// \param args_ Command arguments
	void STOR (char const *args_);

	/// \brief Store a unique file
	/// \param args_ Command arguments
	void STOU (char const *args_);

	/// \brief Set file structure
	/// \param args_ Command arguments
	void STRU (char const *args_);

	/// \brief Identify system
	/// \param args_ Command arguments
	void SYST (char const *args_);

	/// \brief Set representation type
	/// \param args_ Command arguments
	void TYPE (char const *args_);

	/// \brief User name
	/// \param args_ Command arguments
	void USER (char const *args_);

	/// \brief Map of command handlers
	static std::vector<std::pair<std::string_view, void (FtpSession::*) (char const *)>> const
	    handlers;
};
