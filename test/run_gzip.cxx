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
#include "fs/io/GzipOutputStream.hxx"
#include "fs/io/StdioOutputStream.hxx"
#include "Log.hxx"
#include "util/Error.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool
Copy(OutputStream &dest, int src, Error &error)
{
	while (true) {
		char buffer[4096];
		ssize_t nbytes = read(src, buffer, sizeof(buffer));
		if (nbytes <= 0) {
			if (nbytes < 0) {
				error.SetErrno();
				return false;
			} else
				return true;
		}

		dest.Write(buffer, nbytes);
	}
}

static bool
CopyGzip(OutputStream &_dest, int src, Error &error)
{
	GzipOutputStream dest(_dest);
	if (!Copy(dest, src, error))
		return false;

	dest.Flush();
	return true;
}

static bool
CopyGzip(FILE *_dest, int src, Error &error)
{
	StdioOutputStream dest(_dest);
	return CopyGzip(dest, src, error);
}

int
main(int argc, gcc_unused char **argv)
{
	if (argc != 1) {
		fprintf(stderr, "Usage: run_gzip\n");
		return EXIT_FAILURE;
	}

	try {
		Error error;
		if (!CopyGzip(stdout, STDIN_FILENO, error)) {
			fprintf(stderr, "%s\n", error.GetMessage());
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	} catch (const std::exception &e) {
		LogError(e);
		return EXIT_FAILURE;
	}
}
