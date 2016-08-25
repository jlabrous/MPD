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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "SpotifyDatabasePlugin.hxx"
#include "db/Interface.hxx"
#include "db/DatabasePlugin.hxx"
#include "db/Selection.hxx"
#include "db/DatabaseError.hxx"
#include "db/LightDirectory.hxx"
#include "db/PlaylistInfo.hxx"
#include "db/LightSong.hxx"
#include "db/Stats.hxx"
#include "config/Block.hxx"
#include "tag/TagBuilder.hxx"
#include "tag/TagTable.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"
#include "fs/Traits.hxx"
#include "Log.hxx"
#include "SongFilter.hxx"

#include <string>
#include <vector>
#include <set>

#include <assert.h>
#include <string.h>

#include <stdio.h>

#include "lib/spotify/Spotify.hxx"

class SpotifyDatabase : public Database {

public:
	SpotifyDatabase():Database(spotify_db_plugin) {}

	static Database *Create(EventLoop &loop, DatabaseListener &listener,
				const ConfigBlock &param,
				Error &error);

	virtual void Open() override;
	virtual void Close() override;
	virtual const LightSong *GetSong(const char *uri_utf8) const override;
	void ReturnSong(const LightSong *song) const override;

	virtual bool Visit(const DatabaseSelection &selection,
			   VisitDirectory visit_directory,
			   VisitSong visit_song,
			   VisitPlaylist visit_playlist,
			   Error &error) const override;

	virtual bool VisitUniqueTags(const DatabaseSelection &selection,
				     TagType tag_type, tag_mask_t group_mask,
				     VisitTag visit_tag,
				     Error &error) const override;

	virtual bool GetStats(const DatabaseSelection &selection,
			      DatabaseStats &stats,
			      Error &error) const override;
	time_t GetUpdateStamp() const override {
		return 0;
	}

protected:
	bool Configure(const ConfigBlock &param, Error &error);

private:
};

Database *
SpotifyDatabase::Create(gcc_unused EventLoop &loop,
		     gcc_unused DatabaseListener &listener,
		     const ConfigBlock &param, Error &error)
{
	SpotifyDatabase *db = new SpotifyDatabase();
	if (!db->Configure(param, error)) {
		delete db;
		return nullptr;
	}
    fprintf(stderr,"JLB:SpotifyDatabase::Create\n");

	return db;
}

inline bool
SpotifyDatabase::Configure(const ConfigBlock &, Error &)
{
    fprintf(stderr,"JLB:SpotifyDatabase::Configure\n");
	return true;
}

void
SpotifyDatabase::Open()
{
    fprintf(stderr,"JLB:SpotifyDatabase::Open\n");
}

void
SpotifyDatabase::Close()
{
    fprintf(stderr,"JLB:SpotifyDatabase::Close\n");
}

void
SpotifyDatabase::ReturnSong(const LightSong *_song) const
{
    fprintf(stderr,"JLB:SpotifyDatabase::ReturnSong\n");
	assert(_song != nullptr);

	delete _song;
}

// Get song info by path. We can receive either the id path, or the titles
// one
const LightSong *
SpotifyDatabase::GetSong(const char *uri) const
{
    fprintf(stderr,"JLB:SpotifyDatabase::GetSong\n");
	LightSong *song = new LightSong();
	return song; 
}

/**
 * Double-quote a string, adding internal backslash escaping.
 */
static void
dquote(std::string &out, const char *in)
{
	out.push_back('"');

	for (; *in != 0; ++in) {
		switch(*in) {
		case '\\':
		case '"':
			out.push_back('\\');
			break;
		}

		out.push_back(*in);
	}

	out.push_back('"');
}

bool
SpotifyDatabase::Visit(const DatabaseSelection &selection,
		    VisitDirectory visit_directory,
		    VisitSong visit_song,
		    VisitPlaylist visit_playlist,
		    Error &error) const
{
  std::string sp_url;
  std::string sp_path="/";
  std::string mpd_path = "/";
  if (selection.uri != "/")
    mpd_path = "/"+selection.uri;
    
  int len = spotify->getPlaylistSize();
  
  for (int i = 0; i< len;i++) {
    std::string name;
    sp_playlist_type type=spotify->getPlaylist(i, name,sp_url);
    switch(type) {
    case SP_PLAYLIST_TYPE_PLAYLIST:
      if (sp_path == mpd_path) {
        visit_playlist( PlaylistInfo(name.c_str(),0),LightDirectory((sp_url+"#").c_str(),0),error);
			  fprintf(stderr,"Add <%s> <%s> <%s>\n", mpd_path.c_str(),sp_path.c_str(),name.c_str());
      } else {
			  fprintf(stderr,"Skip <%s> <%s> <%s>\n", mpd_path.c_str(),sp_path.c_str(),name.c_str());
      }
      break;
   	case SP_PLAYLIST_TYPE_START_FOLDER: 
      { 
        std::string new_path;                                                                                  
        if (sp_path == "/")
          new_path = sp_path + name;
        else
          new_path = sp_path + "/" + name;
          
        if (sp_path == mpd_path) {
          visit_directory( LightDirectory(new_path.c_str()+1,0),error);
          fprintf(stderr,"Add <%s>/\n", new_path.c_str());
        } else {
          fprintf(stderr,"Skip <%s>/\n", new_path.c_str());
        }
 			  sp_path = new_path;
      }
      break;
  	case SP_PLAYLIST_TYPE_END_FOLDER:
      {
        int pos = sp_path.find_last_of("/"); 
        if ((pos == std::string::npos) || (pos == 0 ))
          sp_path = "/";
        else
          sp_path = sp_path.substr(0,pos);
			fprintf(stderr,"<%s>\n", sp_path.c_str());
      }
      break;
    }
  }
  return true;
}

bool
SpotifyDatabase::VisitUniqueTags(const DatabaseSelection &selection,
			      TagType tag, gcc_unused uint32_t group_mask,
			      VisitTag visit_tag,
			      Error &error) const
{
    fprintf(stderr,"JLB:SpotifyDatabase::VisitUniqueTags\n");
	return true;
}

bool
SpotifyDatabase::GetStats(const DatabaseSelection &,
		       DatabaseStats &stats, Error &) const
{
    fprintf(stderr,"JLB:SpotifyDatabase::GetStats\n");
	return true;
}

const DatabasePlugin spotify_db_plugin = {
	"spotify",
	0,
	SpotifyDatabase::Create,
};
