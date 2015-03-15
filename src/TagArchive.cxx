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
#include "TagArchive.hxx"
#include "TagStream.hxx"
#include "fs/Path.hxx"
#include "util/Error.hxx"
#include "input/InputStream.hxx"
#include "input/plugins/ArchiveInputPlugin.hxx"
#include "thread/Cond.hxx"

#include <assert.h>

bool
tag_archive_scan(Path path, const tag_handler &handler, void *handler_ctx)
{
	assert(!path.IsNull());

	Mutex mutex;
	Cond cond;
	auto *is = OpenArchiveInputStream(path, mutex, cond, IgnoreError());
	if (is == nullptr)
		return false;

	bool result = tag_stream_scan(*is, handler, handler_ctx);
	delete is;
	return result;
}
