/*
   watchlog.c - main code module for watchlogd log processing daemon.

   Copyright (c) 1997  mike knerr <mk@metrotron.com>

   This file is part of the watchlogd package

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program in the file COPYING; if not write to
   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 
   02139, USA
 */

#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>

#include "log.h"
#include "logrotate.h"
#include "watchlog.h"

#define PIDFILE "/var/run/watchlogd.pid"

/* private: */

static BOOLEAN bDaemon = FALSE;
static BOOLEAN bTestMode = FALSE;
static BOOLEAN bVerboseMode = FALSE;

static void usage(char *);
static void version(void);
static BOOLEAN isMyName(char *);
static BOOLEAN isMyDaemonName(char *);
static BOOLEAN pidSave(void);
static int pidFetch(void);
static BOOLEAN pidIsRunning(void);
static BOOLEAN pidIsMeRunning(void);
static BOOLEAN pidErase(void);
static BOOLEAN isValidStartTime(char *);
static BOOLEAN isTimeToStart(char *);
static void shutdown(int);


BOOLEAN inDaemonMode(void)
{
    return bDaemon;
}

BOOLEAN inTestMode(void)
{
    return bTestMode;
}

BOOLEAN inVerboseMode(void)
{
    return bVerboseMode;
}


static void usage(char *szExecName)
{

    if (strncmp(szExecName, "watchlogd", 9) == 0) {

	fprintf(stderr, "usage: watchlogd [-DTuv] [-s {<hh:mm>|now}] [-i <hours>] [-f <statefile>]\n"
		"                 [<configfile>+]\n");


    } else if (strncmp(szExecName, "watchlog", 8) == 0) {

	fprintf(stderr, "usage: watchlog [-DTuv] [-s {<hh:mm>|now}] [-i <hours>] [-f <statefile>]\n"
		"                 [<configfile>+]\n");


    } else {
	/* not called by us -- so get outa here */
	exit(1);
    }

    fprintf(stderr, "\n%s " VERSION "-" PATCHLEVEL "  " COPYRIGHT "\n", szExecName);
    fprintf(stderr, "May be freely redistributed under the"
	    " terms of the GNU Public License\n\n");

    exit(1);
}

static void version(void)
{
    fprintf(stderr, "watchlogd " VERSION "-" PATCHLEVEL "\n");
    exit(1);
}

static BOOLEAN isMyDaemonName(char *szName)
{

    int iReturn = FALSE;

    if (strncmp(szName, "watchlogd", 9) == 0) {
	iReturn = TRUE;
    } else if (strncmp(szName, "/usr/sbin/watchlogd", 19) == 0) {
	iReturn = TRUE;
    } else if (strncmp(szName, "./watchlogd", 11) == 0) {
	iReturn = TRUE;
    }
    return iReturn;
}


static BOOLEAN isMyName(char *szName)
{

    int iReturn = FALSE;

    if (isMyDaemonName(szName)) {
	return TRUE;
    } else if (strncmp(szName, "watchlog", 8) == 0) {
	iReturn = TRUE;
    } else if (strncmp(szName, "/usr/sbin/watchlog", 18) == 0) {
	iReturn = TRUE;
    } else if (strncmp(szName, "./watchlog", 10) == 0) {
	iReturn = TRUE;
    }
    return iReturn;
}


static BOOLEAN pidSave(void)
{

    FILE *f;
    int fd;
    int pid;

    if (((fd = open(PIDFILE, O_RDWR | O_CREAT, 0644)) == -1)
	|| ((f = fdopen(fd, "r+")) == NULL)) {

	message(MSG_ERR, "cannot open or create %s\n", PIDFILE);
	return FALSE;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
	message(MSG_ERR, "cannot lock %s\n", PIDFILE);
	fclose(f);
	return FALSE;
    }
    pid = getpid();

    message(MSG_DEBUG, "writing out pid %d\n", pid);

    if (!fprintf(f, "%d\n", pid)) {
	message(MSG_ERR, "cannot write to %s (%s)\n", PIDFILE, strerror(errno));
	close(fd);
	return FALSE;
    }
    fflush(f);

    if (flock(fd, LOCK_UN) == -1) {
	message(MSG_ERR, "cannot unlock %s (%s)\n", PIDFILE, strerror(errno));
	close(fd);
	return FALSE;
    }
    close(fd);

    return TRUE;
}


