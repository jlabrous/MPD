/* the Music Player Daemon (MPD)
 * (c)2003-2004 by Warren Dukes (shank@mercury.chem.pitt.edu)
 * This project's homepage is: http://www.musicpd.org
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "interface.h"
#include "command.h"
#include "playlist.h"
#include "directory.h"
#include "tables.h"
#include "player.h"
#include "listen.h"
#include "conf.h"
#include "path.h"
#include "playerData.h"
#include "stats.h"
#include "sig_handlers.h"
#include "audio.h"
#include "volume.h"
#include "log.h"
#include "permission.h"
#include "replayGain.h"
#include "../config.h"

#include <stdio.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#define SYSTEM_CONFIG_FILE_LOCATION	"/etc/mpd.conf"
#define USER_CONFIG_FILE_LOCATION	"/.mpdconf"

typedef struct _Options {
        char * portStr;
        char * musicDirArg;
        char * playlistDirArg;
        char * logFile;
        char * errorFile;
        char * usr;
        char * dbFile;
        int daemon;
        int createDB;
	int onlyCreateDB;
	int onlyUpdateDB;
} Options;

void usage(char * argv[]) {
        ERROR("usage:\n");
        ERROR("   %s [options] <port> <music dir> <playlist dir> <log file> <error file>\n",argv[0]);
        ERROR("   %s [options] <conf file>\n",argv[0]);
        ERROR("   %s [options]   (searches for ~%s then %s)\n",
                        argv[0],USER_CONFIG_FILE_LOCATION,
                        SYSTEM_CONFIG_FILE_LOCATION);
        ERROR("\n");
        ERROR("options:\n");
        ERROR("   --help             this usage statement\n");
        ERROR("   --no-daemon        don't detach from console\n");
        ERROR("   --create-db        force (re)creation database\n");
        ERROR("   --only-create-db   create database and exit\n");
        ERROR("   --only-update-db   create database and exit\n");
        ERROR("   --no-create-db     don't create database\n");
        ERROR("   --verbose          verbose logging\n");
        ERROR("   --version          prints version information\n");
}

void version() {
        ERROR("mpd (MPD: Music Player Daemon) %s\n",VERSION);
        ERROR("\n");
        ERROR("Copyright (C) 2003 Warren Dukes <shank@mercury.chem.pitt.edu>\n");
        ERROR("This is free software; see the source for copying conditions.  There is NO\n");
        ERROR("warranty; not even MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
        ERROR("\n");
        ERROR("Supported formats:\n");

#ifdef HAVE_AUDIOFILE
        ERROR(" wav");
#endif
#ifdef HAVE_MAD
        ERROR(" mp3");
#endif
#ifdef HAVE_FAAD
        ERROR(" mp4 aac");
#endif
#ifdef HAVE_FLAC
        ERROR(" flac");
#endif
#ifdef HAVE_OGG
        ERROR(" ogg");
#endif
        ERROR("\n");
}

void parseOptions(int argc, char ** argv, Options * options) {
        int argcLeft = argc;

        options->usr = NULL;
        options->daemon = 1;
        options->createDB = 0;
        options->onlyCreateDB = 0;
        options->onlyUpdateDB = 0;
        options->dbFile = NULL;

        if(argc>1) {
                int i = 1;
                while(i<argc) {
                        if(strncmp(argv[i],"--",2)==0) {
                                if(strcmp(argv[i],"--help")==0) {
                                        usage(argv);
                                        exit(EXIT_SUCCESS);
                                }
                                else if(strcmp(argv[i],"--no-daemon")==0) {
                                        options->daemon = 0;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--create-db")==0) {
                                        options->createDB = 1;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--only-create-db")==0) {
                                        options->onlyCreateDB = 1;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--only-update-db")==0) {
                                        options->onlyUpdateDB = 1;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--no-create-db")==0) {
                                        options->createDB = -1;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--verbose")==0) {
                                        logLevel = LOG_LEVEL_DEBUG;
                                        argcLeft--;
                                }
                                else if(strcmp(argv[i],"--version")==0) {
                                        version();
                                        exit(EXIT_SUCCESS);
                                }
                                else {
                                        myfprintf(stderr,"unknown command line option: %s\n",argv[i]);
                                        exit(EXIT_FAILURE);
                                }
                        }
                        else break;
                        i++;
                }
        }

        if(argcLeft==6) {
                options->portStr = argv[argc-5];
                options->musicDirArg = argv[argc-4];
                options->playlistDirArg = argv[argc-3];
                options->logFile = argv[argc-2];
                options->errorFile = argv[argc-1];
                return;
        }
        else if(argcLeft<=2) {
                char ** conf = NULL;
                if(argcLeft==2) conf = readConf(argv[argc-1]);
                if(argcLeft==1) {
                        FILE * fp;
                        char * homedir = getenv("HOME");
                        char userfile[MAXPATHLEN+1] = "";
                        if(homedir && (strlen(homedir)+
                                                strlen(USER_CONFIG_FILE_LOCATION)) <
                                        MAXPATHLEN) {
                                strcpy(userfile,homedir);
                                strcat(userfile,USER_CONFIG_FILE_LOCATION);
                        }
                        if(strlen(userfile) && (fp=fopen(userfile,"r"))) {
                                fclose(fp);
                                conf = readConf(userfile);
                        }
                        else if((fp=fopen(SYSTEM_CONFIG_FILE_LOCATION,"r"))) {
                                fclose(fp);
                                conf = readConf(SYSTEM_CONFIG_FILE_LOCATION);
                        }
                }
                if(conf) {
                        options->portStr = conf[CONF_PORT];
                        options->musicDirArg = conf[CONF_MUSIC_DIRECTORY];
                        options->playlistDirArg = conf[CONF_PLAYLIST_DIRECTORY];
                        options->logFile = conf[CONF_LOG_FILE];
                        options->errorFile = conf[CONF_ERROR_FILE];
                        options->usr = conf[CONF_USER];
                        if(conf[CONF_DB_FILE]) {
                                options->dbFile = conf[CONF_DB_FILE];
                        }
                        return;
                }
        }

        usage(argv);
        exit(EXIT_FAILURE);
}

int main(int argc, char * argv[]) {
        int port, uid, gid;
        FILE * out;
        FILE * err;
        Options options;
        int i;

        for(i=0;i<FD_SETSIZE;i++) {
                switch(i) {
                        case STDIN_FILENO:
                        case STDOUT_FILENO:
                        case STDERR_FILENO:
                                break;
                        default:
                                close(i);
                }
        }

        initConf();

        parseOptions(argc,argv,&options);

        initStats();
        initLog();

        if((port = atoi(options.portStr))<0) {
                ERROR("problem with port number\n");
                return EXIT_FAILURE;
        }

        if(!options.onlyCreateDB && !options.onlyUpdateDB &&
			(listenSocket = establish(port))<0) 
	{
                ERROR("error binding port\n");
                return EXIT_FAILURE;
        }

        /*
         * lose privileges as early as possible
         */

        /* change uid */
        if (options.usr && strlen(options.usr)) {
#ifdef _BSD_SOURCE
                gid_t gid_list[NGROUPS_MAX];
#endif

                /* get uid */
                struct passwd * userpwd;
                if ((userpwd = getpwnam(options.usr)) == NULL) {
                        ERROR("no such user: %s\n", options.usr);
                        return EXIT_FAILURE;
                }
                uid = userpwd->pw_uid;
                gid = userpwd->pw_gid;

                if(setgid(gid) == -1) {
                        ERROR("cannot setgid of user %s: %s\n", options.usr,
                                        strerror(errno));
                        return EXIT_FAILURE;
                }

#ifdef _BSD_SOURCE
                /* init suplementary groups 
                 * (must be done before we change our uid)
                 */
                if (initgroups(options.usr, gid) == -1) {
                        ERROR("cannot init suplementary groups "
                                        "of user %s: %s\n", options.usr, 
                                        strerror(errno));
                }
                else if(getgroups(NGROUPS_MAX, gid_list) == -1) {
                        ERROR("cannot get groups "
                                        "of user %s: %s\n", options.usr, 
                                        strerror(errno));
                        return EXIT_FAILURE;
                }
                else if(setgroups(NGROUPS_MAX, gid_list) == -1) {
                        ERROR("cannot set groups "
                                        "of user %s: %s\n", options.usr, 
                                        strerror(errno));
                        return EXIT_FAILURE;
                }
#endif

                /* set uid */
                if (setuid(uid) == -1) {
                        ERROR("cannot change to uid of user "
                                        "%s: %s\n", options.usr, 
                                        strerror(errno));
                        return EXIT_FAILURE;
                }

        }

        if(NULL==(out=fopen(options.logFile,"a"))) {
                ERROR("problem opening file \"%s\" for writing\n",
                                options.logFile);
                return EXIT_FAILURE;
        }

        if(NULL==(err=fopen(options.errorFile,"a"))) {
                ERROR("problem opening file \"%s\" for writing\n",
                                options.errorFile);
                return EXIT_FAILURE;
        }

	initPaths(options.playlistDirArg,options.musicDirArg);
	initPermissions();
        initReplayGainState();

        initTables();
        initPlaylist();

        if(!options.dbFile) directory_db = strdup(rpp2app(".mpddb"));
        else directory_db = strdup(options.dbFile);

        if(options.createDB>0 || options.onlyCreateDB || readDirectoryDB()<0) 
	{
                if(options.createDB<0) {
                        ERROR("can't open db file and using \"--no-create-db\""
                                        " command line option\n");
			ERROR("try running \"%s --only-create-db\"\n",
					argv[0]);
                        exit(EXIT_FAILURE);
                }
                initMp3Directory();
                if(writeDirectoryDB()<0) {
                        ERROR("problem opening db for reading or writing\n");
                        exit(EXIT_FAILURE);
                }
		if(options.onlyCreateDB) exit(EXIT_SUCCESS);
        }
	if(options.onlyUpdateDB) {
		if(updateMp3Directory(stderr)<0) exit(EXIT_FAILURE);
		exit(EXIT_SUCCESS);
	}

        initCommands();
        initAudioDriver();
        initPlayerData();
        initVolume();
        initInterfaces();

        close(STDIN_FILENO);
        if(options.daemon) {
                int pid;

                fflush(NULL);
                pid = fork();
                if(pid>0) _exit(EXIT_SUCCESS);
                else if(pid<0) {
                        ERROR("problems fork'ing for daemon!\n");
                        exit(EXIT_FAILURE);
                }

                if(chdir("/")<0) {
                        ERROR("problems changing to root directory\n");
                        exit(EXIT_FAILURE);
                }

                if(setsid()<0) {
                        ERROR("problems setsid'ing\n");
                        exit(EXIT_FAILURE);
                }

                if(dup2(fileno(out),STDOUT_FILENO)<0) {
                        myfprintf(err,"problems dup2 stdout : %s\n",
                                        strerror(errno));
                        exit(EXIT_FAILURE);
                }

                if(dup2(fileno(err),STDERR_FILENO)<0) {
                        myfprintf(err,"problems dup2 stderr : %s\n",
                                        strerror(errno));
                        exit(EXIT_FAILURE);
                }

                myfprintfStdLogMode(out,err,options.logFile,options.errorFile);

                fflush(NULL);
                pid = fork();
                if(pid>0) _exit(EXIT_SUCCESS);
                else if(pid<0) {
                        ERROR("problems fork'ing for daemon!\n");
                        exit(EXIT_FAILURE);
                }
        }
        else {
                fclose(out);
                fclose(err);
        }

        /* lets redirect stdin to dev null as a work around for libao bug */
        {
                int fd = open("/dev/null",O_RDONLY);
                if(fd<0) {
                        ERROR("not able to open /dev/null to redirect stdin: "
                                        "%s\n",strerror(errno));
                        exit(EXIT_FAILURE);
                }
                if(dup2(fd,STDIN_FILENO)<0) {
                        ERROR("problems dup2's stdin for redirection: "
                                        "%s\n",strerror(errno));
                        exit(EXIT_FAILURE);
                }
        }

        openVolumeDevice();
        initSigHandlers();
        readPlaylistState();

        while(COMMAND_RETURN_KILL!=doIOForInterfaces()) {
                syncPlayerAndPlaylist();
                closeOldInterfaces();
		if(COMMAND_RETURN_KILL==handlePendingSignals()) break;
		readDirectoryDBIfUpdateIsFinished();
        }

        savePlaylistState();
        playerKill();

        freeAllInterfaces();
        close(listenSocket);
        closeMp3Directory();
        closeTables();
        finishPlaylist();
        freePlayerData();
        finishAudioDriver();
        finishVolume();
	finishPaths();
	finishPermissions();
        finishCommands();

        return EXIT_SUCCESS;
}
/* vim:set shiftwidth=4 tabstop=8 expandtab: */
