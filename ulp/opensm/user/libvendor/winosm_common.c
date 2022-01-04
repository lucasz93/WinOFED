/*
 * Copyright (c) 2009-2011 Intel Corporation. All rights reserved.
 *
 * This software is available to you under the OpenFabricsAlliance.org BSD license
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

#include <vendor/winosm_common.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <complib/cl_memory.h>
#include <opensm/osm_base.h>

#include <..\..\..\..\etc\user\inet.c>

/*
 * Just like fopen() except the filename string is env var expanded prior
 * to opening the file. Allows %TEMP%\osm.log to work.
 */
#undef fopen

FILE *Fopen( 
   const char *filename,
   const char *mode )
{
	FILE *pFile;
	char *fname;

	fname = strdup_expand(filename);
	if (fname) {
		pFile = fopen(fname,mode);
		free(fname);
		return pFile;
	}
	return NULL;
}

#define OSM_MAX_LOG_NAME_SIZE 512

static char *syslog_fname;
static FILE *syslog_file;
static char *syslog_id;

void openlog(char *ident, int option, int facility)
{
	if (!syslog_fname)
		syslog_fname = strdup(OSM_DEFAULT_TMP_DIR "osm.syslog");	

	if (!syslog_file) {
		syslog_file = Fopen(syslog_fname,"w");
		if (syslog_file)
			syslog_id = strdup(ident);
	}
}

void closelog(void)
{
	if (syslog_file) {
		fprintf(syslog_file, "\n[%s] Closing syslog\n",syslog_id);
		fflush(syslog_file);
		fclose(syslog_file);
		syslog_file = NULL;
		if (syslog_id) {
			free((void*)syslog_id);
			syslog_id = NULL;
		}
		if (syslog_fname) {
			free((void*)syslog_fname);
			syslog_fname = NULL;
		}
	}
}

/* output to user-mode DebugView monitor if running */
 
void syslog(int prio, char *fmt, ... )
{
	char Buffer[1400];
	SYSTEMTIME st;
	uint32_t pid = GetCurrentThreadId();
	int rc;
	va_list args;

	va_start(args,fmt);
	rc = _vsnprintf(Buffer, sizeof(Buffer), (LPSTR)fmt, args); 
	va_end(args);

	if (!syslog_file) {
		OutputDebugStringA(Buffer);
		return;
	}

	if ( rc < 0 )
		fprintf(syslog_file,"syslog() overflow @ %d\n", sizeof(Buffer));

	GetLocalTime(&st);

	fprintf(syslog_file, "[%s][%02d-%02d-%4d %02d:%02d:%02d:%03d][%04X] %s",
		syslog_id, st.wMonth, st.wDay, st.wYear,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, pid,
		Buffer);
	fflush(syslog_file);
}

char*
GetOsmTempPath(void)
{
    static char temp_path[OSM_MAX_LOG_NAME_SIZE]; // ends with a '\'<null>.

    if (temp_path[0] == '\0')  // initialized?
    	GetTempPath(OSM_MAX_LOG_NAME_SIZE,temp_path);

    return temp_path;
}

char*
GetOsmCachePath(void)
{
   char* cache_path;
   char* tmp_file_name;
   char* winib_home, tmp;
   HANDLE hFile;

   winib_home = getenv("WinIB_HOME");
   if (winib_home == NULL)
   {
     /* The WinIB_HOME variable isn't defined. Use the 
        default temp path */
     return GetOsmTempPath();
   }
   cache_path = (char*)cl_malloc(OSM_MAX_LOG_NAME_SIZE);
   strcpy(cache_path, winib_home);

   strcat(cache_path, "\\etc\\");
   tmp_file_name = (char*)cl_malloc(OSM_MAX_LOG_NAME_SIZE);
   strcpy(tmp_file_name, cache_path);
   strcat(tmp_file_name, "opensm.opts");
   hFile = CreateFile(tmp_file_name,
                      GENERIC_READ,
                      0,
                      NULL,
                      OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL,
                      NULL);
   if (hFile == INVALID_HANDLE_VALUE) 
   { 
     cl_free(cache_path);
     return GetOsmTempPath();
   }
   /* Such file exists. This means the directory is usable */
   CloseHandle(hFile);

   return cache_path;
}


/*
 * Like _strdup() with Environment variable expansion.
 * Example: str '%windir%\temp\osm.log' --> 'C:\windows\temp\osm.log'
 * Multiple Env vars are supported.
 */

char *strdup_expand(const char *base)
{
	char *str,*p,*s,*es,*xs,*rc,*n;
	char p_env[80];

	str = _strdup(base);
	if (!str)
		return str;

	while( (s = strchr(str,'%')) )
	{
		p = strchr((++s),'%');
		if (!p)
			return str;

		memcpy(p_env,s,p-s);
		p_env[p-s] = '\0';
		
 		es = getenv(p_env);
		if (!es)
			return str;

		xs = (char*)malloc(strlen(str)+strlen(es));
		if (xs) {
			for(rc=str,n=xs; rc < (s-1);rc++) *n++ = *rc; 
			*n='\0';
			strcat(n,es);
			strcat(n,(p+1));
		}
		free(str);
		str = xs;
	}
	return str;
}


/****************************************************************************/

#include "..\..\..\etc\user\getopt.c"

int getopt_long_only(int argc, char *const*argv, const char *optstring,
			const struct option *longopts, int *longindex)
{
	return getopt_long( argc, argv, optstring, longopts, longindex );
}

ssize_t getline(char **obuf, size_t *out_sz, FILE *S)
{
	char *buf, *bstart;
	ssize_t bsize;
	int cnt;

	if ( obuf ) {
		buf = *obuf;
		bsize = (ssize_t)*out_sz;
	}
	else {
		bsize = 512;
		buf = malloc(bsize);
		if (!buf) {
			*out_sz = 0;
			return -1;
		}
		*out_sz = (size_t)bsize;
		*obuf = buf;
	}

	bstart = fgets( buf, (int)bsize, S);
	if ( !bstart ) { // error or EOF
		cnt = -1;
	}
	else {
		cnt = strlen(bstart);
	}
	return cnt;
}
