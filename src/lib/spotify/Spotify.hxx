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

#ifndef SPOTIFY_HXX
#define SPOTIFY_HXX


#include "event/DeferredMonitor.hxx"
#include "event/TimeoutMonitor.hxx"
#include "event/Loop.hxx"
#include "util/Error.hxx"
#include "tag/Tag.hxx"

extern "C" {
#include <libspotify/api.h>
}

#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

class Spotify;
class SongEnumerator;

extern Spotify *spotify;

class Spotify_Player {
public:
  Spotify_Player(){};
  ~Spotify_Player(){};
  
  virtual void end_of_track()=0;
  virtual int music_delivery(const sp_audioformat *format,const void *frames, int num_frames) =0;
  virtual void metadata_updated(Tag *_tag)=0;
  
};

class Spotify :public DeferredMonitor,TimeoutMonitor {
public:
  Spotify(EventLoop &loop);
  ~Spotify();
  
  sp_error Login(const char *username = NULL, const char *password=NULL);
  
  sp_error Play(const char *url, Spotify_Player *_player);
  void Stop(Spotify_Player *_player);

  SongEnumerator *LoadPlayList(const char *url); 
   
  int getPlaylistSize(); 
  sp_playlist_type getPlaylist(int i, std::string &name,std::string &url); 
  
  static Spotify *Create();
protected:  
  
  void OnTimeout();
  void RunDeferred();

private:
  sp_session *sp;
  Spotify_Player *player;
  sp_track *current_track;                                                
  
  pthread_mutex_t g_notify_mutex;
  
  static int music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames);
  static void end_of_track(sp_session *sess);
  static void metadata_updated(sp_session *sess);
  static void notify_main_thread(sp_session *sess);
  static void message_to_user(sp_session *session, const char *message);
  static void log_message(sp_session *session, const char *data);
};


#endif