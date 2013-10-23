// -*- mode: c; tab-width: 8; indent-tabs-mode: 1; st-rulers: [70] -*-
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Pagoda Box, Inc.  All rights reserved.
 */

#ifndef NARC_H
#define NARC_H 

#if defined(__sun)
#include "solarisfixes.h"
#endif

#include <uv.h>		/* Event driven programming library */

#include "sds.h"	/* Dynamic safe strings */
#include "adlist.h"	/* Linked lists */
#include "zmalloc.h"	/* total memory usage aware version of malloc/free */
#include "version.h"	/* Version macro */
#include "util.h"	/* Misc functions useful in many places */

/* Error codes */
#define NARC_OK		0
#define NARC_ERR	-1

/* Static narc configuration */
#define NARC_MAX_BUFF_SIZE 		4096
#define NARC_MAX_MESSAGE_SIZE 		1024
#define NARC_CONFIGLINE_MAX		1024
#define NARC_MAX_LOGMSG_LEN		1024	/* Default maximum length of syslog messages */
#define NARC_DEFAULT_DAEMONIZE   	0
#define NARC_DEFAULT_PIDFILE     	"/var/run/narc.pid"
#define NARC_DEFAULT_LOGFILE     	""
#define NARC_DEFAULT_SYSLOG_IDENT	"narc"
#define NARC_DEFAULT_SYSLOG_ENABLED	0
#define NARC_DEFAULT_HOST 		"127.0.0.1"
#define NARC_DEFAULT_PORT 		514
#define NARC_DEFAULT_PROTO		"tcp"
#define NARC_DEFAULT_IDENTIFIER		""

/* Stream locking */
#define NARC_STREAM_LOCKED	1
#define NARC_STREAM_UNLOCKED	2

/* Log levels */
#define NARC_DEBUG		0
#define NARC_VERBOSE		1
#define NARC_NOTICE		2
#define NARC_WARNING		3
#define NARC_LOG_RAW		(1<<10)	/* Modifier to log without timestamp */
// #define NARC_DEFAULT_VERBOSITY	NARC_NOTICE
#define NARC_DEFAULT_VERBOSITY	NARC_DEBUG

/* Anti-warning macro... */
#define NARC_NOTUSED(V)	((void) V)

/* We can print the stacktrace, so our assert is defined this way: */
#define narcAssertWithInfo(_c,_o,_e)	((_e)?(void)0 : (_narcAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define narcAssert(_e)			((_e)?(void)0 : (_narcAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define narcPanic(_e)			_narcPanic(#_e,__FILE__,__LINE__),_exit(1)

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

typedef struct {
	char *id;				/* message id prefix */
	char *file;				/* absolute path to the file */
	int fd;					/* file descriptor */
	uv_statbuf_t *stat;			/* the stat struct */
	char buffer[NARC_MAX_BUFF_SIZE];	/* read buffer (file content) */
	char line[NARC_MAX_LOGMSG_LEN + 1];	/* the current line */
	int index;				/* the line character index */
	int lock;				/* read lock to prevent resetting buffers */
} narcStream;

/*-----------------------------------------------------------------------------
 * Global state
 *----------------------------------------------------------------------------*/

struct narcServer {
	/* General */
	char		*configfile;			/* Absolute config file path, or NULL */
	char		*pidfile;			/* PID file path */
	int		arch_bits;			/* 32 or 64 depending on sizeof(long) */
	list		*streams;			/* Stream list */
	uv_loop_t	*loop;				/* Event loop */
	char 		*identifier; 			/* prefix all messages */
	/* Networking */
	char		*host; 				/* Remote syslog host */
	int 		port; 				/* Remote syslog port */
	char 		*protocol; 			/* Protocol to use when communicating with remote host */
	/* Configuration */
	int		verbosity;			/* Loglevel in narc.conf */
	int		daemonize;			/* True if running as a daemon */
	/* Logging */
	char		*logfile;			/* Path of log file */
	int		syslog_enabled;			/* Is syslog enabled? */
	char		*syslog_ident;			/* Syslog ident */
	int		syslog_facility;		/* Syslog facility */
};

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct narcServer	server;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/
/* Core functions and callbacks */
void		narcOutOfMemoryHandler(size_t allocation_size);
int		main(int argc, char **argv);
void		loadServerConfig(char *filename, char *options);
void		initServerConfig(void);
void		initServer(void);
void		openFile(narcStream *stream);
void		onFileOpen(uv_fs_t *req);
void		initWatcher(narcStream *stream);
void		onFileChange(uv_fs_event_t *handle, const char *filename, int events, int status);
void		statFile(narcStream *stream);
void		onFileStat(uv_fs_t* req);
void		readFile(narcStream *stream);
void		onFileRead(uv_fs_t *req);
void		initBuffer(char *buffer);
void		initLine(char *line);
void		handleMessage(char *id, char *message);

/* Logging */
#ifdef __GNUC__
void		narcLog(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#else
void		narcLog(int level, const char *fmt, ...);
#endif
void		narcLogRaw(int level, const char *msg);

/* Git SHA1 */
char		*narcGitSHA1(void);
char		*narcGitDirty(void);
uint64_t	narcBuildId(void);

/* Deprecated */
#if defined(__GNUC__)
void		*calloc(size_t count, size_t size) __attribute__ ((deprecated));
void		free(void *ptr) __attribute__ ((deprecated));
void		*malloc(size_t size) __attribute__ ((deprecated));
void		*realloc(void *ptr, size_t size) __attribute__ ((deprecated));
#endif

/* Debugging stuff */
void		_narcAssert(char *estr, char *file, int line);
void		_narcPanic(char *msg, char *file, int line);

#endif