// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2020 Michael Theall
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

#include "ioBuffer.h"

#include <cassert>
#include <cstring>

///////////////////////////////////////////////////////////////////////////
IOBuffer::~IOBuffer () = default;

IOBuffer::IOBuffer (std::size_t const size_)
    : m_buffer (std::make_unique<char[]> (size_)), m_size (size_)
{
	assert (size_ > 0);
}

char *IOBuffer::freeArea () const
{
	assert (m_end < m_size);
	return &m_buffer[m_end];
}

std::size_t IOBuffer::freeSize () const
{
	assert (m_size >= m_end);
	return m_size - m_end;
}

void IOBuffer::markFree (std::size_t size_)
{
	assert (m_end >= m_start);
	assert (m_end - m_start >= size_);
	m_start += size_;

	// reset back to beginning
	if (m_start == m_end)
	{
		m_start = 0;
		m_end   = 0;
	}
}

char *IOBuffer::usedArea () const
{
	assert (m_start < m_size);
	return &m_buffer[m_start];
}

std::size_t IOBuffer::usedSize () const
{
	assert (m_end >= m_start);
	return m_end - m_start;
}

void IOBuffer::markUsed (std::size_t size_)
{
	assert (m_size >= m_end);
	assert (m_size - m_end >= size_);
	m_end += size_;
}

bool IOBuffer::empty () const
{
	assert (m_end >= m_start);
	return (m_end - m_start) == 0;
}

std::size_t IOBuffer::capacity () const
{
	return m_size;
}

void IOBuffer::clear ()
{
	m_start = 0;
	m_end   = 0;
}

void IOBuffer::coalesce ()
{
	assert (m_size >= m_end);
	assert (m_end >= m_start);

	auto const size = m_end - m_start;
	if (size != 0)
		std::memmove (&m_buffer[0], &m_buffer[m_start], size);

	m_end -= m_start;
	m_start = 0;
}
