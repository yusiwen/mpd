/*
 * Copyright (C) 2003-2015 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPD_BUFFERED_READER_HXX
#define MPD_BUFFERED_READER_HXX

#include "check.h"
#include "Compiler.h"
#include "util/DynamicFifoBuffer.hxx"

#include <stddef.h>

class Reader;

class BufferedReader {
	static constexpr size_t MAX_SIZE = 512 * 1024;

	Reader &reader;

	DynamicFifoBuffer<char> buffer;

	bool eof;

	unsigned line_number;

public:
	BufferedReader(Reader &_reader)
		:reader(_reader), buffer(4096), eof(false),
		 line_number(0) {}

	bool Fill(bool need_more);

	gcc_pure
	WritableBuffer<void> Read() const {
		return buffer.Read().ToVoid();
	}

	void Consume(size_t n) {
		buffer.Consume(n);
	}

	char *ReadLine();

	unsigned GetLineNumber() const {
		return line_number;
	}
};

#endif
