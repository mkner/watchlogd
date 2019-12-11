
#ifndef INCLD_LOG_H
#define INCLD_LOG_H

#include <stdio.h>
#include "watchlog.h"

#define MSG_DEBUG	1
#define MSG_TEST	2
#define MSG_NORMAL	3
#define MSG_ERR		4
#define MSG_FATAL	5



#define LOG_SHOWTIME	(1 << 0)

void logSetLevel(int level);
int logGetLevel(void);
void logSetMyName(char *);
void logSyslog(int iPriority, char *szMessage);
void message(int level, char *format,...);
BOOLEAN logSendErrorLog(char *szMailAddr);

#endif
