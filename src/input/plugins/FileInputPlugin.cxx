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

#include "config.h" /* must be first for large file support */
#include "FileInputPlugin.hxx"
#include "../InputStream.hxx"
#include "../InputPlugin.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"
#include "fs/Path.hxx"
#include "fs/FileInfo.hxx"
#include "fs/io/FileReader.hxx"
#include "system/FileDescriptor.hxx"

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static constexpr Domain file_domain("file");

class FileInputStream final : public InputStream {
	FileReader reader;

public:
	FileInputStream(const char *path, FileReader &&_reader, off_t _size,
			Mutex &_mutex, Cond &_cond)
		:InputStream(path, _mutex, _cond),
		 reader(std::move(_reader)) {
		size = _size;
		seekable = true;
		SetReady();
	}

	/* virtual methods from InputStream */

	bool IsEOF() override {
		return GetOffset() >= GetSize();
	}

	size_t Read(void *ptr, size_t size, Error &error) override;
	bool Seek(offset_type offset, Error &error) override;
};

InputStream *
OpenFileInputStream(Path path,
		    Mutex &mutex, Cond &cond,
		    Error &error)
try {
	FileReader reader(path);

	const FileInfo info = reader.GetFileInfo();

	if (!info.IsRegular()) {
		error.Format(file_domain, "Not a regular file: %s",
			     path.c_str());
		return nullptr;
	}

#ifdef POSIX_FADV_SEQUENTIAL
	posix_fadvise(reader.GetFD().Get(), (off_t)0, info.GetSize(),
		      POSIX_FADV_SEQUENTIAL);
#endif

	return new FileInputStream(path.ToUTF8().c_str(),
				   std::move(reader), info.GetSize(),
				   mutex, cond);
} catch (const std::exception &e) {
	error.Set(e);
	return nullptr;
}

static InputStream *
input_file_open(gcc_unused const char *filename,
		gcc_unused Mutex &mutex, gcc_unused Cond &cond,
		gcc_unused Error &error)
{
	/* dummy method; use OpenFileInputStream() instead */

	return nullptr;
}

bool
FileInputStream::Seek(offset_type new_offset, Error &error)
try {
	reader.Seek((off_t)new_offset);
	offset = new_offset;
	return true;
} catch (const std::exception &e) {
	error.Set(e);
	return false;
}

size_t
FileInputStream::Read(void *ptr, size_t read_size, Error &error)
try {
	size_t nbytes = reader.Read(ptr, read_size);
	offset += nbytes;
	return nbytes;
} catch (const std::exception &e) {
	error.Set(e);
	return 0;
}

const InputPlugin input_plugin_file = {
	"file",
	nullptr,
	nullptr,
	input_file_open,
};
