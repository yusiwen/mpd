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
#include "PlaylistStream.hxx"
#include "PlaylistRegistry.hxx"
#include "CloseSongEnumerator.hxx"
#include "util/UriUtil.hxx"
#include "util/Error.hxx"
#include "input/InputStream.hxx"
#include "input/LocalOpen.hxx"
#include "fs/Path.hxx"
#include "Log.hxx"

#include <assert.h>

static SongEnumerator *
playlist_open_path_suffix(Path path, Mutex &mutex, Cond &cond)
{
	assert(!path.IsNull());

	const auto *suffix = path.GetSuffix();
	if (suffix == nullptr)
		return nullptr;

	const auto suffix_utf8 = Path::FromFS(suffix).ToUTF8();
	if (!playlist_suffix_supported(suffix_utf8.c_str()))
		return nullptr;

	Error error;
	InputStream *is = OpenLocalInputStream(path, mutex, cond, error);
	if (is == nullptr) {
		LogError(error);
		return nullptr;
	}

	auto playlist = playlist_list_open_stream_suffix(*is,
							 suffix_utf8.c_str());
	if (playlist != nullptr)
		playlist = new CloseSongEnumerator(playlist, is);
	else
		delete is;

	return playlist;
}

SongEnumerator *
playlist_open_path(Path path, Mutex &mutex, Cond &cond)
{
	assert(!path.IsNull());

	const std::string uri_utf8 = path.ToUTF8();
	auto playlist = !uri_utf8.empty()
		? playlist_list_open_uri(uri_utf8.c_str(), mutex, cond)
		: nullptr;
	if (playlist == nullptr)
		playlist = playlist_open_path_suffix(path, mutex, cond);

	return playlist;
}

SongEnumerator *
playlist_open_remote(const char *uri, Mutex &mutex, Cond &cond)
{
	assert(uri_has_scheme(uri));

	SongEnumerator *playlist = playlist_list_open_uri(uri, mutex, cond);
	if (playlist != nullptr)
		return playlist;

	Error error;
	InputStream *is = InputStream::OpenReady(uri, mutex, cond, error);
	if (is == nullptr) {
		if (error.IsDefined())
			FormatError(error, "Failed to open %s", uri);

		return nullptr;
	}

	playlist = playlist_list_open_stream(*is, uri);
	if (playlist == nullptr) {
		delete is;
		return nullptr;
	}

	return new CloseSongEnumerator(playlist, is);
}
