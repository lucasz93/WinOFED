/*
 * Copyright (c) 1996-2011 Intel Corporation. All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _OSM_COMMON_H_
#define _OSM_COMMON_H_

#include <winsock2.h>
#include <windows.h>
#include <sys/stat.h>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <linux\_string.h>
#include <linux\unistd.h>
#include <linux\arpa\inet.h>
#include <stdarg.h>
#include <ctype.h>
#include <shlobj.h>

#include <complib\cl_debug.h>
#define cl_is_debug osm_is_debug

#include <time.h>
#include <sys\time.h>
#include <getopt.h>

#define random rand
#define srandom srand

#pragma warning(disable : 4996)
#pragma warning(disable : 4100)

#include <complib\cl_mutex.h>
#include <complib\cl_thread.h>
#include <complib\cl_timer.h>

typedef int	ssize_t;

#define OSM_API __stdcall
#define OSM_CDECL __cdecl

#define complib_init()
#define complib_exit()

#define chmod(a,b) _chmod(a,b)
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE

#define snprintf _snprintf
#define vsnprintf _vsnprintf
#ifndef strtoull 
#define strtoull _strtoui64
#endif

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#define O_NONBLOCK 0

#define open _open
#define close _close
#define fileno _fileno
#define stat _stat
#define fstat _fstat
#define unlink(str) _unlink(str)
#define dup2(a,b)
#define isatty _isatty

#ifndef getpid
#define getpid() GetCurrentProcessId()
#endif

#define usleep(usec) SleepEx(usec/1000,TRUE)

ssize_t getline(char**, size_t*, FILE *);

#ifdef HAVE_LIBPTHREAD
#error HAVE_LIBPTHREAD defined?
#endif

/* for those cases where uses of pthreads constructs do not have
 * #ifdef HAVE_LIBPTHREAD protection.
 */
#define pthread_t cl_thread_t
#define pthread_mutex_t cl_mutex_t

#define pthread_mutex_lock cl_mutex_acquire
#define pthread_mutex_unlock cl_mutex_release
#define pthread_mutex_init(a,b) cl_mutex_init((a))
#define pthread_mutex_destroy cl_mutex_destroy

#define pthread_cond_init(a,b)
#define pthread_cond_signal cl_event_signal
#define pthread_cond_destroy(a)


/************************************************************************/
static char* 
get_char_option(const char* optstring,
                             char*const* argv,int argc, 
                             int iArg, int* opt_ind,char* opt_p);   
int 
getopt_long_only(int argc, char *const*argv,
                         const char *optstring,
                         const struct option *longopts, int *longindex);

/**************************************************************************/

extern char *strdup_expand(const char *);
extern FILE *Fopen(const char *,const char *);
#define fopen Fopen

/* The following defines replace syslog.h */

void openlog(char *ident, int option, int facility);
void closelog(void);
#define LOG_CONS	(1<<0)
#define LOG_PID		(1<<2)
#define LOG_USER	(1<<3)

void syslog(int priority, char *fmt, ... );

#define LOG_DEBUG 7
#define LOG_INFO 6
#define LOG_NOTICE 5
#define LOG_WARNING 4
#define LOG_ERR 3
#define LOG_CRIT 2
#define LOG_ALTERT 1
#define LOG_EMERG 0

/*****************************************/

/****f* OpenSM: osm_common/GetOsmTempPath
* NAME
*	GetOsmTempPath
*
* DESCRIPTION
*	The function retrieves the temp path defined in Windows using its API
*
* SYNOPSIS
*/
char*
GetOsmTempPath(void);
/*
* PARAMETERS
*	NONE
*
* RETURN VALUE
*	This function returns string containing the default temp path in windows
*
* NOTES
*/

/****f* OpenSM: osm_common/GetOsmCachePath
* NAME
*	GetOsmCachePath
*
* DESCRIPTION
*	The function retrieves the path the cache directory. This directory is 
*  the etc dir under the installation directory of the mellanox stack. 
*  The installation directory should be pointed out by the WinIB_HOME variable.
*  If WinIB_HOME variable is missing, or there is not /etc/ dir under it - then
*  the function will return the getOsmTempPath() value.
*
* SYNOPSIS
*/
char *GetOsmCachePath(void);
/*
* PARAMETERS
*	NONE
*
* RETURN VALUE
*	function returns string containing the default cache path for osm use.
*
* NOTES
*/

/* **** Move this to inc\complib\cl_qlist.h once it works correctly */

/****d* Component Library: Quick List/cl_item_obj
* NAME
*	cl_item_obj
*
* DESCRIPTION
*	used to extract a 'typed' pointer from Quick List item.
*
* SYNOPSIS
*/

#define cl_item_obj(item_ptr, obj_ptr, item_field) \
	(void*)((uint8_t*)item_ptr - \
		((uint8_t*)(&(obj_ptr)->item_field) - (uint8_t*)(obj_ptr)))
/*
* PARAMETERS
*	item_ptr
*		[in] Pointer to a cl_qlist structure.
*
*	obj_ptr
*		[in] object pointer
*
*   item_field
*       [in] object pointer field
*
*   TypeOF_Obj
*
* RETURN VALUE
*	returns a 'TypeOF_Obj ptr
*
* SEE ALSO
*	Quick List
*********/

#endif		/* _OSM_COMMON_H_ */
