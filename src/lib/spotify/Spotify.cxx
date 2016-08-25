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

#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <netinet/in.h>

#include "config.h"
#include "Spotify.hxx"
#include "util/Domain.hxx"
#include "Log.hxx"
#include "Instance.hxx"
#include "tag/TagBuilder.hxx"
#include "playlist/SongEnumerator.hxx"
#include "playlist/MemorySongEnumerator.hxx"
#include "input/InputStream.hxx"
#include "src/db/PlaylistInfo.hxx"
#include "src/db/LightDirectory.hxx"

static constexpr Domain spotify_domain("spotify");
Spotify *spotify=NULL;

#include "spotify_appkey.c"
static sp_session_callbacks callbacks;
sp_session_config config;
char* getmacid();

char* getmacid()
{
  struct ifreq ifr;
  struct ifconf ifc;
  char buf[1024];
  int success = 0;
  static char hostid[20];
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock == -1) { /* handle error*/ };

  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = buf;
  if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) { /* handle error */ }

  struct ifreq* it = ifc.ifc_req;
  const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

  for (; it != end; ++it) {
      strcpy(ifr.ifr_name, it->ifr_name);
      if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
	  if (! (ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
	      if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
		  success = 1;
		  break;
	      }
	  }
      }
      else { /* handle error */ }
  }

  unsigned char mac_address[6];

  if (success) {
    memcpy(mac_address, ifr.ifr_hwaddr.sa_data, 6);
    sprintf (hostid,"%02X:%02X:%02X:%02X:%02X:%02X\n",mac_address[0],mac_address[1],mac_address[2],mac_address[3],mac_address[4],mac_address[5]);
    return hostid; 
  } else {
    return NULL;
  }
}

static char *url2uri(const char *url,char *uri){
  char *p;
  
  strcpy(uri,"spotify");
  strcat(uri,strchr(&url[8],'/'));

  p=uri;
  if((p=strchr(p,'#'))!=NULL)*p=0;
  
  p=uri;
  while((p=strchr(p,'/'))!=NULL)*p=':';

  return uri;
}

static char *uri2url(const char *uri,char *url){
  strcpy(url,"https://open.spotify.com");
  strcat(url,&uri[7]);
  char *p=&url[10];
  while((p=strchr(p,':'))!=NULL)*p='/';
  return url;
}

Spotify::Spotify(EventLoop &loop):
  DeferredMonitor(loop),
  TimeoutMonitor(loop)
{
  
  puts("Spotify::Spotify");
  
  callbacks.music_delivery=music_delivery;
  callbacks.end_of_track=end_of_track;
  callbacks.metadata_updated=metadata_updated;
  callbacks.notify_main_thread = notify_main_thread;
  callbacks.message_to_user=message_to_user;
  callbacks.log_message=log_message;
  
  config.api_version = SPOTIFY_API_VERSION;
  config.cache_location = "/var/tmp/spotify";
  config.settings_location = "/var/tmp/spotify";
  config.application_key = g_appkey;
  config.application_key_size = g_appkey_size;
  config.user_agent = "Music Player Daemon";
  config.callbacks = &callbacks;
  config.userdata = this;
  config.device_id=getmacid();

  spotify =  this;
  
  player = NULL;
  pthread_mutex_init(&g_notify_mutex, NULL);
}

sp_error Spotify::Login(const char *username, const char *password) {
  sp_error err;
  puts("sp_session_create");
  err = sp_session_create(&config, &sp);
  puts("sp_session_create done ");
 
  if (SP_ERROR_OK != err) {
    FormatError(spotify_domain, "Unable to create session: %s\n",
	    sp_error_message(err));
  } else {
    sp_session_preferred_bitrate (sp, SP_BITRATE_320k);
  }
    return sp_session_login(sp, username, password, 0, NULL);
}

