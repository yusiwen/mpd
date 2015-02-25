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
#include "Block.hxx"
#include "ConfigParser.hxx"
#include "ConfigPath.hxx"
#include "system/FatalError.hxx"
#include "fs/AllocatedPath.hxx"
#include "util/Error.hxx"

#include <assert.h>
#include <stdlib.h>

int
BlockParam::GetIntValue() const
{
	char *endptr;
	long value2 = strtol(value.c_str(), &endptr, 0);
	if (*endptr != 0)
		FormatFatalError("Not a valid number in line %i", line);

	return value2;
}

unsigned
BlockParam::GetUnsignedValue() const
{
	char *endptr;
	unsigned long value2 = strtoul(value.c_str(), &endptr, 0);
	if (*endptr != 0)
		FormatFatalError("Not a valid number in line %i", line);

	return (unsigned)value2;
}

bool
BlockParam::GetBoolValue() const
{
	bool value2;
	if (!get_bool(value.c_str(), &value2))
		FormatFatalError("%s is not a boolean value (yes, true, 1) or "
				 "(no, false, 0) on line %i\n",
				 name.c_str(), line);

	return value2;
}

ConfigBlock::~ConfigBlock()
{
	delete next;
}

const BlockParam *
ConfigBlock::GetBlockParam(const char *name) const
{
	for (const auto &i : block_params) {
		if (i.name == name) {
			i.used = true;
			return &i;
		}
	}

	return nullptr;
}

const char *
ConfigBlock::GetBlockValue(const char *name, const char *default_value) const
{
	const BlockParam *bp = GetBlockParam(name);
	if (bp == nullptr)
		return default_value;

	return bp->value.c_str();
}

AllocatedPath
ConfigBlock::GetBlockPath(const char *name, const char *default_value,
			   Error &error) const
{
	assert(!error.IsDefined());

	int line2 = line;
	const char *s;

	const BlockParam *bp = GetBlockParam(name);
	if (bp != nullptr) {
		line2 = bp->line;
		s = bp->value.c_str();
	} else {
		if (default_value == nullptr)
			return AllocatedPath::Null();

		s = default_value;
	}

	AllocatedPath path = ParsePath(s, error);
	if (gcc_unlikely(path.IsNull()))
		error.FormatPrefix("Invalid path in \"%s\" at line %i: ",
				   name, line2);

	return path;
}

AllocatedPath
ConfigBlock::GetBlockPath(const char *name, Error &error) const
{
	return GetBlockPath(name, nullptr, error);
}

int
ConfigBlock::GetBlockValue(const char *name, int default_value) const
{
	const BlockParam *bp = GetBlockParam(name);
	if (bp == nullptr)
		return default_value;

	return bp->GetIntValue();
}

unsigned
ConfigBlock::GetBlockValue(const char *name, unsigned default_value) const
{
	const BlockParam *bp = GetBlockParam(name);
	if (bp == nullptr)
		return default_value;

	return bp->GetUnsignedValue();
}

gcc_pure
bool
ConfigBlock::GetBlockValue(const char *name, bool default_value) const
{
	const BlockParam *bp = GetBlockParam(name);
	if (bp == nullptr)
		return default_value;

	return bp->GetBoolValue();
}
