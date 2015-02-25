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
#include "ConfigGlobal.hxx"
#include "ConfigParser.hxx"
#include "Data.hxx"
#include "Param.hxx"
#include "Block.hxx"
#include "ConfigFile.hxx"
#include "ConfigPath.hxx"
#include "ConfigError.hxx"
#include "fs/Path.hxx"
#include "fs/AllocatedPath.hxx"
#include "util/Error.hxx"
#include "system/FatalError.hxx"
#include "Log.hxx"

#include <stdlib.h>

static ConfigData config_data;

void config_global_finish(void)
{
	config_data.Clear();
}

void config_global_init(void)
{
}

bool
ReadConfigFile(Path path, Error &error)
{
	return ReadConfigFile(config_data, path, error);
}

static void
Check(const ConfigBlock &block)
{
	if (!block.used)
		/* this whole block was not queried at all -
		   the feature might be disabled at compile time?
		   Silently ignore it here. */
		return;

	for (const auto &i : block.block_params) {
		if (!i.used)
			FormatWarning(config_domain,
				      "option '%s' on line %i was not recognized",
				      i.name.c_str(), i.line);
	}
}

void config_global_check(void)
{
	for (auto i : config_data.blocks)
		for (const auto *p = i; p != nullptr; p = p->next)
			Check(*p);
}

const config_param *
config_get_param(ConfigOption option)
{
	config_param *param = config_data.params[unsigned(option)];
	if (param != nullptr)
		param->used = true;
	return param;
}

const ConfigBlock *
config_get_block(ConfigBlockOption option)
{
	ConfigBlock *block = config_data.blocks[unsigned(option)];
	if (block != nullptr)
		block->used = true;
	return block;
}

const ConfigBlock *
config_find_block(ConfigBlockOption option, const char *key, const char *value)
{
	for (const auto *block = config_get_block(option);
	     block != nullptr; block = block->next) {
		const char *value2 = block->GetBlockValue(key);
		if (value2 == nullptr)
			FormatFatalError("block without '%s' name in line %d",
					 key, block->line);

		if (strcmp(value2, value) == 0)
			return block;
	}

	return nullptr;
}

const char *
config_get_string(ConfigOption option, const char *default_value)
{
	const struct config_param *param = config_get_param(option);

	if (param == nullptr)
		return default_value;

	return param->value.c_str();
}

AllocatedPath
config_get_path(ConfigOption option, Error &error)
{
	const struct config_param *param = config_get_param(option);
	if (param == nullptr)
		return AllocatedPath::Null();

	return config_parse_path(param, error);
}

AllocatedPath
config_parse_path(const struct config_param *param, Error & error)
{
	AllocatedPath path = ParsePath(param->value.c_str(), error);
	if (gcc_unlikely(path.IsNull()))
		error.FormatPrefix("Invalid path at line %i: ",
				   param->line);

	return path;
}

unsigned
config_get_unsigned(ConfigOption option, unsigned default_value)
{
	const struct config_param *param = config_get_param(option);
	long value;
	char *endptr;

	if (param == nullptr)
		return default_value;

	value = strtol(param->value.c_str(), &endptr, 0);
	if (*endptr != 0 || value < 0)
		FormatFatalError("Not a valid non-negative number in line %i",
				 param->line);

	return (unsigned)value;
}

unsigned
config_get_positive(ConfigOption option, unsigned default_value)
{
	const struct config_param *param = config_get_param(option);
	long value;
	char *endptr;

	if (param == nullptr)
		return default_value;

	value = strtol(param->value.c_str(), &endptr, 0);
	if (*endptr != 0)
		FormatFatalError("Not a valid number in line %i", param->line);

	if (value <= 0)
		FormatFatalError("Not a positive number in line %i",
				 param->line);

	return (unsigned)value;
}

bool
config_get_bool(ConfigOption option, bool default_value)
{
	const struct config_param *param = config_get_param(option);
	bool success, value;

	if (param == nullptr)
		return default_value;

	success = get_bool(param->value.c_str(), &value);
	if (!success)
		FormatFatalError("Expected boolean value (yes, true, 1) or "
				 "(no, false, 0) on line %i\n",
				 param->line);

	return value;
}
