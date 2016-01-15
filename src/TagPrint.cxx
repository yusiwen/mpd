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
#include "TagPrint.hxx"
#include "tag/Tag.hxx"
#include "tag/Settings.hxx"
#include "client/Response.hxx"

void
tag_print_types(Response &r)
{
	for (unsigned i = 0; i < TAG_NUM_OF_ITEM_TYPES; i++)
		if (IsTagEnabled(i))
			r.Format("tagtype: %s\n", tag_item_names[i]);
}

void
tag_print(Response &r, TagType type, const char *value)
{
	r.Format("%s: %s\n", tag_item_names[type], value);
}

void
tag_print_values(Response &r, const Tag &tag)
{
	for (const auto &i : tag)
		r.Format("%s: %s\n", tag_item_names[i.type], i.value);
}

void
tag_print(Response &r, const Tag &tag)
{
	if (!tag.duration.IsNegative())
		r.Format("Time: %i\n"
			 "duration: %1.3f\n",
			 tag.duration.RoundS(),
			 tag.duration.ToDoubleS());

	tag_print_values(r, tag);
}
