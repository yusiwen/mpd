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
#include "TagCommands.hxx"
#include "CommandError.hxx"
#include "client/Client.hxx"
#include "protocol/ArgParser.hxx"
#include "protocol/Result.hxx"
#include "tag/Tag.hxx"
#include "Partition.hxx"
#include "util/ConstBuffer.hxx"

CommandResult
handle_addtagid(Client &client, ConstBuffer<const char *> args)
{
	unsigned song_id;
	if (!check_unsigned(client, &song_id, args.front()))
		return CommandResult::ERROR;

	const char *const tag_name = args[1];
	const TagType tag_type = tag_name_parse_i(tag_name);
	if (tag_type == TAG_NUM_OF_ITEM_TYPES) {
		command_error(client, ACK_ERROR_ARG,
			      "Unknown tag type: %s", tag_name);
		return CommandResult::ERROR;
	}

	const char *const value = args[2];

	Error error;
	if (!client.partition.playlist.AddSongIdTag(song_id, tag_type, value,
						    error))
		return print_error(client, error);

	return CommandResult::OK;
}

CommandResult
handle_cleartagid(Client &client, ConstBuffer<const char *> args)
{
	unsigned song_id;
	if (!check_unsigned(client, &song_id, args.front()))
		return CommandResult::ERROR;

	TagType tag_type = TAG_NUM_OF_ITEM_TYPES;
	if (args.size >= 2) {
		const char *const tag_name = args[1];
		tag_type = tag_name_parse_i(tag_name);
		if (tag_type == TAG_NUM_OF_ITEM_TYPES) {
			command_error(client, ACK_ERROR_ARG,
				      "Unknown tag type: %s", tag_name);
			return CommandResult::ERROR;
		}
	}

	Error error;
	if (!client.partition.playlist.ClearSongIdTag(song_id, tag_type,
						      error))
		return print_error(client, error);

	return CommandResult::OK;
}
