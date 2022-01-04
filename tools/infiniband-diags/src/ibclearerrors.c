/*
 * Copyright (c) 2004-2008 Voltaire Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <strsafe.h>
#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

#define X_PARAMLEN 254
#define X_LINELEN 4096
#define X_TEMPFILE "ibnetdiscoverTMP.txt"
#define X_TEMPFILELEN 20



static void ShowUsage()
{
    printf( "ibclearerrors [-h] [<topology-file>" \
	    "| -C ca_name -P ca_port -t timeout_ms]\n"
        "\th\t\t - Help (displays this message)\n"
        "\t<topology-file>\t - should be in the format indicated by ibnetdiscover\n"
        "\tca_name\t\t - the name indicated by ibstat\n");
	exit(1);
}

static void printCopyError(char* copiedVal, HRESULT hr)
{	
	if (hr == ERROR_INSUFFICIENT_BUFFER)
	{
		printf("The %s length is too long %08x.\n", copiedVal, hr);
	}
	else 
	{
		printf("Error in getting the %s %08x.\n", copiedVal, hr);
	}
	exit(-1);
}

static int clearErrors(char* caInfo, char* lid, char* port, BOOL all)
{
	char usedCommand [ X_LINELEN ];
	HRESULT hr;
	
	if ((caInfo == NULL) || 
		(lid == NULL) || (strlen(lid) <= 0)||
		(port == NULL) || (strlen(port) <= 0))
	{
		return 1;	
	}

	hr = StringCchPrintfEx(usedCommand,X_LINELEN,NULL,NULL,0,
                       "perfquery %s -R %s %s %s 0x0fff",
                       caInfo,
                       (all ? "-a" : ""),
						lid, 
						port);
	if (FAILED(hr))	
	{
		StringCchPrintfEx(usedCommand,X_LINELEN,NULL,NULL,0,"used Command");
		printCopyError(usedCommand,hr);
	}

	if (system(usedCommand))
	{
		return 1;
	}
	else
	{
		return 0;
	}	
}

static char* getLid(char* line, char* lidPrefix) 
{
	//
	//Get lid start position
	//
	char* lidStart = strstr(line, lidPrefix) + strlen(lidPrefix);
	char* NextToken;

	//
	//Get the string from lidStart until the first position of a blank = exactly the lid
	//
	char* lid = strtok_s (lidStart," \t", &NextToken); 

	return lid;
}

int __cdecl main(int argc, char* argv[])
{
	char topofile[X_PARAMLEN];
	BOOL topologyGiven = 0;

	char caInfo[X_LINELEN];
	BOOL caInfoGiven = 0;
	
	int nodes = 0;
	int errors = 0;
	
	FILE *fp;
	char line[X_LINELEN];
	char templine[X_LINELEN];

	int i;

	char type[10];
	char usedCommand[X_LINELEN];

	HRESULT hr;
	int OpenFileErr;

	topofile[0] = '\0';

	for( i = 1; i < argc; i++ )
    {
        char* pArg;
		char* nextArg;

        pArg = argv[i];

		if (( *pArg != '-' ) && ( *pArg != '/' ))
		{
			if (strlen(topofile) == 0) 
			{
				hr = HRESULT_CODE(StringCchCopyNExA(topofile,X_PARAMLEN,pArg,X_PARAMLEN,NULL,NULL,0));
				if (FAILED(hr))	
				{
					StringCchPrintfEx(topofile,X_PARAMLEN,NULL,NULL,0,"topofile");
					printCopyError(topofile,hr);
				}
				topologyGiven = 1;
				continue;
			}
			else
			{
				ShowUsage();
                break;
			}		
		}
			

        // Skip leading dashes
        while(( *pArg == '-' ) || ( *pArg == '/' ))
            pArg++;

        switch( *pArg )
        {
        case 'h':
		case '?':
			ShowUsage();
			break;
		case 'P':
		case 'C':
		case 't':
            if( ++i == argc )
            {
				ShowUsage();
                break;
            }

			nextArg = argv[i];
            if( *nextArg == '-' )
            {
				ShowUsage();
                break;
            }

			if (!caInfoGiven)
			{
				StringCchPrintfEx(caInfo,X_LINELEN,NULL,NULL,0,"");
			}
			
			hr = StringCchPrintfEx(	caInfo,X_LINELEN,NULL,NULL,0,
									"%s -%s %s",caInfo,pArg,nextArg);
			if (FAILED(hr))	
			{
				StringCchPrintfEx(caInfo,X_LINELEN,NULL,NULL,0,"caInfo");
				printCopyError(caInfo,hr);
			}
			
			caInfoGiven = 1;
            break;
        default:
            printf( "Unknown parameter %s\n", pArg );
            ShowUsage();
        }
    }

	//
	// If a topology file was not given we will create one
	//
	if (!topologyGiven)
	{		
		DWORD nPathLength = X_LINELEN+X_TEMPFILELEN;
		char path[X_LINELEN+X_TEMPFILELEN];
		int reqLen = GetTempPath(nPathLength, path);
		if (reqLen > X_LINELEN)
		{
			printf("Obtaining a path to create temporary files failed\n");
			exit(-1);
		}	
		
		hr = StringCchPrintfEx(	topofile,X_LINELEN,NULL,NULL,0,
								"%s%s", path, X_TEMPFILE);
		if (FAILED(hr))	
		{
			StringCchPrintfEx(topofile,X_LINELEN,NULL,NULL,0,"topofile");
			printCopyError(topofile,hr);
		}

		hr = StringCchPrintfEx(	usedCommand,X_LINELEN,NULL,NULL,0,
								"ibnetdiscover %s > %s", 
								caInfoGiven ? caInfo : "", 
								topofile );
		if (FAILED(hr))	
		{
			StringCchPrintfEx(usedCommand,X_LINELEN,NULL,NULL,0,"used Command");
			printCopyError(usedCommand,hr);
		}
		
		if (system(usedCommand))
		{
			printf("ibnetdiscover execution failed (%d)\n", GetLastError() );
			exit(1);
		}
	}

	OpenFileErr = fopen_s(&fp, topofile, "r");
	if(OpenFileErr != 0)
	{
	   printf("Cannot open file.\n");
	   exit(1);
	}

	while (fgets(line, X_LINELEN, fp) != NULL) 
	{
		if (strstr( line, "Ca" ) == line || 
			strstr( line, "Rt" ) == line ||
			strstr( line, "Switch" ) == line)
		{
			char *NextToken;
			nodes++;
			
			//
			//First word is the type (one of the above)
			//
			hr = HRESULT_CODE(StringCchCopyNExA(templine,X_LINELEN,line,X_LINELEN,NULL,NULL,0));
			if (FAILED(hr))	
			{
				StringCchPrintfEx(templine,X_LINELEN,NULL,NULL,0,"inner topology line");
				printCopyError(templine,hr);
			}
			 
			hr = HRESULT_CODE(StringCchCopyNExA(type,10,strtok_s(templine," \t", &NextToken),10,NULL,NULL,0));
			if (FAILED(hr))	
			{
				StringCchPrintfEx(type,10,NULL,NULL,0,"type");
				printCopyError(type,hr);
			}
			
		}

		
		if (strstr( line, "Switch" ) == line)
		{			
			errors += clearErrors(	caInfoGiven ? caInfo : "",  
									getLid(line,"port 0 lid "), 
									"255", 
									1 );
		}

		if (*line == '[')
		{ 
			char* port;
			char *NextToken;
			
			hr = HRESULT_CODE(StringCchCopyNExA(templine,X_LINELEN,line,X_LINELEN,NULL,NULL,0));
			if (FAILED(hr))	
			{
				StringCchPrintfEx(templine,X_LINELEN,NULL,NULL,0,"inner topology line");
				printCopyError(templine,hr);
			}
			
			port = strtok_s (templine,"[]", &NextToken); //First word is the port in []
			
			if (strcmp(type,"Switch") != 0) //Got it from the previous line
			{
				errors += clearErrors(	caInfoGiven ? caInfo : "",
										getLid(line," lid "),
										port,
										0 );
			}
		}

		if (strstr( line, "ib" ) == line)
		{
			printf("%s\n", line);
		}
	}
		
	printf ("\n## Summary: %d nodes cleared %d errors\n", nodes, errors);
	fclose (fp);

	//
	// If we created our own topology file - we sould delete it
	//
	if (strstr( topofile, X_TEMPFILE ))
	{
		hr = StringCchPrintfEx(	usedCommand,X_LINELEN,NULL,NULL,0, "del %s", topofile );
		if (FAILED(hr))	
		{
			StringCchPrintfEx(usedCommand,X_LINELEN,NULL,NULL,0,"used Command");
			printCopyError(usedCommand,hr);
		}
		
		if (system(usedCommand))
		{
			printf("Temporary file cleanup failed\n");
			exit(1);
		}
	}
	
	return 0;
}




