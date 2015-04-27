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

#include "config.h"
#include "FileOutputStream.hxx"
#include "fs/FileSystem.hxx"
#include "util/Error.hxx"

FileOutputStream *
FileOutputStream::Create(Path path, Error &error)
{
	FileOutputStream *f = new FileOutputStream(path, error);
	if (!f->IsDefined()) {
		delete f;
		f = nullptr;
	}

	return f;
}

#ifdef WIN32

FileOutputStream::FileOutputStream(Path _path, Error &error)
	:BaseFileOutputStream(_path)
{
	SetHandle(CreateFile(_path.c_str(), GENERIC_WRITE, 0, nullptr,
			     CREATE_ALWAYS,
			     FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,
			     nullptr));
	if (!IsDefined())
		error.FormatLastError("Failed to create %s",
				      GetPath().ToUTF8().c_str());
}

uint64_t
BaseFileOutputStream::Tell() const
{
	LONG high = 0;
	DWORD low = SetFilePointer(handle, 0, &high, FILE_CURRENT);
	if (low == 0xffffffff)
		return 0;

	return uint64_t(high) << 32 | uint64_t(low);
}

bool
BaseFileOutputStream::Write(const void *data, size_t size, Error &error)
{
	assert(IsDefined());

	DWORD nbytes;
	if (!WriteFile(handle, data, size, &nbytes, nullptr)) {
		error.FormatLastError("Failed to write to %s",
				      path.ToUTF8().c_str());
		return false;
	}

	if (size_t(nbytes) != size) {
		error.FormatLastError(ERROR_DISK_FULL,
				      "Failed to write to %s",
				      path.ToUTF8().c_str());
		return false;
	}

	return true;
}

bool
FileOutputStream::Commit(gcc_unused Error &error)
{
	assert(IsDefined());

	Close();
	return true;
}

void
FileOutputStream::Cancel()
{
	assert(IsDefined());

	Close();
	RemoveFile(GetPath());
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_LINKAT
#ifndef O_TMPFILE
/* supported since Linux 3.11 */
#define __O_TMPFILE 020000000
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#include <stdio.h>
#endif

/**
 * Open a file using Linux's O_TMPFILE for writing the given file.
 */
static bool
OpenTempFile(FileDescriptor &fd, Path path)
{
	const auto directory = path.GetDirectoryName();
	if (directory.IsNull())
		return false;

	return fd.Open(directory.c_str(), O_TMPFILE|O_WRONLY, 0666);
}

#endif /* HAVE_LINKAT */

FileOutputStream::FileOutputStream(Path _path, Error &error)
	:BaseFileOutputStream(_path)
{
#ifdef HAVE_LINKAT
	/* try Linux's O_TMPFILE first */
	is_tmpfile = OpenTempFile(SetFD(), GetPath());
	if (!is_tmpfile) {
#endif
		/* fall back to plain POSIX */
		if (!SetFD().Open(GetPath().c_str(),
				  O_WRONLY|O_CREAT|O_TRUNC,
				  0666))
			error.FormatErrno("Failed to create %s",
					  GetPath().c_str());
#ifdef HAVE_LINKAT
	}
#endif
}

uint64_t
BaseFileOutputStream::Tell() const
{
	return fd.Tell();
}

bool
BaseFileOutputStream::Write(const void *data, size_t size, Error &error)
{
	assert(IsDefined());

	ssize_t nbytes = fd.Write(data, size);
	if (nbytes < 0) {
		error.FormatErrno("Failed to write to %s", GetPath().c_str());
		return false;
	} else if ((size_t)nbytes < size) {
		error.FormatErrno(ENOSPC,
				  "Failed to write to %s", GetPath().c_str());
		return false;
	}

	return true;
}

bool
FileOutputStream::Commit(Error &error)
{
	assert(IsDefined());

#if HAVE_LINKAT
	if (is_tmpfile) {
		RemoveFile(GetPath());

		/* hard-link the temporary file to the final path */
		char fd_path[64];
		snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d",
			 GetFD().Get());
		if (linkat(AT_FDCWD, fd_path, AT_FDCWD, GetPath().c_str(),
			   AT_SYMLINK_FOLLOW) < 0) {
			error.FormatErrno("Failed to commit %s",
					  GetPath().c_str());
			Close();
			return false;
		}
	}
#endif

	bool success = Close();
	if (!success)
		error.FormatErrno("Failed to commit %s", GetPath().c_str());

	return success;
}

void
FileOutputStream::Cancel()
{
	assert(IsDefined());

	Close();

#ifdef HAVE_LINKAT
	if (!is_tmpfile)
#endif
		RemoveFile(GetPath());
}

#endif

AppendFileOutputStream::AppendFileOutputStream(Path _path, Error &error)
	:BaseFileOutputStream(_path)
{
#ifdef WIN32
	SetHandle(CreateFile(GetPath().c_str(), GENERIC_WRITE, 0, nullptr,
			     OPEN_EXISTING,
			     FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,
			     nullptr));
	if (!IsDefined())
		error.FormatLastError("Failed to append to %s",
				      GetPath().ToUTF8().c_str());

	if (!SeekEOF()) {
		error.FormatLastError("Failed seek end-of-file of %s",
				      GetPath().ToUTF8().c_str());
		Close();
	}
#else
	if (!SetFD().Open(GetPath().c_str(),
			  O_WRONLY|O_APPEND))
		error.FormatErrno("Failed to append to %s",
				  GetPath().c_str());
#endif
}

bool
AppendFileOutputStream::Commit(gcc_unused Error &error)
{
	assert(IsDefined());

#ifdef WIN32
	return Close();
#else
	bool success = Close();
	if (!success)
		error.FormatErrno("Failed to commit %s", GetPath().c_str());

	return success;
#endif
}
