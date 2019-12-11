#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "watchlog.h"
#include "log.h"
#include "logrotate.h"

static char *tabooExts[] =
{".mpksave", ".mpkorig", ".rpmsave", ".rpmorig", "~"};
static int tabooCount = sizeof(tabooExts) / sizeof(char *);

/* flags to handle using default if not found to be specified */
static BOOLEAN bGlobalConfigFile = FALSE;
static BOOLEAN bFoundGlobalInclude = FALSE;
static BOOLEAN bParsedDefaultConfigDir = FALSE;

static int readConfigFile(char *configFile, logInfo * defConfig,
			  logInfo ** logsPtr, int *numLogsPtr);

static int isolateValue(char *fileName, int lineNum, char *key,
			char **startPtr, char **endPtr)
{
    char *chptr = *startPtr;

    while (isblank(*chptr))
	chptr++;
    if (*chptr == '=') {
	chptr++;
	while (*chptr && isblank(*chptr))
	    chptr++;
    }
    if (*chptr == '\n') {
	message(MSG_ERR, "%s:%d argument expected after %s\n",
		fileName, lineNum, key);
	return 1;
    }
    *startPtr = chptr;

    while (*chptr != '\n')
	chptr++;

    while (isblank(*chptr))
	chptr--;

    if (*chptr == '\n')
	*endPtr = chptr;
    else
	*endPtr = chptr + 1;

    return 0;
}


static char *
 readAddress(char *configFile, int lineNum, char *key,
	     char **startPtr)
{
    char oldchar;
    char *endtag, *chptr;
    char *start = *startPtr;
    char *address;

    if (!isolateValue(configFile, lineNum, key, &start,
		      &endtag)) {
	oldchar = *endtag, *endtag = '\0';

	chptr = start;
	while (*chptr && isprint(*chptr) && *chptr != ' ')
	    chptr++;
	if (*chptr) {
	    message(MSG_ERR, "%s:%d invalid %s address %s\n",
		    configFile, lineNum, key, start);
	    return NULL;
	}
	address = strdup(start);

	*endtag = oldchar, start = endtag;

	*startPtr = start;

	return address;
    } else
	return NULL;
}


int readConfigPath(char *path, logInfo * defConfig,
		   logInfo ** logsPtr, int *numLogsPtr)
{
    struct stat sb;
    DIR *dir;
    struct dirent *ent;
    int here;
    int i;

/* dont reread recursively */

    if ((bFoundGlobalInclude == 0)
	&& (bParsedDefaultConfigDir == 1)
	&& (strcmp(path, DEFAULTCONFDIR) == 0)) {

	return 0;
    }
    if ((bFoundGlobalInclude == 0)
	&& (bParsedDefaultConfigDir == 0)
	&& (strcmp(path, DEFAULTCONFDIR) == 0)) {

	/* set so no recurse */
	bParsedDefaultConfigDir = 1;

    }
    if (stat(path, &sb)) {

	/* dont show stat error if attempted to use
	 * default conf file but show open error later
	 */

	if (strcmp(path, DEFAULTCONFFILE) != 0) {
	    message(MSG_ERR, "cannot stat %s: %s\n", path, strerror(errno));
	    return 1;
	}
    }
    if (S_ISDIR(sb.st_mode)) {
	dir = opendir(path);
	if (!dir) {
	    message(MSG_ERR, "failed to open directory %s: %s\n", path,
		    strerror(errno));
	    return 1;
	}
	here = open(".", O_RDONLY);
	if (here < 0) {
	    message(MSG_ERR, "cannot open current directory: %s\n",
		    strerror(errno));
	    closedir(dir);
	    return 1;
	}
	if (chdir(path)) {
	    message(MSG_ERR, "error in chdir(\"%s\"): %s\n", path,
		    strerror(errno));
	    close(here);
	    closedir(dir);
	    return 1;
	}
	do {
	    errno = 0;
	    ent = readdir(dir);
	    if (errno) {
		message(MSG_ERR, "readdir() from %s failed: %s\n", path,
			strerror(errno));
		fchdir(here);
		close(here);
		closedir(dir);
		return 1;
	    } else if (ent && ent->d_name[0] == '.' && (!ent->d_name[1] ||
			   (ent->d_name[1] == '.' && !ent->d_name[2]))) {
		/* noop */
	    } else if (ent) {
		for (i = 0; i < tabooCount; i++) {
		    if (!strcmp(ent->d_name + strlen(ent->d_name) -
				strlen(tabooExts[i]), tabooExts[i]))
			break;
		}

		if (i == tabooCount) {
		    if (readConfigFile(ent->d_name, defConfig, logsPtr,
				       numLogsPtr)) {
			fchdir(here);
			close(here);
			return 1;
		    }
		}
	    }
	}
	while (ent);

	closedir(dir);

	fchdir(here);
	close(here);
    } else {
	return readConfigFile(path, defConfig, logsPtr, numLogsPtr);
    }

    return 0;
}


