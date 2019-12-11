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
#include <time.h>
#include <unistd.h>

#include "watchlog.h"
#include "log.h"
#include "logrotate.h"


static logState *findState(char *fn, logState ** statesPtr,
			   int *numStatesPtr);



static logState *findState(char *fn, logState ** statesPtr,
			   int *numStatesPtr)
{
    int i;
    logState *states = *statesPtr;
    int numStates = *numStatesPtr;

    for (i = 0; i < numStates; i++)
	if (!strcmp(fn, states[i].fn))
	    break;

    if (i == numStates) {
	i = numStates++;
	states = realloc(states, sizeof(*states) * numStates);
	states[i].fn = strdup(fn);
	memset(&states[i].lastRotated, 0, sizeof(states[i].lastRotated));

	*statesPtr = states;
	*numStatesPtr = numStates;
    }
    return (states + i);
}

int rotateLog(logInfo * log, logState ** statesPtr, int *numStatesPtr)
{
    struct stat sb;
    time_t nowSecs = time(NULL);
    struct tm now = *localtime(&nowSecs);
    char *oldName, *newName = NULL;
    char *disposeName;
    char *finalName;
    char *tmp;
    char *ext = "";
    BOOLEAN bErrors = FALSE;
    BOOLEAN bDoRotate = FALSE;
    int i;
    int fd;
    /*int oldstderr = 0,*/		/* newerr; */
    logState * state = NULL;
    uid_t createUid;
    gid_t createGid;
    mode_t createMode;

    if (log->bCheckLog == FALSE) {
	message(MSG_TEST, "\nchecking: %s (checklog is off)\n", log->fn);
	return 1;
    }
    message(MSG_TEST, "\nchecking: %s\n", log->fn);
    switch (log->criterium) {
    case ROT_DAILY:
	message(MSG_TEST, "checklog: daily\n");
	break;
    case ROT_WEEKLY:
	message(MSG_TEST, "checklog: weekly\n");
	break;
    case ROT_MONTHLY:
	message(MSG_TEST, "checklog: monthly\n");
	break;
    case ROT_SIZE:
	message(MSG_TEST, "checklog: filesize (maxsize: %d)\n", log->maxsize);
	break;
    }


    if (log->rotateCount >= 0) {
	message(MSG_TEST, "rotations: %d \n", log->rotateCount);

	if (log->rotateCount == 0) {

	    /* will be nothing to compress since nothing rotated
	     * and avoid gunziping a NOT gziped file when
	     * mailing
	     */
	    log->flags &= ~LOG_FLAG_COMPRESS;
	}
    }
    if (log->flags & LOG_FLAG_COMPRESS)
	ext = COMPRESS_EXT;


    if (log->bSendErrorLog) {
	message(MSG_TEST, "error log will be mailed to: %s \n", log->errAddress);

    }
    if (log->bSendRemovedLog) {
	message(MSG_TEST, "removed logs will be mailed to: %s\n", log->logAddress);
    } else {
	message(MSG_TEST, "removed logs will not be mailed out\n");
    }

    if (stat(log->fn, &sb)) {
	message(MSG_ERR, "stat of %s failed: %s\n", log->fn,
		strerror(errno));
	bErrors = TRUE;
    }
    if (bErrors) {
	message(MSG_TEST, "errors detected\n");

    } else {
	message(MSG_TEST, "checking processing criteria...\n");

    }


    if (!bErrors) {

	state = findState(log->fn, statesPtr, numStatesPtr);

	/* show comparision values  */

	message(MSG_TEST, "last rotated year: %d  current year %d\n",
		state->lastRotated.tm_year, now.tm_year);
	message(MSG_TEST, "last rotated month: %d  current month %d\n",
		state->lastRotated.tm_mon, now.tm_mon);
	message(MSG_TEST, "last rotated day: %d  current day %d\n",
		state->lastRotated.tm_mday, now.tm_mday);


	if (log->criterium == ROT_SIZE) {
	    message(MSG_TEST, "current size: %d  maxsize: %d\n",
		    sb.st_size, log->maxsize);
	    bDoRotate = (sb.st_size >= log->maxsize);

	} else if (state->lastRotated.tm_year != now.tm_year ||
		   state->lastRotated.tm_mon != now.tm_mon ||
		   state->lastRotated.tm_mday != now.tm_mday) {
	    switch (log->criterium) {
	    case ROT_WEEKLY:
		/* on first day of the week */
		bDoRotate = (now.tm_wday == 0);
		break;
	    case ROT_MONTHLY:
		/* on first day of the month */
		bDoRotate = (now.tm_mday == 1);
		break;
	    case ROT_DAILY:
		/* when day different from last processing day */
		bDoRotate = (state->lastRotated.tm_mday != now.tm_mday);
		break;
	    default:
		/* ack! */
		bDoRotate = 0;
		break;
	    }
	} else {
	    bDoRotate = 0;
	}
    }
    if (bErrors) {
	message(MSG_ERR, "errors detected\n");

    } else {
	message(MSG_TEST, "continuing...\n");

    }


    if (!bErrors && bDoRotate) {
	message(MSG_TEST, "log will be processed\n");

	state->lastRotated = now;

	if (!log->rotateCount) {
	    disposeName = log->fn;
	    finalName = NULL;
	} else {
	    oldName = alloca(strlen(log->fn) + 10);
	    newName = alloca(strlen(log->fn) + 10);

	    sprintf(oldName, "%s.%d%s", log->fn, log->rotateCount + 1, ext);

	    disposeName = alloca(strlen(log->fn) + 10);
	    strcpy(disposeName, oldName);

	    for (i = log->rotateCount; i && !bErrors; i--) {
		tmp = newName;
		newName = oldName;
		oldName = tmp;
		sprintf(oldName, "%s.%d%s", log->fn, i, ext);

		message(MSG_TEST, "renaming %s to %s\n", oldName, newName);

		if (!inTestMode() && rename(oldName, newName)) {
		    if (errno == ENOENT) {
			message(MSG_TEST, "old log %s does not exist\n",
				oldName);
		    } else {
			message(MSG_ERR, "error renaming %s to %s: %s\n",
				oldName, newName, strerror(errno));
			bErrors = TRUE;
		    }
		}
	    }

	    finalName = oldName;

	    /* note: the gzip extension is *not* used here! */
	    sprintf(finalName, "%s.1", log->fn);

	    /* if the last rotation doesn't exist, that's okay */
	    if (!inTestMode() && access(disposeName, F_OK)) {
		message(MSG_TEST, "file %s doesn't exist -- won't try "
			"dispose of it\n", disposeName);
		disposeName = NULL;
	    }
	}


	if (!bErrors && log->bSendRemovedLog && disposeName) {
	    char *command;

	    command = alloca(strlen(disposeName) + 100 +
			     strlen(UNCOMPRESS_PIPE));

	    if (log->flags & LOG_FLAG_COMPRESS) {
		sprintf(command, "%s < %s | /bin/mail -s '%s' %s",
			UNCOMPRESS_PIPE, disposeName, log->fn,
			log->logAddress);
	    } else {
		sprintf(command, "/bin/mail -s '%s' %s < %s", disposeName,
			log->logAddress, disposeName);
	    }

	    message(MSG_TEST, "executing: \"%s\"\n", command);

	    if (!inTestMode() && system(command)) {
		sprintf(newName, "%s.%d", log->fn, getpid());
		message(MSG_ERR, "Failed to mail %s to %s!\n",
			disposeName, log->logAddress);

		bErrors = TRUE;
	    }
	}
	if (!bErrors && disposeName) {
	    message(MSG_TEST, "removing old log %s\n", disposeName);

	    if (!inTestMode() && unlink(disposeName)) {
		message(MSG_ERR, "Failed to remove old log %s: %s\n",
			disposeName, strerror(errno));
		bErrors = TRUE;
	    }
	}
	if (!bErrors && finalName) {
	    if (log->pre) {
		message(MSG_TEST, "running prerotate script\n");
		if (!inTestMode() && system(log->pre)) {
		    message(MSG_ERR, "error running prerotate script -- leaving old log in 
place \n");
		    bErrors = TRUE;
		}
	    }
	    message(MSG_TEST, "renaming %s to %s\n", log->fn, finalName);

	    if (!inTestMode() && !bErrors && rename(log->fn, finalName)) {
		message(MSG_ERR, "failed to rename %s to %s: %s\n",
			log->fn, finalName, strerror(errno));
	    }
	    if (!bErrors && log->flags & LOG_FLAG_CREATE) {

		if (log->createUid == NO_UID) {
		    createUid = sb.st_uid;
		} else {
		    createUid = log->createUid;
		}

		if (log->createGid == NO_GID) {
		    createGid = sb.st_gid;
		} else {
		    createGid = log->createGid;
		}

		if (log->createMode == NO_MODE) {
		    createMode = sb.st_mode & 0777;
		} else {
		    createMode = log->createMode;
		}

		message(MSG_TEST, "creating new log mode: 0%o uid: %d gid: %d\n", createMode, createUid, createGid);

		if (!inTestMode()) {
		    fd = open(log->fn, O_CREAT | O_RDWR, createMode);
		    if (fd < 0) {
			message(MSG_ERR, "error creating %s: %s\n",
				log->fn, strerror(errno));
			bErrors = TRUE;
		    } else {
			if (fchown(fd, createUid, createGid)) {
			    message(MSG_ERR, "error setting owner of "
				    "%s: %s\n", log->fn, strerror(errno));
			    bErrors = TRUE;
			}
			close(fd);
		    }
		}
	    }
	    if (bErrors) {
		message(MSG_ERR, "errors detected\n");

	    } else {
		message(MSG_TEST, "continuing with postrotate...\n");

	    }

	    if (!bErrors && log->post) {
		message(MSG_TEST, "running postrotate script:\n");
		if (!inTestMode() && system(log->post)) {
		    message(MSG_ERR, "error running postrotate script:\n");
		    bErrors = TRUE;
		} else {
		    message(MSG_TEST, log->post);
		    message(MSG_TEST, "\n");
		}


	    }
	    if (bErrors) {
		message(MSG_ERR, "errors detected\n");

	    } else {
		message(MSG_TEST, "continuing with compression...\n");

	    }

	    if (!bErrors && log->flags & LOG_FLAG_COMPRESS) {
		char *command;

		command = alloca(strlen(finalName) + strlen(COMPRESS_COMMAND)
				 + 20);

		sprintf(command, "%s %s", COMPRESS_COMMAND, finalName);
		message(MSG_TEST, "compressing new log with: %s\n", command);
		if (!inTestMode() && system(command)) {
		    message(MSG_TEST, "failed to compress log %s\n",
			    finalName);
		    bErrors = TRUE;
		}
	    }
	}
    } else if (!bDoRotate) {
	message(MSG_TEST, "log does not need processing\n");
    }


/* if skip mailing errlog stuff if testing mode */

    if (!inTestMode()) {

	if (log->bSendRemovedLog) {

	    logSendErrorLog(log->errAddress);
	}
    }
    return bErrors;
}


int writeState(char *stateFilename, logState * states,
	       int numStates)
{
    FILE *f;
    int i;

    f = fopen(stateFilename, "w");
    if (!f) {
	message(MSG_ERR, "error creating state file %s: %s\n",
		stateFilename, strerror(errno));
	return 1;
    }
    fprintf(f, "watchlog state -- version 1\n");

    for (i = 0; i < numStates; i++) {
	if (states[i].lastRotated.tm_year) {
	    fprintf(f, "%s %d-%d-%d\n", states[i].fn,
		    states[i].lastRotated.tm_year + 1900,
		    states[i].lastRotated.tm_mon + 1,
		    states[i].lastRotated.tm_mday);
	}
    }

    fclose(f);

    return 0;
}

int readState(char *stateFilename, logState ** statesPtr,
	      int *numStatesPtr)
{
    FILE *f;
    char buf[1024];
    char buf2[1024];
    int year, month, day;
    int i;
    int line = 0;
    logState *st;
    BOOLEAN bdayok = FALSE;
    
    f = fopen(stateFilename, "r");

    if (!f && errno == ENOENT) {
	/* create the file before continuing to ensure we have write
	   access to the file */
	f = fopen(stateFilename, "w");
	if (!f) {
	    message(MSG_ERR, "error creating state file %s: %s\n",
		    stateFilename, strerror(errno));
	    return 1;
	}
	fprintf(f, "watchlog state -- version 1\n");
	fclose(f);
	return 0;
    } else if (!f) {
	message(MSG_ERR, "error creating state file %s: %s\n",
		stateFilename, strerror(errno));
	return 1;
    }
    if (!fgets(buf, sizeof(buf) - 1, f)) {
	message(MSG_ERR, "error reading top line of %s\n", stateFilename);
	fclose(f);
	return 1;
    }
    if (strcmp(buf, "watchlog state -- version 1\n")) {
	fclose(f);
	message(MSG_ERR, "bad top line in state file %s\n", stateFilename);
	return 1;
    }
    line++;

    while (fgets(buf, sizeof(buf) - 1, f)) {
	line++;
	i = strlen(buf);
	if (buf[i - 1] != '\n') {
	    message(MSG_ERR, "line too long in state file %s\n",
		    stateFilename);
	    fclose(f);
	    return 1;
	}
	if (i == 1)
	    continue;

	if (sscanf(buf, "%s %d-%d-%d\n", buf2, &year, &month, &day) != 4) {
	    message(MSG_ERR, "bad line %d in state file %s\n",
		    stateFilename);
	    fclose(f);
	    return 1;
	}
	/* Hack to hide earlier bug */
	if (year != 1900 && (year < 1996 || year > 2100)) {
	    message(MSG_ERR, "bad year %d for file %s in state file %s\n",
		    year, buf2, stateFilename);
	    fclose(f);
	    return 1;
	}
	if (month < 1 || month > 12) {
	    message(MSG_ERR, "bad month %d for file %s in state file %s\n",
		    month, buf2, stateFilename);
	    fclose(f);
	    return 1;
	}
	
	
	/* 0 to hide earlier bug */

         if (day > 0 
	    && (   month == 4 || month == 6 || month == 9 
	        || month == 11 ) && day <= 30
	    ) { bdayok = TRUE; }
	
	if (day > 0
	    && ( month == 1 || month == 3 || month == 5 
	         || month == 7  || month == 8 || month == 10 
		 || month == 12) && day <= 31
	       ) { bdayok = TRUE; }
	   
	if ( day > 0
	     && ( month == 2 && day < 30)
	   ) { bdayok = TRUE; }
	       
	       
        if ( bdayok == FALSE ) {
	   
	     message(MSG_ERR, "bad day %d for file %s in state file %s\n",
		    day, buf2, stateFilename);
	    
	    fclose(f);
	    return 1;
	}
	
	year -= 1900, month -= 1;

	st = findState(buf2, statesPtr, numStatesPtr);

	st->lastRotated.tm_mon = month;
	st->lastRotated.tm_mday = day;
	st->lastRotated.tm_year = year;
    }

    fclose(f);

    return 0;

}

