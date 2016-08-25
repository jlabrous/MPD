/*
 * Copyright (C) 2003-2014 The Music Player Daemon Project
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
#include "SpotifyInputPlugin.hxx"
#include "../AsyncInputStream.hxx"
#include "../InputPlugin.hxx"
#include "util/HugeAllocator.hxx"
#include "util/StringCompare.hxx"
#include "util/Error.hxx"
#include "config/ConfigGlobal.hxx"
#include "config/Block.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"
#include "tag/Tag.hxx"
#include "tag/TagBuilder.hxx"
#include "Chrono.hxx"
#include "event/Loop.hxx"
#include "lib/spotify/Spotify.hxx"

extern "C" {
#include <libspotify/api.h>
}

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

class SpotifyInputStream;


static constexpr Domain spotify_domain("spotify");
sp_session *sp=NULL;
sp_track *track;
SpotifyInputStream *is =NULL;
/**
 * Do not buffer more than this number of bytes.  It should be a
 * reasonable limit that doesn't make low-end machines suffer too
 * much, but doesn't cause stuttering on high-latency lines.
 */
static const size_t SPOTIFY_MAX_BUFFERED = 512 * 1024;

/**
 * Resume the stream at this number of bytes after it has been paused.
 */
static const size_t SPOTIFY_RESUME_AT = 384 * 1024;

class SpotifyInputStream final : public AsyncInputStream , Spotify_Player {
	uint64_t next_offset;

	bool reconnect_on_resume, reconnecting;

public:
	SpotifyInputStream(const char *_url,
		       Mutex &_mutex, Cond &_cond)
		:AsyncInputStream(_url, _mutex, _cond,
				  SPOTIFY_MAX_BUFFERED,
				  SPOTIFY_RESUME_AT),
		 reconnect_on_resume(false), reconnecting(false)  {
		Error error;
		   
		fprintf(stderr,"SpotifyInputStream(%s)\n",_url);
		spotify->Play(_url,this);
		   
		SetMimeType("audio/x-mpd-cdda-pcm");
		}

	virtual ~SpotifyInputStream() {
	  spotify->Stop(this);
	}
	int music_delivery( const sp_audioformat *format, const void *frames, int num_frames);
	
	void end_of_track() {
	  SetClosed();
	}
	
	void metadata_updated(Tag *_tag) {	  
	  SetTag(_tag);
	  SetReady();
		
	}
private:
	
protected:
	/* virtual methods from AsyncInputStream */
	virtual void DoResume() override;
	virtual void DoSeek(offset_type new_offset) override;

};


void
SpotifyInputStream::DoResume()
{
}

void
SpotifyInputStream::DoSeek(offset_type new_offset)
{
}

int SpotifyInputStream::music_delivery(const sp_audioformat *format, const void *frames, int num_frames)
{
  size_t s;
  size_t m;
  
  if ( num_frames == 0 ) return 0;

  s = num_frames * sizeof(int16_t) * format->channels;
  m = GetBufferSpace();

 
  if (s < m) {
    AppendToBuffer(frames, s);
    return num_frames;
  } else {
    return 0;
  }
}
  
/*
 * Spotify methods
 * 
 */


/**
 * The session configuration. Note that application_key_size is an
 * external, so we set it in main() instead.
 */

/*
 * InputPlugin methods
 *
 */

/*
 * spotify:track:5nSkWEU7XKqU9EiskvVSlo
 */
static InputPlugin::InitResult
input_spotify_init(const ConfigBlock &block, Error &error)
{
  
  sp_error err;
  
  const char *username,*password;
  
  puts("input_spotify_init io_thread_get");
  
  Spotify::Create();
  
  if (SP_ERROR_OK != err) {
    FormatError(spotify_domain, "Unable to create session: %s\n",
	    sp_error_message(err));
    return InputPlugin::InitResult::ERROR;
  } 
  
  username = block.GetBlockValue("spotify_user");
  password = block.GetBlockValue("spotify_password");

  spotify->Login(username, password);
  
  FormatInfo(spotify_domain,"input_spotify_init");
  
  return InputPlugin::InitResult::SUCCESS;
}

static void
input_spotify_finish()
{
}

static InputStream *
input_spotify_open(const char *url,
	       Mutex &mutex, Cond &cond,
	       Error &error)
{
	if (!StringStartsWith(url, "https://play.spotify.com/track/") &&
	    !StringStartsWith(url, "http://play.spotify.com/track/") &&
	    !StringStartsWith(url, "https://open.spotify.com/track/") &&
	    !StringStartsWith(url, "http://open.spotify.com/track/"))
		return nullptr;

        FormatInfo(spotify_domain,"input_spotify_open %s",url);

	is = new SpotifyInputStream(url, mutex, cond);
	return is;
}

const InputPlugin input_plugin_spotify = {
	"spotify",
	input_spotify_init,
	input_spotify_finish,
	input_spotify_open,
};