static int readConfigFile(char *configFile, logInfo * defConfig,
			  logInfo ** logsPtr, int *numLogsPtr)
{
    int fd;
    char *buf, *endtag;
    char oldchar, foo;
    int length;
    int lineNum = 1;
    int multiplier;
    int i;
    char *scriptStart = NULL;
    char **scriptDest = NULL;
    logInfo *newlog = defConfig;
    char *start, *chptr;
    struct group *group;
    struct passwd *pw;
    int rc;
    char createOwner[64], createGroup[64];
    char buf64[64];
    mode_t createMode;


    fd = open(configFile, O_RDONLY);
    if (fd < 0) {
	message(MSG_ERR, "failed to open config file %s: %s\n",
		configFile, strerror(errno));
	return 1;
    }
    length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    buf = alloca(length + 2);
    if (!buf) {
	message(MSG_ERR, "alloca() of %d bytes failed\n", length);
	close(fd);
	return 1;
    }
    if (read(fd, buf, length) != length) {
	message(MSG_ERR, "failed to read %s: %s\n", configFile,
		strerror(errno));
	close(fd);
	return 1;
    }
    close(fd);

    /* knowing the buffer ends with a newline makes things a (bit) cleaner */
    buf[length + 1] = '\0';
    buf[length] = '\n';

    message(MSG_TEST, "\nreading config file %s\n", configFile);

    if (strcmp(configFile, DEFAULTCONFFILE) == 0) {
	message(MSG_TEST, "parsing global config: %s\n\n", configFile);
	bGlobalConfigFile = TRUE;
    }
    start = buf;
    while (*start) {
	while (isblank(*start) && (*start))
	    start++;
	if (*start == '#') {
	    while (*start != '\n')
		start++;
	}
	if (*start == '\n') {
	    start++;
	    lineNum++;
	    continue;
	}
	if (scriptStart) {
	    if (strncmp(start, "end", 3) == 0) {

		message(MSG_TEST, "found end subscript:\n");

		chptr = start + 3;

		while (isblank(*chptr))
		    chptr++;
		if (*chptr == '\n') {
		    endtag = start;
		    while (*endtag != '\n')
			endtag--;
		    endtag++;
		    *scriptDest = malloc(endtag - scriptStart + 1);
		    strncpy(*scriptDest, scriptStart, endtag - scriptStart);
		    (*scriptDest)[endtag - scriptStart] = '\0';
		    start = chptr + 1;
		    lineNum++;

		    scriptDest = NULL;
		    scriptStart = NULL;
		}
	    }
	    if (scriptStart) {
		while (*start != '\n')
		    start++;
		lineNum++;
		start++;
	    }
	} else if (isalpha(*start)) {
	    endtag = start;
	    while (isalpha(*endtag))
		endtag++;
	    oldchar = *endtag;
	    *endtag = '\0';


/********************************************/
/***** start endifs for option scanning *****/
/********************************************/



/********************************************/
/***       global only options            ***/
/********************************************/


/**** senderrors options ****/

	    if (strcmp(start, "senderrors") == 0) {

		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "senderrors", &start, &endtag) == 0) {

		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "on") == 0) {
			if (bGlobalConfigFile) {

			    message(MSG_TEST, "set senderrors on\n");
			    newlog->bSendErrorLog = TRUE;
			}
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "off") == 0) {
			if (bGlobalConfigFile) {

			    message(MSG_TEST, "set senderrors off\n");
			    newlog->bSendErrorLog = FALSE;
			}
			*endtag = oldchar, start = endtag;

		    } else {

			/* not a senderrors option */

			message(MSG_ERR, "%s:%d invalid senderrors option'%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }

		} else {

		    /* no find senderrors option */

		    message(MSG_ERR, "%s:%d missing senderrors option\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}

		*endtag = oldchar, start = endtag;

		if (!bGlobalConfigFile) {
		    message(MSG_TEST, "ignoring 'senderrors' in local config\n");
		}
/**** senderrorsto options ****/

	    } else if (strcmp(start, "senderrorsto") == 0) {

		*endtag = oldchar, start = endtag;

		if (!(newlog->errAddress = readAddress(configFile, lineNum,
					      "senderrorsto", &start))) {
		    return 1;
		} else {

		    if (!bGlobalConfigFile) {
			message(MSG_TEST, "ignoring 'senderrorsto' in local config\n");
			newlog->errAddress = NULL;
		    } else {

			message(MSG_TEST, "set senderrorsto: %s\n", newlog->errAddress);
		    }
		}

/**** include directive ****/

	    } else if (strcmp(start, "include") == 0) {
		if (newlog != defConfig) {
		    message(MSG_ERR, "%s:%d include may not appear inside "
			  "of log file definition", configFile, lineNum);
		    return 1;
		}
		*endtag = oldchar, start = endtag;
		if (!isolateValue(configFile, lineNum, "size", &start,
				  &endtag)) {
		    oldchar = *endtag, *endtag = '\0';

		    if (bGlobalConfigFile) {

			bFoundGlobalInclude = TRUE;
			message(MSG_TEST, "found include in global conf %s\n", DEFAULTCONFFILE);

			message(MSG_TEST, "including %s\n", start);

			if (readConfigPath(start, defConfig, logsPtr,
					   numLogsPtr)) {
			    return 1;
			}
		    }		/*endif was global conf */
		    *endtag = oldchar, start = endtag;
		}
		if (!bGlobalConfigFile) {
		    message(MSG_TEST, "ignoring 'include' in local config\n");
		}
/********************************************/
/***   local or global options            ***/
/********************************************/
/**** compress options ****/
	    } else if (strcmp(start, "compress") == 0) {
		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "compress", &start, &endtag) == 0) {

		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "on") == 0) {

			message(MSG_TEST, "set compress on\n");
			newlog->flags |= LOG_FLAG_COMPRESS;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "off") == 0) {

			message(MSG_TEST, "set compress off\n");
			newlog->flags &= ~LOG_FLAG_COMPRESS;
			*endtag = oldchar, start = endtag;

		    } else {

			/* not a compress option */

			message(MSG_ERR, "%s:%d invalid compress option'%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }

		} else {

		    /* no compress option */

		    message(MSG_ERR, "%s:%d missing compress option\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}

/* **here       
   *endtag = oldchar, start = endtag;
 */

/**** sendlog options ****/

	    } else if (strcmp(start, "sendlog") == 0) {
		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "sendlog", &start, &endtag) == 0) {

		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "on") == 0) {

			message(MSG_TEST, "set sendlog on\n");
			newlog->bSendRemovedLog = TRUE;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "off") == 0) {

			message(MSG_TEST, "set sendlog off\n");
			newlog->bSendRemovedLog = FALSE;
			*endtag = oldchar, start = endtag;

		    } else {

			/* not a sendlog option */

			message(MSG_ERR, "%s:%d invalid sendlog option'%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }

		} else {

		    /* no find sendlog option */

		    message(MSG_ERR, "%s:%d missing sendlog option\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}

		*endtag = oldchar, start = endtag;


/**** createnew options ****/

	    } else if (strcmp(start, "createnew") == 0) {
		*endtag = oldchar, start = endtag;


		if (isolateValue(configFile, lineNum, "createnew", &start, &endtag) == 0) {


		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "on") == 0) {

			message(MSG_TEST, "set createnew on\n");
			newlog->flags |= LOG_FLAG_CREATE;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "off") == 0) {

			message(MSG_TEST, "set createnew off\n");
			newlog->flags &= ~LOG_FLAG_CREATE;
			*endtag = oldchar, start = endtag;

		    } else {

			/* not a option */

			message(MSG_ERR, "%s:%d invalid createnew option'%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }

		} else {


		    message(MSG_ERR, "%s:%d missing createnew option\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}


		*endtag = oldchar, start = endtag;


/**** createmode options ****/

	    } else if (strcmp(start, "createmode") == 0) {
		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "createmode", &start, &endtag) == 0) {
		    oldchar = *endtag, *endtag = '\0';

		    message(MSG_DEBUG, "createmode parsing: %s\n", start);

		    /* safer */
		    (void) strncpy(buf64, start, sizeof(buf64) - 1);
		    buf64[sizeof(buf64) - 1] = '\0';

		    rc = sscanf(buf64, "%o %s %s%c", &createMode,
				createOwner, createGroup, &foo);

		    message(MSG_DEBUG, "sscanf returns: %d\n", rc);

		    if ((rc == 0) || (rc == EOF)) {
			message(MSG_ERR, "could not parse createmode options\n");
			return 1;
		    }
		    if (rc == 4) {
			message(MSG_ERR, "%s:%d extra arguments for "
			     "createmode\n", configFile, lineNum, start);
			if (foo != '\t') {
			    return 1;
			} else {
			    message(MSG_TEST, "createmode ignoring TAB at end of line\n");
			}

		    }
		    if (rc > 0) {
			newlog->createMode = createMode;
			message(MSG_TEST, "createmode set mode: %o\n", createMode);
		    }
		    message(MSG_DEBUG, "checking owner with getpwnam\n");

		    if (rc > 1) {
			pw = getpwnam(createOwner);
			if (pw == NULL) {
			    message(MSG_ERR, "%s:%d unkown user '%s'\n",
				    configFile, lineNum, createOwner);
			    return 1;
			}
			newlog->createUid = pw->pw_uid;
			endpwent();
			message(MSG_TEST, "createmode set owner: %s\n", createOwner);

		    }
		    message(MSG_DEBUG, "checking group with getgrnam\n");

		    if (rc > 2) {
			group = getgrnam(createGroup);
			if (group == NULL) {
			    message(MSG_ERR, "%s:%d unkown group '%s'\n",
				    configFile, lineNum, createGroup);
			    return 1;
			}
			newlog->createGid = group->gr_gid;
			endgrent();
			message(MSG_TEST, "createmode set group: %s\n", createGroup);

		    }
		} else {


		    message(MSG_ERR, "%s:%d missing createmode options\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}



		/* eat since was just copied over */

		while (*start != '\n')
		    start++;


/* try this */
/* no work start--; */

/*
   endtag = start;
 */

/**** maxsize options ****/

	    } else if (strcmp(start, "maxsize") == 0) {
		*endtag = oldchar, start = endtag;


		if (isolateValue(configFile, lineNum, "maxsize", &start, &endtag) == 0) {
		    oldchar = *endtag, *endtag = '\0';

		    length = strlen(start) - 1;
		    if ((start[length] == 'k') || (start[length] == 'K')) {
			start[length] = '\0';
			multiplier = 1024;
		    } else if (start[length] == 'M') {
			start[length] = '\0';
			multiplier = 1024 * 1024;
		    } else if (!isdigit(start[length])) {
			message(MSG_ERR, "%s:%d unknown maxsize unit '%c'\n",
				configFile, lineNum, start[length]);
			return 1;
		    } else {
			multiplier = 1;
		    }

		    newlog->maxsize = multiplier * strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MSG_ERR, "%s:%d invalid maxsize '%s'\n",
				configFile, lineNum, start);
			return 1;
		    }
		    /** this is now set where 'rotate filesize' 
                      * is found newlog->criterium = ROT_SIZE;
                      */

		    message(MSG_TEST, "set maxsize: %d\n", newlog->maxsize);

		    *endtag = oldchar, start = endtag;
		}
/**** rotate options for frequency ****/

	    } else if (strcmp(start, "checklog") == 0) {

		*endtag = oldchar, start = endtag;

		if ((isolateValue(configFile, lineNum, "checklog", &start, &endtag) == 0)
		    || (isolateValue(configFile, lineNum, "process", &start, &endtag) == 0)) {

		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "daily") == 0) {

			message(MSG_TEST, "set checklog: daily\n");
			newlog->criterium = ROT_DAILY;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "weekly") == 0) {

			message(MSG_TEST, "set checklog: weekly\n");
			newlog->criterium = ROT_WEEKLY;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "monthly") == 0) {

			message(MSG_TEST, "set checklog: monthly\n");
			newlog->criterium = ROT_MONTHLY;
			*endtag = oldchar, start = endtag;

		    } else if (strcmp(start, "filesize") == 0) {

			message(MSG_TEST, "set checklog: filesize\n");
			newlog->criterium = ROT_SIZE;
			*endtag = oldchar, start = endtag;
		    } else if (strcmp(start, "off") == 0) {

			message(MSG_TEST, "set checklog: off\n");
			newlog->bCheckLog = FALSE;
			*endtag = oldchar, start = endtag;


		    } else {

			message(MSG_ERR, "%s:%d invalid checklog option'%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }


		} else {
		    message(MSG_ERR, "%s:%d missing checklog option\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}		/* endif got value ok */



/**** rotations options for number to keep ****/

	    } else if (strcmp(start, "rotations") == 0) {
		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "rotations", &start,
				 &endtag) == 0) {
		    oldchar = *endtag, *endtag = '\0';

		    newlog->rotateCount = strtoul(start, &chptr, 0);
		    if (*chptr) {
			message(MSG_ERR, "%s:%d invalid rotation count'%s'\n",
				configFile, lineNum, start);
			return 1;
		    } else {

			message(MSG_TEST, "set rotation count: %d\n", newlog->rotateCount);
		    }

		    *endtag = oldchar, start = endtag;
		}
/**** sendlogto options ****/

	    } else if (strcmp(start, "sendlogto") == 0) {
		*endtag = oldchar, start = endtag;
		if (!(newlog->logAddress = readAddress(configFile, lineNum,
						 "sendlogto", &start))) {
		    return 1;
		} else {

		    message(MSG_TEST, "set sendlogto: %s\n", newlog->logAddress);
		}

/**** begin subscript options ****/

	    } else if (strcmp(start, "begin") == 0) {

		*endtag = oldchar, start = endtag;

		if (isolateValue(configFile, lineNum, "begin", &start, &endtag) == 0) {

		    oldchar = *endtag, *endtag = '\0';

		    if (strcmp(start, "prerotate") == 0) {

			message(MSG_TEST, "found begin subscript: prerotate\n");

			*endtag = oldchar, start = endtag;
			scriptStart = start;
			scriptDest = &newlog->pre;

			/* eat the rest of the line */
			while (*start != '\n')
			    start++;

		    } else if (strcmp(start, "postrotate") == 0) {

			message(MSG_TEST, "found begin subscript: postrotate\n");

			*endtag = oldchar, start = endtag;
			scriptStart = start;
			scriptDest = &newlog->post;

			/* eat the rest of the line */
			while (*start != '\n')
			    start++;


		    } else {

			message(MSG_ERR, "%s:%d invalid begin subscript directive '%s'\n",
				configFile, lineNum, start);
			*endtag = oldchar, start = endtag;

			return 1;

		    }


		} else {
		    message(MSG_ERR, "%s:%d missing begin subscript directive\n",
			    configFile, lineNum);
		    *endtag = oldchar, start = endtag;

		    return 1;

		}		/* endif got value ok */


	    } else {
		message(MSG_ERR, "%s:%d unknown option '%s' "
			"-- ignoring line\n", configFile, lineNum, start);

		*endtag = oldchar, start = endtag;
	    }


