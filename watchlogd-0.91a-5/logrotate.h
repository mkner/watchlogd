#ifndef INCLD_LOGROTATE_H
#define INCLD_LOGROTATE_H

#include <sys/types.h>
#include "watchlog.h"

#define LOG_FLAG_COMPRESS	(1 << 0)
#define LOG_FLAG_CREATE		(1 << 1)

#define COMPRESS_COMMAND "gzip -9"
#define COMPRESS_EXT ".gz"
#define UNCOMPRESS_PIPE "gunzip"


typedef struct {
    char *fn;
    enum {
	ROT_DAILY, ROT_WEEKLY, ROT_MONTHLY, ROT_SIZE
    } criterium;
    unsigned int maxsize;
    int rotateCount;
    char *pre;
    char *post;
    char *logAddress;
    char *errAddress;
    int flags;
    mode_t createMode;		/* if any/all of these are -1, we use the */
    uid_t createUid;		/* attributes from the log file just rotated */
    gid_t createGid;
    BOOLEAN bSendRemovedLog;	/* can be a local override if a flag */
    BOOLEAN bSendErrorLog;      /* send out an error log */
    BOOLEAN bCheckLog;		/* process rotations? */
    BOOLEAN bScanLog;		/* event scanning on? */
} logInfo;


typedef struct {
    char *fn;
    struct tm lastRotated;	/* only tm.mon, tm_mday, tm_year are valid! */
} logState;

#define NO_MODE ((mode_t) -1)
#define NO_UID  ((uid_t) -1)
#define NO_GID  ((gid_t) -1)


extern BOOLEAN bTestMode;


/* prototypes */

int readConfigPath(char *path, logInfo * defConfig,
		   logInfo ** logsPtr, int *numLogsPtr);

int rotateLog(logInfo * log, logState ** statesPtr, int *numStatesPtr);

int writeState(char *stateFilename, logState * states,
	       int numStates);
int readState(char *stateFilename, logState ** statesPtr,
	      int *numStatesPtr);




#endif
