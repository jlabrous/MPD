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
#include "CddbPlaylistPlugin.hxx"
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
#include <cdio/paranoia.h>
#include <cdio/cd_types.h>
#include <string.h>
#include <cddb/cddb.h>


#include <iostream>

static constexpr Domain cddb_domain("cddb");

static cddb_disc_t *get_cddb(CdIo_t *cdio) {
  cddb_conn_t *conn = NULL;   /* libcddb connection structure */
  cddb_disc_t *disc = NULL;   /* libcddb disc structure */
  cddb_track_t *track;
  track_t cnt, t;             /* track counters */
  lba_t lba;                  /* Logical Block Address */
  
  conn = cddb_new();
  cddb_cache_enable(conn);

  disc = cddb_disc_new();
  
  lba = cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK);
  cddb_disc_set_length(disc, FRAMES_TO_SECONDS(lba));
  printf("cddb_disc_set_length(%d)\n",FRAMES_TO_SECONDS(lba));
  cnt = cdio_get_num_tracks(cdio);
  printf("cdio_get_num_tracks %d\n",cnt);
  for (t = 1; t <= cnt; t++) {
    lba = cdio_get_track_lba(cdio, t);
    track = cddb_track_new();
    cddb_track_set_frame_offset(track, lba);
    printf("cddb_track_set_frame_offset(%d)\n",lba);
    cddb_disc_add_track(disc, track);
  }
  
  cddb_disc_calc_discid(disc);
  printf("CD disc ID is %08x\n", cddb_disc_get_discid(disc));
  
  
  
  if (cddb_query(conn, disc)>0) {
    printf("Found\n");
    if (cddb_read(conn, disc)) {
    printf("Found\n");
      return disc;
    }
  } 
  
    printf("Not Found\n");
  cddb_disc_destroy(disc);
  return NULL;
}

static SongEnumerator *
cddb_open_cdrom(const char *url,
			    gcc_unused Mutex &mutex, gcc_unused Cond &cond)
{
  std::forward_list<DetachedSong> songs;
  CdIo_t *cdio;
  cdio = cdio_open(NULL, DRIVER_UNKNOWN);
  
  int allow_cdtext=0;
  int allow_cddb=0;
  
  printf("URL %s\n",url);
  
  if (!strncmp(url,"cddb://",7)) {
    allow_cddb=1;
  } else if (!strncmp(url,"cdtext://",7)) {
    allow_cdtext=1;
  } else {
    allow_cdtext=allow_cddb=1;
  }
  
  track_t track,first_track, last_track;
  
  first_track = cdio_get_first_track_num(cdio);
  last_track = cdio_get_num_tracks(cdio);
  
  cdtext_t *cdtext=NULL;
  cddb_disc_t *cddb=NULL;
  cddb_track_t *cddb_track=NULL;
  char *album=NULL;
  
  if (allow_cdtext && (cdtext=cdio_get_cdtext(cdio,0))) {
    if (cdtext->field[CDTEXT_TITLE]) {
      album = strdup(cdtext->field[CDTEXT_TITLE]);
      allow_cddb=0;
    } else {
      allow_cdtext=0;
    }
  } 
  
  if (allow_cddb && (cddb = get_cddb(cdio))) {
    album = strdup(cddb_disc_get_title(cddb));
    cddb_track=cddb_disc_get_track_first(cddb);
  } 
  
  for ( track = first_track; track <= last_track; track++ ) {
    std::string uri;
    lsn_t lsn_from,lsn_to;
    printf("Track %d\n",track);
    uri="cdda:///"+std::to_string(track);
    std::cout << uri << std::endl;
    TagBuilder t;
    lsn_from = cdio_get_track_lsn(cdio, track);
    lsn_to = cdio_get_track_last_lsn(cdio, track);
    
    if (allow_cdtext && (cdtext=cdio_get_cdtext(cdio,track))) {
      
      if (cdtext->field[CDTEXT_COMPOSER]) {
        t.AddItem(TAG_COMPOSER, cdtext->field[CDTEXT_COMPOSER]);
      }

      if (cdtext->field[CDTEXT_DISCID]) {
        t.AddItem(TAG_DISC, cdtext->field[CDTEXT_DISCID]);
      }

      if (cdtext->field[CDTEXT_GENRE]) {
        t.AddItem(TAG_GENRE, cdtext->field[CDTEXT_GENRE]);
      }

      if (cdtext->field[CDTEXT_MESSAGE]) {
        t.AddItem(TAG_COMMENT, cdtext->field[CDTEXT_MESSAGE]);
      }

      if (cdtext->field[CDTEXT_PERFORMER]) {
        t.AddItem(TAG_PERFORMER, cdtext->field[CDTEXT_PERFORMER]);
        t.AddItem(TAG_ARTIST, cdtext->field[CDTEXT_PERFORMER]);
      }

      if (cdtext->field[CDTEXT_TITLE]) {
        t.AddItem(TAG_NAME, cdtext->field[CDTEXT_TITLE]);
        t.AddItem(TAG_TITLE, cdtext->field[CDTEXT_TITLE]);
      } else {
        char buffer[256];
	sprintf(buffer,"Track_%d",track);
	t.AddItem(TAG_NAME, buffer);
      }
      
      if (album) {
        t.AddItem(TAG_ALBUM, album);
      }
      
    } else if (cddb_track) {
      if(cddb_track_get_artist(cddb_track)) {
        t.AddItem(TAG_PERFORMER, cddb_track_get_artist(cddb_track));
        t.AddItem(TAG_ARTIST, cddb_track_get_artist(cddb_track));
      }
      if (cddb_track_get_title(cddb_track)) {
	t.AddItem(TAG_NAME, cddb_track_get_title(cddb_track));
	t.AddItem(TAG_TITLE, cddb_track_get_title(cddb_track));
      } else {
        char buffer[256];
	sprintf(buffer,"Track_%d",track);
	t.AddItem(TAG_NAME, buffer);
      }
      
      if(cddb_track_get_ext_data(cddb_track)) {
	t.AddItem(TAG_COMMENT, cddb_track_get_ext_data(cddb_track));

      }
      
      if (album) {
        t.AddItem(TAG_ALBUM, album);
      }
      cddb_track=cddb_disc_get_track_next(cddb);
      
    } else {
      char buffer[256];
      sprintf(buffer,"Track_%d",track);
      t.AddItem(TAG_NAME, buffer);
    }
    char buffer[256];
    sprintf(buffer,"%d",track);
    t.AddItem(TAG_TRACK, buffer);
    
    DetachedSong s(std::move(uri),t.Commit());
    s.SetEndTime(SongTime::FromScale((lsn_to-lsn_from),CDIO_CD_FRAMES_PER_SEC));
    songs.emplace_front(s);
  }
  
  if (album) free(album);
  if (cddb) cddb_disc_destroy(cddb);

  cdio_destroy(cdio);

  songs.reverse();
  return new MemorySongEnumerator(std::move(songs));
}

static const char *const cddb_suffixes[] = {
	"cdda",
	nullptr
};

static const char *const cddb_schemes[] = {
	"cdda",
	"cddb",
	"cdtext",
	nullptr
};

const struct playlist_plugin cddb_playlist_plugin = {
	"cddb",

	nullptr,
	nullptr,
	cddb_open_cdrom,
	nullptr,

	cddb_schemes,
	cddb_suffixes,
	nullptr,
};