/**********************/
/** end parse options */
/**********************/

	    while (isblank(*start))
		start++;

	    if (*start != '\n') {
		message(MSG_ERR, "%s:%d unexpected text\n", configFile,
			lineNum);
		while (*start != '\n')
		    start++;
	    }
	    lineNum++;
	    start++;
	} else if (*start == '/') {
	    if (newlog != defConfig) {
		message(MSG_ERR, "%s:%d unexpected log filename\n",
			configFile, lineNum);
		return 1;
	    }
	    (*numLogsPtr)++;
	    *logsPtr = realloc(*logsPtr, sizeof(**logsPtr) * *numLogsPtr);
	    newlog = *logsPtr + *numLogsPtr - 1;
	    memcpy(newlog, defConfig, sizeof(*newlog));

	    endtag = start;
	    while (!isspace(*endtag))
		endtag++;
	    oldchar = *endtag;
	    *endtag = '\0';

	    for (i = 0; i < *numLogsPtr - 1; i++) {
		if (!strcmp((*logsPtr)[i].fn, start)) {
		    message(MSG_ERR, "%s:%d duplicate log entry for %s\n",
			    configFile, lineNum, start);
		    return 1;
		}
	    }

	    newlog->fn = strdup(start);

	    message(MSG_TEST, "\nreading config info for %s\n", start);

	    *endtag = oldchar, start = endtag;

	    while (*start && isspace(*start) && *start != '{') {
		if (*start == '\n')
		    lineNum++;
		start++;
	    }

	    if (*start != '{') {
		message(MSG_ERR, "%s:%d { expected after log file name\n",
			configFile, lineNum);
		return 1;
	    }
	    start++;
	    while (isblank(*start) && *start != '\n')
		start++;

	    if (*start != '\n') {
		message(MSG_ERR, "%s:%d unexpected text after {\n");
		return 1;
	    }
	} else if (*start == '}') {
	    if (newlog == defConfig) {
		message(MSG_ERR, "%s:%d unxpected }\n", configFile, lineNum);
		return 1;
	    }
	    newlog = defConfig;

	    start++;
	    while (isblank(*start))
		start++;

	    if (*start != '\n') {
		message(MSG_ERR, "%s:%d, unexpected text after {\n",
			configFile, lineNum);
	    }
	} else {
	    message(MSG_ERR, "%s:%d lines must begin with a keyword "
		    "or a /\n", configFile, lineNum);

	    while (*start != '\n')
		start++;
	    lineNum++;
	    start++;
	}

    }

/* now since we are sort of done... check if the default conf file
 * was used but no include was found in which case attempt to
 * parse out and use files from the default config directory
 */

/*** CHANGED always include watchlog.d anyway ***/
/*    if ((bFoundGlobalInclude == FALSE)) {

   message(MSG_DEBUG, "\nno include found in global conf %s\n", DEFAULTCONFFILE);
   message(MSG_TEST, "so try including %s\n", DEFAULTCONFDIR);
 */
    if (readConfigPath(DEFAULTCONFDIR, defConfig, logsPtr, numLogsPtr)) {
	return 1;
    }
/*  } */


    return 0;

}
