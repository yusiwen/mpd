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
#include "PlaylistPrint.hxx"
#include "PlaylistFile.hxx"
#include "queue/Playlist.hxx"
#include "queue/QueuePrint.hxx"
#include "SongPrint.hxx"
#include "Partition.hxx"
#include "Instance.hxx"
#include "db/Interface.hxx"
#include "client/Client.hxx"
#include "client/Response.hxx"
#include "input/InputStream.hxx"
#include "DetachedSong.hxx"
#include "fs/Traits.hxx"
#include "util/Error.hxx"
#include "thread/Cond.hxx"

#define SONG_FILE "file: "
#define SONG_TIME "Time: "

void
playlist_print_uris(Response &r, Partition &partition,
		    const playlist &playlist)
{
	const Queue &queue = playlist.queue;

	queue_print_uris(r, partition, queue, 0, queue.GetLength());
}

bool
playlist_print_info(Response &r, Partition &partition,
		    const playlist &playlist,
		    unsigned start, unsigned end)
{
	const Queue &queue = playlist.queue;

	if (end > queue.GetLength())
		/* correct the "end" offset */
		end = queue.GetLength();

	if (start > end)
		/* an invalid "start" offset is fatal */
		return false;

	queue_print_info(r, partition, queue, start, end);
	return true;
}

bool
playlist_print_id(Response &r, Partition &partition, const playlist &playlist,
		  unsigned id)
{
	int position;

	position = playlist.queue.IdToPosition(id);
	if (position < 0)
		/* no such song */
		return false;

	return playlist_print_info(r, partition,
				   playlist, position, position + 1);
}

bool
playlist_print_current(Response &r, Partition &partition,
		       const playlist &playlist)
{
	int current_position = playlist.GetCurrentPosition();
	if (current_position < 0)
		return false;

	queue_print_info(r, partition, playlist.queue,
			 current_position, current_position + 1);
	return true;
}

void
playlist_print_find(Response &r, Partition &partition,
		    const playlist &playlist,
		    const SongFilter &filter)
{
	queue_find(r, partition, playlist.queue, filter);
}

void
playlist_print_changes_info(Response &r, Partition &partition,
			    const playlist &playlist,
			    uint32_t version,
			    unsigned start, unsigned end)
{
	queue_print_changes_info(r, partition, playlist.queue, version,
				 start, end);
}

void
playlist_print_changes_position(Response &r,
				const playlist &playlist,
				uint32_t version,
				unsigned start, unsigned end)
{
	queue_print_changes_position(r, playlist.queue, version,
				     start, end);
}

#ifdef ENABLE_DATABASE

static bool
PrintSongDetails(Response &r, Partition &partition, const char *uri_utf8)
{
	const Database *db = partition.instance.database;
	if (db == nullptr)
		return false;

	auto *song = db->GetSong(uri_utf8, IgnoreError());
	if (song == nullptr)
		return false;

	song_print_info(r, partition, *song);
	db->ReturnSong(song);
	return true;
}

#endif

void
spl_print(Response &r, Partition &partition,
	  const char *name_utf8, bool detail)
{
#ifndef ENABLE_DATABASE
	(void)partition;
	(void)detail;
#endif

	PlaylistFileContents contents = LoadPlaylistFile(name_utf8);

	for (const auto &uri_utf8 : contents) {
#ifdef ENABLE_DATABASE
		if (!detail || !PrintSongDetails(r, partition,
						 uri_utf8.c_str()))
#endif
			r.Format(SONG_FILE "%s\n", uri_utf8.c_str());
	}
}
