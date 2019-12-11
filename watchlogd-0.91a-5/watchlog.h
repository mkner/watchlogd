#ifndef INCLD_WATCHLOG_H
#define INCLD_WATCHLOG_H


#define COPYRIGHT "Copyright (c) 1997-1998  Mike Knerr"

#define DEFAULT_STATEFILE "/var/lib/watchlogd/status"
#define DEFAULTCONFFILE "/etc/watchlogd.conf"
#define DEFAULTCONFDIR "/etc/watchlog.d"

typedef enum boolean {
    FALSE = 0, TRUE = 1
} BOOLEAN;


BOOLEAN inDaemonMode(void);
BOOLEAN inTestMode(void);
BOOLEAN inVerboseMode(void);

#endif
