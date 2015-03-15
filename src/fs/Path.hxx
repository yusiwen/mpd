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

#ifndef MPD_FS_PATH_HXX
#define MPD_FS_PATH_HXX

#include "check.h"
#include "Compiler.h"
#include "Traits.hxx"

#include <string>

#include <assert.h>
#include <string.h>

class AllocatedPath;

/**
 * A path name in the native file system character set.
 *
 * This class manages a pointer to an existing path string.  While an
 * instance lives, the string must not be invalidated.
 */
class Path {
	typedef PathTraitsFS::value_type value_type;
	typedef PathTraitsFS::pointer pointer;
	typedef PathTraitsFS::const_pointer const_pointer;

	const_pointer value;

	constexpr Path(const_pointer _value):value(_value) {}

public:
	/**
	 * Copy a #Path object.
	 */
	constexpr Path(const Path &) = default;

	/**
	 * Return a "nulled" instance.  Its IsNull() method will
	 * return true.  Such an object must not be used.
	 *
	 * @see IsNull()
	 */
	static constexpr Path Null() {
		return Path(nullptr);
	}

	/**
	 * Create a new instance pointing to the specified path
	 * string.
	 */
	static constexpr Path FromFS(const_pointer fs) {
		return Path(fs);
	}

	/**
	 * Copy a #Path object.
	 */
	Path &operator=(const Path &) = default;

	/**
	 * Check if this is a "nulled" instance.  A "nulled" instance
	 * must not be used.
	 */
	bool IsNull() const {
		return value == nullptr;
	}

	/**
	 * Clear this object's value, make it "nulled".
	 *
	 * @see IsNull()
	 */
	void SetNull() {
		value = nullptr;
	}

	/**
	 * @return the length of this string in number of "value_type"
	 * elements (which may not be the number of characters).
	 */
	gcc_pure
	size_t length() const {
		assert(value != nullptr);

		return PathTraitsFS::GetLength(value);
	}

	/**
	 * Returns the value as a const C string.  The returned
	 * pointer is invalidated whenever the value of life of this
	 * instance ends.
	 */
	gcc_pure
	const_pointer c_str() const {
		return value;
	}

	/**
	 * Returns a pointer to the raw value, not necessarily
	 * null-terminated.
	 */
	gcc_pure
	const_pointer data() const {
		return value;
	}

	/**
	 * Does the path contain a newline character?  (Which is
	 * usually rejected by MPD because its protocol cannot
	 * transfer newline characters).
	 */
	gcc_pure
	bool HasNewline() const {
		return PathTraitsFS::Find(value, '\n') != nullptr;
	}

	/**
	 * Convert the path to UTF-8.
	 * Returns empty string on error or if this instance is "nulled"
	 * (#IsNull returns true).
	 */
	gcc_pure
	std::string ToUTF8() const;

	/**
	 * Determine the "base" file name.
	 * The return value points inside this object.
	 */
	gcc_pure
	Path GetBase() const {
		return FromFS(PathTraitsFS::GetBase(value));
	}

	/**
	 * Gets directory name of this path.
	 * Returns a "nulled" instance on error.
	 */
	gcc_pure
	AllocatedPath GetDirectoryName() const;

	/**
	 * Determine the relative part of the given path to this
	 * object, not including the directory separator.  Returns an
	 * empty string if the given path equals this object or
	 * nullptr on mismatch.
	 */
	gcc_pure
	const_pointer Relative(Path other_fs) const {
		return PathTraitsFS::Relative(c_str(), other_fs.c_str());
	}

	gcc_pure
	bool IsAbsolute() const {
		return PathTraitsFS::IsAbsolute(c_str());
	}

	gcc_pure
	const_pointer GetSuffix() const;
};

#endif