sp_error Spotify::Play(const char *_url,Spotify_Player *_player) {
  sp_error err;
  char uri[256];
  url2uri(_url,uri);
  sp_link *link;
	
  fprintf(stderr,"Play %s\n",uri);
  link = sp_link_create_from_string ( uri );
  current_track = sp_link_as_track(link);
		   
  sp_track_add_ref(current_track);

  if (sp_track_error(current_track) == SP_ERROR_OK) {
    printf("Now playing \"%s\"...\n", sp_track_name(current_track));
    fflush(stdout);
  }
  player = _player;
  
  pthread_mutex_lock(&g_notify_mutex);
  err = sp_session_player_load(sp, current_track);
  sp_session_player_play(sp, 1);		   
  pthread_mutex_unlock(&g_notify_mutex);
  
  return err;
}
void Spotify::Stop(Spotify_Player *_player) {
 fprintf(stderr,"Stop\n");
  if (player != _player)  fprintf(stderr,"Hugh!\n");
  player = NULL;
  pthread_mutex_lock(&g_notify_mutex);
  sp_session_player_unload(sp);
  pthread_mutex_unlock(&g_notify_mutex);
}

static Tag get_Tag(sp_track *track) {
    TagBuilder tag_builder;
    
    const char *track_name=NULL;
    const char *album_name=NULL;
    const char *artist_name=NULL;
    char title[256];
    char uri[256];
    char url[256];
    track_name = sp_track_name(track);
		
    sp_album *album=sp_track_album (track);
    
    if (album) {
      album_name = sp_album_name(album);
      
      sp_artist * artist=sp_album_artist (album);
      
      if (artist) {
	  artist_name = sp_artist_name(artist);
      }
      
      sp_link *albumart=sp_link_create_from_album_cover(album,SP_IMAGE_SIZE_LARGE);
      char albumart_uri[256];
      char albumart_url[256];
      
      sp_link_as_string(albumart,albumart_uri,255);
      
      uri2url(albumart_uri,albumart_url);
      printf("AlbumArt=%s\n",albumart_url);
      tag_builder.AddItem(TAG_ALBUM_ART, albumart_url);
    }

    if (track_name) tag_builder.AddItem(TAG_NAME, track_name);
    if (album_name) tag_builder.AddItem(TAG_ALBUM, album_name);
    if (artist_name) tag_builder.AddItem(TAG_ALBUM_ARTIST, artist_name);
    
    int num_artists = sp_track_num_artists(track);
    
    if (num_artists > 0) {
      sp_artist * artist=sp_track_artist (track,0);
      artist_name = sp_artist_name(artist);
      tag_builder.AddItem(TAG_ARTIST, artist_name); 
    }
    
    snprintf(title,200,"%s - %s",artist_name,track_name);
    tag_builder.AddItem(TAG_TITLE, title); 
    
    for (int i=1;i<num_artists;i++) {
      sp_artist * artist=sp_track_artist (track,i);
      artist_name = sp_artist_name(artist);
      fprintf(stderr,"track_artist_name %s\n",artist_name);
      tag_builder.AddItem(TAG_ARTIST, artist_name); 
    }
    
    if (sp_link_as_string(sp_link_create_from_track(track,0),uri,255)) {
      uri2url(uri,url);
      tag_builder.AddItem(TAG_URL, url);
    }

    tag_builder.SetDuration(SignedSongTime::FromMS(sp_track_duration(track)));
    		
    return tag_builder.Commit();  
}

SongEnumerator *Spotify::LoadPlayList(const char *_url) {
  sp_playlist* playlist=NULL;
  
            
  sp_link *link;
  char uri[256];
  url2uri(_url,uri);
  link = sp_link_create_from_string ( uri );
  fprintf(stderr,"Load %s %d\n",uri,sp_link_type(link));
  playlist = sp_playlist_create(sp,link);

  if (playlist == NULL)
    return nullptr;

  std::forward_list<DetachedSong> songs;

  int tracknum= sp_playlist_num_tracks(playlist);
  for (int i =0; i< tracknum;i++) {
    sp_track* track = 	sp_playlist_track(playlist,i);
    Tag tag=get_Tag(track);
    char uri[256];char url[256];
    if (sp_link_as_string(sp_link_create_from_track(track,0),uri,255)) {
      uri2url(uri,url);
      printf("URI %s\nURL %s\n\n",uri,url);
      DetachedSong song(url);
      song.SetTag(tag);
      songs.emplace_front(song);
    }
  }
  songs.reverse();
  return new MemorySongEnumerator(std::move(songs));;
}

