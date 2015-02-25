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
#include "Config.hxx"
#include "Charset.hxx"
#include "config/ConfigGlobal.hxx"

#ifdef WIN32
#include <windows.h> // for GetACP()
#include <stdio.h> // for sprintf()
#elif defined(HAVE_GLIB)
#include <glib.h>
#endif

bool
ConfigureFS(Error &error)
{
#ifdef HAVE_FS_CHARSET
	const char *charset = nullptr;

	charset = config_get_string(ConfigOption::FS_CHARSET, nullptr);
	if (charset == nullptr) {
#ifdef WIN32
		/* Glib claims that file system encoding is always utf-8
		 * on native Win32 (i.e. not Cygwin).
		 * However this is true only if <gstdio.h> helpers are used.
		 * MPD uses regular <stdio.h> functions.
		 * Those functions use encoding determined by GetACP(). */
		static char win_charset[13];
		sprintf(win_charset, "cp%u", GetACP());
		charset = win_charset;
#elif defined(HAVE_GLIB)
		const gchar **encodings;
		g_get_filename_charsets(&encodings);

		if (encodings[0] != nullptr && *encodings[0] != '\0')
			charset = encodings[0];
#endif
	}

	return charset == nullptr || SetFSCharset(charset, error);
#else
	(void)error;
	return true;
#endif
}

void
DeinitFS()
{
#ifdef HAVE_FS_CHARSET
	DeinitFSCharset();
#endif
}
