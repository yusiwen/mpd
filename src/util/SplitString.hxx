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

#ifndef MPD_SPLIT_STRING_HXX
#define MPD_SPLIT_STRING_HXX

#include <forward_list>
#include <string>

/**
 * Split a string at a certain separator character into sub strings
 * and returns a list of these.
 *
 * Two consecutive separator characters result in an empty string in
 * the list.
 *
 * An empty input string, as a special case, results in an empty list
 * (and not a list with an empty string).
 */
std::forward_list<std::string>
SplitString(const char *s, char separator, bool strip=true);

#endif