void Spotify::metadata_updated(sp_session *sess)
{
  Spotify *self=static_cast<Spotify*>(sp_session_userdata (sess));
  Spotify_Player *p;
  
  fprintf(stderr,"Metadata updated\n");

  p = self->player;
  
  if (p) {
    Tag *tag=new Tag(get_Tag(self->current_track));
    p->metadata_updated(tag);
    
    sp_session_player_load(sess, self->current_track);
    sp_session_player_play(sess, 1);		   
  }
}

void Spotify::end_of_track(sp_session *sess)
{
  Spotify *self=static_cast<Spotify*>(sp_session_userdata (sess));
  Spotify_Player *p;
  
  fprintf(stderr,"End of track\n");
  
  p = self->player;
  
  if (p) {
    p->end_of_track();
    p = NULL;
  }
  sp_session_player_unload(sess);
}

int Spotify::music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
  Spotify *self=static_cast<Spotify*>(sp_session_userdata (sess));
  Spotify_Player *p;
  
  //fprintf(stderr,"music_delivery %d %d %d %d\n",num_frames,format->sample_type,format->sample_rate,format->channels);

  p = self->player;
  
  if (p) {
    return p->music_delivery(format,frames,num_frames);
  }
  return 0;
}

void Spotify::OnTimeout() {
  int next_timeout;
  //printf("OnTimeout\n");
  do {
  pthread_mutex_lock(&g_notify_mutex);
    sp_session_process_events(sp, &next_timeout);
  pthread_mutex_unlock(&g_notify_mutex);
  } while (next_timeout==0);
  //printf("OnTimeout next %d\n",next_timeout);
  TimeoutMonitor::Schedule(next_timeout);
}

void Spotify::RunDeferred(){
  int next_timeout;
  //printf("RunDeferred\n");
  //TimeoutMonitor::Cancel();
  
  do {
  pthread_mutex_lock(&g_notify_mutex);
    sp_session_process_events(sp, &next_timeout);
  pthread_mutex_unlock(&g_notify_mutex);
  } while (next_timeout==0);
  
  //printf("RunDeferred next %d\n",next_timeout);
  TimeoutMonitor::Schedule(next_timeout);
}

void Spotify::notify_main_thread(sp_session *sess) {
  Spotify *self=static_cast<Spotify*>(sp_session_userdata (sess));
  //puts("notify_main_thread");
  self->DeferredMonitor::Schedule();
}
extern Instance *instance;

Spotify *Spotify::Create()  {
  Spotify *s;
  
  EventLoop *loop = instance->event_loop;
  puts("input_spotify_init new Spotify");
  s = new Spotify(*loop);
  return s;
}

void Spotify::message_to_user(sp_session *session, const char *message) {
  fprintf(stderr,"Message %s",message);
  
}

void Spotify::log_message(sp_session *session, const char *data) {
  fprintf(stderr,"Log %s",data);
}

int Spotify::getPlaylistSize() {
  sp_playlistcontainer *pc = sp_session_playlistcontainer(sp);
  return sp_playlistcontainer_num_playlists(pc);
} 

sp_playlist_type Spotify::getPlaylist(int i, std::string &name, std::string &url) {
		sp_playlistcontainer *pc = sp_session_playlistcontainer(sp);
    enum sp_playlist_type type = sp_playlistcontainer_playlist_type(pc, i);
    switch (type) {
		case SP_PLAYLIST_TYPE_PLAYLIST:
      {
        char uri_str[200],url_str[200];
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
        sp_link *link =	sp_link_create_from_playlist (pl);
        sp_link_as_string(link,uri_str,200);
        uri2url(uri_str,url_str);
        
        url=  std::string(url_str);
        name = std::string(sp_playlist_name(pl));
      }
      break;
    case SP_PLAYLIST_TYPE_START_FOLDER:
    case SP_PLAYLIST_TYPE_END_FOLDER: 
      {
        char dir_name[200];
        sp_playlistcontainer_playlist_folder_name(pc, i, dir_name, sizeof(dir_name));
        name = std::string(dir_name);
      }
      break;
    default:
      name="-";
    }
    return type;   
   
} 

