/*
 * Copyright (C) 2003-2013 The Music Player Daemon Project
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
#include "SpotifyPlaylistPlugin.hxx"
#include "../PlaylistPlugin.hxx"
#include "../SongEnumerator.hxx"
#include "../MemorySongEnumerator.hxx"
#include "input/InputStream.hxx"
#include "tag/TagBuilder.hxx"
#include "DetachedSong.hxx"
#include "util/Domain.hxx"
#include "util/StringUtil.hxx"
#include "util/Error.hxx"
#include "Log.hxx"
#include "lib/spotify/Spotify.hxx"

#include <string.h>


static constexpr Domain spotify_domain("spotify");


static SongEnumerator *
spotify_open_playlist(const char *url,
			    gcc_unused Mutex &mutex, gcc_unused Cond &cond)
{
  std::forward_list<DetachedSong> songs;
  
  printf("spotify_open_playlist URL %s\n",url);
  if (!strncmp(url,"spotify://",10)) {
    return spotify->LoadPlayList(url);
  }
  else if ((strstr(url,"play.spotify.com") == NULL) && 
    (strstr(url,"open.spotify.com") == NULL))
    return nullptr;

  
//  if (strstr(url,"/playlist/") == NULL)
//    return nullptr;
  
  return spotify->LoadPlayList(url);
}


static const char *const spotify_schemes[] = {
	"spotify",
	"http",
	"https",
	nullptr
};

const struct playlist_plugin spotify_playlist_plugin = {
	"cddb",

	nullptr,
	nullptr,
	spotify_open_playlist,
	nullptr,

	spotify_schemes,
	nullptr,
	nullptr,
};