static int pidFetch(void)
{

/* either return the value in the pidfile or return 0 if an error */

    FILE *f;
    int pid;

    if (!(f = fopen(PIDFILE, "r"))) {
	message(MSG_DEBUG, "cannot open %s\n", PIDFILE);
	return 0;
    }
    fscanf(f, "%d", &pid);
    fclose(f);

    message(MSG_DEBUG, "%s contains [%d] : my pid [%d]\n", PIDFILE, pid, getpid());

    return pid;
}


static BOOLEAN pidErase(void)
{

    if (unlink(PIDFILE) == 0) {
	return TRUE;
    }
    return FALSE;
}


static BOOLEAN pidIsMeRunning(void)
{

    int pid = pidFetch();

    /* see if my pidfile is me? */

    if (pid == getpid()) {
	message(MSG_DEBUG, "pid [%d] is me!\n", pid);
	return TRUE;
    }
    message(MSG_DEBUG, "pid [%d] is NOT me\n", pid);

    return FALSE;
}


static BOOLEAN pidIsRunning(void)
{

/* returns TRUE if there is general notion
   of the saved pid representing a running process */

    int pid = pidFetch();

    /* pidfile is gone or ... ? */

    if (pid == 0) {
	return FALSE;
    }
    /* is saved pid me */

    if (pidIsMeRunning()) {
	return TRUE;
    }
    /* DONT accidentally kill off every process or all in process group
     * attempting to do this test with pid <= -1 
     * in case pidfile was hacked
     */

    if (pid > 0) {
	if ((kill(pid, 0) == -1) && (errno == ESRCH)) {
	    message(MSG_DEBUG, "fetched pid [%d] is not a running process\n", pid);
	    return FALSE;
	} else {
	    message(MSG_DEBUG, "fetched pid [%d] is a running process\n", pid);
	}
    }
    return TRUE;
}



static void shutdown(int i)
{
    message(MSG_TEST, "Shutting down...\n");

    if (bDaemon) {

	/* do clean up */

	pidErase();
    }
    logSyslog(LOG_NOTICE, "Shutting down...\n");

    exit(i);
}



static BOOLEAN isValidStartTime(char *szTime)
{

    int iReturn = TRUE;

/* now is a valid start time! */

    if (strncmp(szTime, "now", 3) == 0) {
	return iReturn;
    }
    if (strlen(szTime) != 5) {
	iReturn = FALSE;
    }
    if (szTime[2] != ':') {
	iReturn = FALSE;
    }
    if (
	   (!isdigit(szTime[0])) ||
	   (!isdigit(szTime[1])) ||
	   (!isdigit(szTime[3])) ||
	   (!isdigit(szTime[4]))
	) {
	iReturn = FALSE;
    }
    if (
	   (szTime[0] > '2') ||
	   (szTime[3] > '5')
	) {
	iReturn = FALSE;
    }
    if (
	   (szTime[0] == '2') &&
	   (szTime[1] > '3')
	) {
	iReturn = FALSE;
    }
    if (iReturn == FALSE) {

	message(MSG_ERR, "Invalid initial processing time\n");
    }
    return iReturn;
}


static BOOLEAN isValidInterval(int i)
{

    if (i < 1) {
	message(MSG_ERR, "Invalid interval %d: Cannot run in less than hourly intervals\n", i);
	return FALSE;
    }
    if (i > 24) {
	message(MSG_ERR, "Invalid interval %d: Must run at least once a day\n", i);
	return FALSE;
    }
    return TRUE;
}


static BOOLEAN isTimeToStart(char *szT)
{

    time_t tNow;
    struct tm tmNow;
    char t[6];
    int iHour;
    int iMin;

/* check for valid start time anyway */

    if (isValidStartTime(szT) == FALSE) {

	message(MSG_ERR, "isTimeToStart got invalid start time\n");
	return FALSE;
    }
/* if starting immediately then don't wait */

    if (strncmp(szT, "now", 3) == 0) {
	message(MSG_TEST, "Initial starting time is now!\n");
	return TRUE;
    }
    tNow = time(NULL);
    tmNow = *localtime(&tNow);

    strncpy(t, szT, 5);
    t[5] = '\0';

    iHour = atoi(strtok(t, ":"));
    iMin = atoi(strtok(NULL, ":"));

    message(MSG_TEST, "check if time to start...\n");
    message(MSG_TEST, "time now: %d:%d \n", tmNow.tm_hour, tmNow.tm_min);
    message(MSG_TEST, "time to start: %d:%d \n", iHour, iMin);

    if ((iHour == tmNow.tm_hour) && (iMin == tmNow.tm_min)) {
	message(MSG_TEST, "ok to start\n");
	return TRUE;
    }
    message(MSG_TEST, "not yet...\n");
    return FALSE;
}



