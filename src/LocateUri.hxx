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

#ifndef MPD_LOCATE_URI_HXX
#define MPD_LOCATE_URI_HXX

#include "check.h"
#include "Compiler.h"
#include "fs/AllocatedPath.hxx"

#ifdef WIN32
#include <windows.h>
/* damn you, windows.h! */
#ifdef ABSOLUTE
#undef ABSOLUTE
#endif
#ifdef RELATIVE
#undef RELATIVE
#endif
#endif

class Domain;
class Error;
class Client;

#ifdef ENABLE_DATABASE
class Storage;
#endif

struct LocatedUri {
	enum class Type {
		/**
		 * Failed to parse the URI.
		 */
		UNKNOWN,

		/**
		 * An absolute URI with a supported scheme.
		 */
		ABSOLUTE,

		/**
		 * A relative URI path.
		 */
		RELATIVE,

		/**
		 * A local file.  The #path attribute is valid.
		 */
		PATH,
	} type;

	const char *canonical_uri;

	/**
	 * Contains the local file path if type==FILE.
	 */
	AllocatedPath path;

	LocatedUri(Type _type, const char *_uri,
		   AllocatedPath &&_path=AllocatedPath::Null())
		:type(_type), canonical_uri(_uri), path(std::move(_path)) {}

	gcc_const
	static LocatedUri Unknown() {
		return LocatedUri(Type::UNKNOWN, nullptr);
	}

	bool IsUnknown() const {
		return type == Type::UNKNOWN;
	}
};

extern const Domain locate_uri_domain;

/**
 * Classify a URI.
 *
 * @param client the #Client that is used to determine whether a local
 * file is allowed; nullptr disables the check and allows all local
 * files
 * @param storage a #Storage instance which may be used to convert
 * absolute URIs to relative ones, using Storage::MapToRelativeUTF8();
 * that feature is disabled if this parameter is nullptr
 */
LocatedUri
LocateUri(const char *uri, const Client *client,
#ifdef ENABLE_DATABASE
	  const Storage *storage,
#endif
	  Error &error);

#endif
