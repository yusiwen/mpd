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
#include "DatabaseCommands.hxx"
#include "Request.hxx"
#include "db/DatabaseGlue.hxx"
#include "db/DatabaseQueue.hxx"
#include "db/DatabasePlaylist.hxx"
#include "db/DatabasePrint.hxx"
#include "db/Count.hxx"
#include "db/Selection.hxx"
#include "CommandError.hxx"
#include "client/Client.hxx"
#include "client/Response.hxx"
#include "tag/Tag.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Error.hxx"
#include "util/StringAPI.hxx"
#include "SongFilter.hxx"
#include "BulkEdit.hxx"

#include <string.h>

CommandResult
handle_listfiles_db(Client &client, Response &r, const char *uri)
{
	const DatabaseSelection selection(uri, false);

	Error error;
	if (!db_selection_print(r, client.partition,
				selection, false, true, error))
		return print_error(r, error);

	return CommandResult::OK;
}

CommandResult
handle_lsinfo2(Client &client, const char *uri, Response &r)
{
	const DatabaseSelection selection(uri, false);

	Error error;
	if (!db_selection_print(r, client.partition,
				selection, true, false, error))
		return print_error(r, error);

	return CommandResult::OK;
}

static CommandResult
handle_match(Client &client, Request args, Response &r, bool fold_case)
{
	RangeArg window;
	if (args.size >= 2 && StringIsEqual(args[args.size - 2], "window")) {
		window = args.ParseRange(args.size - 1);

		args.pop_back();
		args.pop_back();
	} else
		window.SetAll();

	SongFilter filter;
	if (!filter.Parse(args, fold_case)) {
		r.Error(ACK_ERROR_ARG, "incorrect arguments");
		return CommandResult::ERROR;
	}

	const DatabaseSelection selection("", true, &filter);

	Error error;
	return db_selection_print(r, client.partition,
				  selection, true, false,
				  window.start, window.end, error)
		? CommandResult::OK
		: print_error(r, error);
}

CommandResult
handle_find(Client &client, Request args, Response &r)
{
	return handle_match(client, args, r, false);
}

CommandResult
handle_search(Client &client, Request args, Response &r)
{
	return handle_match(client, args, r, true);
}

static CommandResult
handle_match_add(Client &client, Request args, Response &r, bool fold_case)
{
	SongFilter filter;
	if (!filter.Parse(args, fold_case)) {
		r.Error(ACK_ERROR_ARG, "incorrect arguments");
		return CommandResult::ERROR;
	}

	const ScopeBulkEdit bulk_edit(client.partition);

	const DatabaseSelection selection("", true, &filter);
	Error error;
	return AddFromDatabase(client.partition, selection, error)
		? CommandResult::OK
		: print_error(r, error);
}

CommandResult
handle_findadd(Client &client, Request args, Response &r)
{
	return handle_match_add(client, args, r, false);
}

CommandResult
handle_searchadd(Client &client, Request args, Response &r)
{
	return handle_match_add(client, args, r, true);
}

CommandResult
handle_searchaddpl(Client &client, Request args, Response &r)
{
	const char *playlist = args.shift();

	SongFilter filter;
	if (!filter.Parse(args, true)) {
		r.Error(ACK_ERROR_ARG, "incorrect arguments");
		return CommandResult::ERROR;
	}

	Error error;
	const Database *db = client.GetDatabase(error);
	if (db == nullptr)
		return print_error(r, error);

	return search_add_to_playlist(*db, *client.GetStorage(),
				      "", playlist, &filter, error)
		? CommandResult::OK
		: print_error(r, error);
}

CommandResult
handle_count(Client &client, Request args, Response &r)
{
	TagType group = TAG_NUM_OF_ITEM_TYPES;
	if (args.size >= 2 && StringIsEqual(args[args.size - 2], "group")) {
		const char *s = args[args.size - 1];
		group = tag_name_parse_i(s);
		if (group == TAG_NUM_OF_ITEM_TYPES) {
			r.FormatError(ACK_ERROR_ARG,
				      "Unknown tag type: %s", s);
			return CommandResult::ERROR;
		}

		args.pop_back();
		args.pop_back();
	}

	SongFilter filter;
	if (!args.IsEmpty() && !filter.Parse(args, false)) {
		r.Error(ACK_ERROR_ARG, "incorrect arguments");
		return CommandResult::ERROR;
	}

	Error error;
	return PrintSongCount(r, client.partition, "", &filter, group, error)
		? CommandResult::OK
		: print_error(r, error);
}

CommandResult
handle_listall(Client &client, Request args, Response &r)
{
	/* default is root directory */
	const auto uri = args.GetOptional(0, "");

	Error error;
	return db_selection_print(r, client.partition,
				  DatabaseSelection(uri, true),
				  false, false, error)
		? CommandResult::OK
		: print_error(r, error);
}

CommandResult
handle_list(Client &client, Request args, Response &r)
{
	const char *tag_name = args.shift();
	unsigned tagType = locate_parse_type(tag_name);

	if (tagType >= TAG_NUM_OF_ITEM_TYPES &&
	    tagType != LOCATE_TAG_FILE_TYPE) {
		r.FormatError(ACK_ERROR_ARG,
			      "Unknown tag type: %s", tag_name);
		return CommandResult::ERROR;
	}

	SongFilter *filter = nullptr;
	tag_mask_t group_mask = 0;

	if (args.size == 1) {
		/* for compatibility with < 0.12.0 */
		if (tagType != TAG_ALBUM) {
			r.FormatError(ACK_ERROR_ARG,
				      "should be \"%s\" for 3 arguments",
				      tag_item_names[TAG_ALBUM]);
			return CommandResult::ERROR;
		}

		filter = new SongFilter((unsigned)TAG_ARTIST, args.shift());
	}

	while (args.size >= 2 &&
	       StringIsEqual(args[args.size - 2], "group")) {
		const char *s = args[args.size - 1];
		TagType gt = tag_name_parse_i(s);
		if (gt == TAG_NUM_OF_ITEM_TYPES) {
			r.FormatError(ACK_ERROR_ARG,
				      "Unknown tag type: %s", s);
			return CommandResult::ERROR;
		}

		group_mask |= tag_mask_t(1) << unsigned(gt);

		args.pop_back();
		args.pop_back();
	}

	if (!args.IsEmpty()) {
		filter = new SongFilter();
		if (!filter->Parse(args, false)) {
			delete filter;
			r.Error(ACK_ERROR_ARG, "not able to parse args");
			return CommandResult::ERROR;
		}
	}

	if (tagType < TAG_NUM_OF_ITEM_TYPES &&
	    group_mask & (tag_mask_t(1) << tagType)) {
		delete filter;
		r.Error(ACK_ERROR_ARG, "Conflicting group");
		return CommandResult::ERROR;
	}

	Error error;
	CommandResult ret =
		PrintUniqueTags(r, client.partition,
				tagType, group_mask, filter, error)
		? CommandResult::OK
		: print_error(r, error);

	delete filter;

	return ret;
}

CommandResult
handle_listallinfo(Client &client, Request args, Response &r)
{
	/* default is root directory */
	const auto uri = args.GetOptional(0, "");

	Error error;
	return db_selection_print(r, client.partition,
				  DatabaseSelection(uri, true),
				  true, false, error)
		? CommandResult::OK
		: print_error(r, error);
}