int main(int argc, char **argv)
{

    /* pass these defaults some of which will be overriden during parse */

    logInfo defConfig =
    {
	NULL,			/* */
	ROT_SIZE,		/* checklog filesize */
	1024 * 1024,		/* size is 1MB */
	0,			/* */
	0,			/* */
	NULL,			/* */
	NULL,			/* */
	NULL,			/* */
	0,			/* */
	NO_MODE,		/* createMode */
	NO_UID,			/* createUid  */
	NO_GID,			/* createGid  */
	FALSE,			/* mail removed log */
	FALSE,			/* mail error log */
	TRUE,			/* bCheckLog */
	FALSE			/* bScanLog */
    };


    int numLogs = 0, numStates = 0;
    logInfo *logs = NULL;
    logState *states = NULL;

    char *stateFile = DEFAULT_STATEFILE;
    char *szDefaultConfigFile = DEFAULTCONFFILE;
    char *szDefaultConfigDir = DEFAULTCONFDIR;

    int iInterval = 24;		/* interval to run in hours */
    int bDidFirstProcessing = FALSE;	/* so no reread config */
    char *szStartTime = "now";	/* time to start first processing */

    int i;
    int iHr;			/* hour counter */
    int rc = 0;
    int iOption, long_index;

/* will add:
 * -c check interval (for rotation)
 * -s scan interval (for events)
 * (both override -i)
 * -r {on|off} process rotations?
 * -e {on|off} scan events?
 */

    struct option options[] =
    {
	{"daemon", 0, 0, 'd'},
	{"debug-mode", 0, 0, 'D'},
	{"interval", 1, 0, 'i'},
	{"state-file", 1, 0, 'f'},
	{"start-at", 1, 0, 'a'},
	{"test-mode", 0, 0, 'T'},
	{"usage", 0, 0, '?'},
	{"verbose-mode", 0, 0, 'V'},
	{"version", 0, 0, 'v'},
	{0, 0, 0, 0}
    };


    /* dont like the idea of not running under one of our names */

    if (!isMyName(argv[0])) {
	message(MSG_ERR, "Imposter! %s\n", argv[0]);
	exit(1);
    }
    logSetLevel(MSG_NORMAL);

    if (isMyDaemonName(argv[0])) {

	logSetMyName("watchlogd");
    } else {
	logSetMyName("watchlog");
    }

    /* get parameters and set flags */

    while (1) {
	iOption = getopt_long(argc, argv, "DTVdf:i:a:uv?", options, &long_index);
	if (iOption == EOF)
	    break;

	switch ((char) iOption) {

	case 'f':
	    stateFile = optarg;
	    break;

	case 'd':
	    bDaemon = TRUE;
	    break;

	case 'D':
	    bTestMode = TRUE;
	    logSetLevel(MSG_DEBUG);
	    break;

	case 'i':
	    iInterval = atoi(optarg);
	    break;

	case 'a':
	    szStartTime = optarg;
	    break;

	case 'T':
	    bTestMode = TRUE;
	    logSetLevel(MSG_TEST);
	    break;

	case 'u':
	    usage(argv[0]);
	    break;

	case 'V':
	    logSetLevel(MSG_TEST);
	    bTestMode = FALSE;
	    bVerboseMode = TRUE;
	    break;

	case 'v':
	    version();
	    break;

	case '?':
	    usage(argv[0]);
	    break;
	}
    }


/* see if started as watchlogd 
 * and if so then run as daemon
 */

    if (isMyDaemonName(argv[0])) {

	bDaemon = TRUE;

    }
/* check out validity of daemon option parameters before continuing */

    /* if we are daemon may have an optional start time or interval */

    if (bDaemon == TRUE) {

	message(MSG_TEST, "Daemon mode is on\n");
	message(MSG_TEST, "Processing interval is: %d\n", iInterval);


	if (isValidInterval(iInterval) == FALSE) {
	    exit(1);
	}
	if (isValidStartTime(szStartTime) == FALSE) {
	    exit(1);
	}
	message(MSG_TEST, "Initial processing time: %s\n", szStartTime);


    }				/*endif is daemon */
    /* if we will be a daemon then fork now */
    if (bDaemon) {

	if (!pidIsRunning()) {

	    message(MSG_TEST, "Starting watchlog daemon\n");

	    switch (fork()) {

	    case -1:
		message(MSG_ERR, "Error forking daemon [%d]\n", getpid());
		exit(0);
		break;

	    case 0:
		/* this process is the child
		 * so continue to run
		 */
		message(MSG_TEST, "daemon fork is OK! [%d]\n", getpid());
		(void) setsid();

		logSyslog(LOG_NOTICE, "Starting Up... (watchlogd-" VERSION
			  "-" PATCHLEVEL ")\n");
		if (!pidSave()) {
		    shutdown(1);
		}
		break;

	    default:
		/* this process is the parent process
		 * so exit thereby letting only the 
		 * child continue to run as the daemon
		 */

		message(MSG_TEST, "daemon parent is exiting [%d]\n", getpid());
		_exit(0);
	    }

	} else {

	    message(MSG_ERR, "Daemon is already running!\n");
	    logSyslog(LOG_NOTICE, "Exiting... Daemon is already running!\n");

	    exit(1);

	}

    }				/*endif if daemon mode */
/****** read log configurations *******/
    /*  check to see if a config file(s) was given */
    if (optind == argc) {

	/* if no config file given then attempt to use default config file */

	message(MSG_DEBUG, "szDefaultConfigFile: %s\n", szDefaultConfigFile);

	if (readConfigPath(szDefaultConfigFile, &defConfig, &logs, &numLogs)) {

	    /*
	     * if no default config file then attempt to use
	     * the default directory of config files
	     */

	    if (readConfigPath(szDefaultConfigDir, &defConfig, &logs, &numLogs)) {
		shutdown(1);
	    }
	}
    } else {

	/* otherwise a primary config file was given so attempt use it */

	while (optind < argc) {
	    if (readConfigPath(argv[optind], &defConfig, &logs, &numLogs)) {
		shutdown(1);
	    }
	    optind++;
	}
    }


    /* main server loop */
    while (1) {

	if (bDaemon) {

/* wait until time to start first processing */

	    if (bDidFirstProcessing == FALSE) {

		message(MSG_TEST, "Waiting to start initial processing at '%s'\n", szStartTime);
		sleep(1);

		/* hang out here until time to start initial processing */

		while (1) {

		    /* message(MSG_DEBUG, "Check start time in while(1) \n"); */

		    if (isTimeToStart(szStartTime)) {
			message(MSG_TEST, "Starting initial processing...\n");
			break;
		    }
		    sleep(60);
		}
	    }
	}			/* endif a daemon */
	if (readState(stateFile, &states, &numStates)) {
	    shutdown(1);
	}
	if (bDaemon) {
	    /* be a little paranoid */
	    if (!pidIsMeRunning()) {
		message(MSG_ERR, "Saved PID is NOT me!\n");
		logSyslog(LOG_ERR, "Cannot verify my PID!\n");
		shutdown(1);
	    }
	}
	message(MSG_TEST, "\nBegin checking %d logs\n", numLogs);
	logSyslog(LOG_INFO, "Processing logs...\n");

	for (i = 0; i < numLogs; i++) {
	    rc |= rotateLog(logs + i, &states, &numStates);
	}

	if (!bTestMode && writeState(stateFile, states, numStates)) {
	    shutdown(1);
	}
	bDidFirstProcessing = TRUE;


	if (bDaemon) {

	    /* now sleep for the interval until next time to run through */

	    message(MSG_TEST, "\nWaiting for %d hours to start next log processing\n", iInterval);

	    for (iHr = 1; iHr <= iInterval; iHr++) {

		/* can do stuff here every hour */

		if (!pidIsMeRunning()) {

		    message(MSG_ERR, "Saved PID is NOT me!\n");
		    logSyslog(LOG_ERR, "Cannot verify my PID!\n");
		    shutdown(1);
		}
		sleep(60 * 60);

	    }



	}
	if (!bDaemon) {
	    message(MSG_TEST, "\nNot in daemon mode so exiting now...\n");
	    break;
	}
    }				/*end while server loop */

    shutdown(rc != 0);

    return (0);			/* gcc squawks about this not being here */
}				/*end main */
