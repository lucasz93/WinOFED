/*
 * Copyright (c) 2013 Oce Printing Systems GmbH.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 *      - Neither the name Oce Printing Systems GmbH nor the names
 *        of the authors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED  “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * OR CONTRIBUTOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE. 
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <tchar.h>
#include <Sddl.h>

#include <rdma/rwinsock.h>
#include <rdma/rsocksvc.h>

#define RS_NETSTAT_MAX_ENTRIES  1024
#define RS_NETSTAT_MAPPING_SIZE ( RS_NETSTAT_MAX_ENTRIES * sizeof(RS_NETSTAT_ENTRY) )

// Function prototypes
DWORD ServiceStart(DWORD dwArgc, LPTSTR *lpszArgv);
VOID  ServiceStop();
BOOL  ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
void  AddToMessageLog(LPTSTR lpszMsg, DWORD dwErr);

// internal variables
static HANDLE					  hMapFile = NULL;
static SERVICE_STATUS			 ssStatus;       // current status of the service
static SERVICE_STATUS_HANDLE	sshStatusHandle = 0;
static BOOL						  bDebug = FALSE;
static TCHAR					 szErr[256];

// internal function prototypes
VOID WINAPI service_ctrl(DWORD dwCtrlCode);
VOID WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv);
VOID CmdInstallService();
VOID CmdRemoveService();
VOID CmdDebugService(int argc, char **argv);
BOOL WINAPI ControlHandler ( DWORD dwCtrlType );
LPTSTR GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize );

//
//  FUNCTION: main
//
//  PURPOSE: entrypoint for service
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    main() either performs the command line task, or
//    call StartServiceCtrlDispatcher to register the
//    main service thread.  When the this call returns,
//    the service has stopped, so exit.
//
void __cdecl main(int argc, char **argv)
{
	SERVICE_TABLE_ENTRY dispatchTable[] = {
      { TEXT(SZSERVICENAME), (LPSERVICE_MAIN_FUNCTION)service_main},
      { NULL, NULL}
	};

   if ( (argc > 1) &&
        ((*argv[1] == '-') || (*argv[1] == '/')) ) {
      if      ( 0 == _stricmp( "install", argv[1]+1 ) )
         CmdInstallService();
      else if ( 0 == _stricmp( "remove",  argv[1]+1 ) )
         CmdRemoveService();
      else if ( 0 == _stricmp( "debug",   argv[1]+1 ) ) {
         bDebug = TRUE;
         CmdDebugService(argc, argv);
      } else
         goto dispatch;

      exit(0);
   }

   // if it doesn't match any of the above parameters
   // the service control manager may be starting the service
   // so we must call StartServiceCtrlDispatcher
dispatch:
   // this is just to be friendly
   printf( "%s -install          to install the service\n", SZAPPNAME );
   printf( "%s -remove           to remove the service\n", SZAPPNAME );
   printf( "%s -debug <params>   to run as a console app for debugging\n", SZAPPNAME );
   printf( "\nStartServiceCtrlDispatcher being called.\n" );
   printf( "This may take several seconds.  Please wait.\n" );

   if (!StartServiceCtrlDispatcher(dispatchTable))
      AddToMessageLog(TEXT("StartServiceCtrlDispatcher failed."), GetLastError());
}

//
//  FUNCTION: service_main
//
//  PURPOSE: To perform actual initialization of the service
//
//  PARAMETERS:
//    dwArgc   - number of command line arguments
//    lpszArgv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    This routine performs the service initialization and then calls
//    the user defined ServiceStart() routine to perform majority
//    of the work.
//
void WINAPI service_main (DWORD dwArgc, LPTSTR *lpszArgv)
{
	DWORD dwErr = 0;

   // register our service control handler:
   //
   sshStatusHandle = RegisterServiceCtrlHandler( TEXT(SZSERVICENAME), service_ctrl);

   if ( !sshStatusHandle ) {
		dwErr = GetLastError();
		AddToMessageLog(TEXT("RegisterServiceCtrlHandler"), dwErr);
		goto cleanup;
   }

   ZeroMemory(&ssStatus, sizeof(ssStatus));

   // SERVICE_STATUS members that don't change
   //
   ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
   ssStatus.dwServiceSpecificExitCode = 0;

   // report the status to the service control manager.
   //
   if (!ReportStatusToSCMgr(
                           SERVICE_START_PENDING, // service state
                           NO_ERROR,              // exit code
                           3000)) {               // wait hint
		AddToMessageLog(TEXT("ReportStatusToSCMgr"), dwErr = GetLastError());
		goto cleanup;
	}

   	dwErr = ServiceStart( dwArgc, lpszArgv );
	if (dwErr)
		AddToMessageLog(TEXT("ServiceStart"), dwErr);

cleanup:

   // report the current status to the service control manager.
   //
   if (sshStatusHandle)
      (VOID)ReportStatusToSCMgr(
                               ssStatus.dwCurrentState,
                               dwErr,
                               0);

   return;
}

static DWORD ServiceStart (DWORD dwArgc, LPTSTR *lpszArgv)
{
	RS_NETSTAT_ENTRY*	pNetstat = NULL;
	SECURITY_ATTRIBUTES	security;
	int   i;
	DWORD dwErr = 0;

	ZeroMemory(&security, sizeof(security));
	security.nLength = sizeof(security);
	ConvertStringSecurityDescriptorToSecurityDescriptor(
			 TEXT("D:P(A;OICI;GA;;;WD)"),
			 SDDL_REVISION_1,
			 &security.lpSecurityDescriptor,
			 NULL);

	hMapFile = CreateFileMapping(
					INVALID_HANDLE_VALUE,	// use paging file
					&security,				// default security 
					  PAGE_READWRITE
					| SEC_COMMIT,			// read/write access
					0,						// max. object size
					RS_NETSTAT_MAPPING_SIZE,// buffer size
					rsNetstatMapping		// name of mapping object
				);
	if (hMapFile == NULL) {
		AddToMessageLog(TEXT("CreateFileMapping"), dwErr = GetLastError());
		return dwErr;
	}

	pNetstat = (PRS_NETSTAT_ENTRY) MapViewOfFile(
									hMapFile,            // handle to map object
									FILE_MAP_ALL_ACCESS, // read/write permission
									0,                   
									0,                   
									RS_NETSTAT_MAPPING_SIZE
								);
	if (pNetstat == NULL) {
		AddToMessageLog(TEXT("MapViewOfFile"), dwErr = GetLastError());
		return dwErr;
	}

	for (i = 0; i < RS_NETSTAT_MAX_ENTRIES; i++)
		pNetstat[i].s = (int) INVALID_SOCKET;

	UnmapViewOfFile(pNetstat);

	ssStatus.dwCurrentState = SERVICE_RUNNING;

	return 0;
}

static void ServiceStop ()
{
	if (hMapFile)
		CloseHandle(hMapFile);

	ssStatus.dwCurrentState = SERVICE_STOPPED;
}

//
//  FUNCTION: service_ctrl
//
//  PURPOSE: This function is called by the SCM whenever
//           ControlService() is called on this service.
//
//  PARAMETERS:
//    dwCtrlCode - type of control requested
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID WINAPI service_ctrl (DWORD dwCtrlCode)
{
   // Handle the requested control code.
   //
   switch (dwCtrlCode) {
   // Stop the service.
   //
   // SERVICE_STOP_PENDING should be reported before
   // setting the Stop Event - hServerStopEvent - in
   // ServiceStop().  This avoids a race condition
   // which may result in a 1053 - The Service did not respond...
   // error.
   case SERVICE_CONTROL_STOP:
   case SERVICE_CONTROL_SHUTDOWN:
      ReportStatusToSCMgr(SERVICE_STOP_PENDING, NO_ERROR, 0);
      ServiceStop();
      break;

      // Update the service status.
      //
   case SERVICE_CONTROL_INTERROGATE:
      break;

      // invalid control code
      //
   default:
      break;

   }

   ReportStatusToSCMgr(ssStatus.dwCurrentState, NO_ERROR, 0);
}

//
//  FUNCTION: ReportStatusToSCMgr()
//
//  PURPOSE: Sets the current status of the service and
//           reports it to the Service Control Manager
//
//  PARAMETERS:
//    dwCurrentState - the state of the service
//    dwWin32ExitCode - error code to report
//    dwWaitHint - worst case estimate to next checkpoint
//
//  RETURN VALUE:
//    TRUE  - success
//    FALSE - failure
//
//  COMMENTS:
//
BOOL ReportStatusToSCMgr(DWORD dwCurrentState,
                         DWORD dwWin32ExitCode,
                         DWORD dwWaitHint)
{
   static DWORD dwCheckPoint = 1;
          BOOL  fResult      = TRUE;

   if ( !bDebug ) { // when debugging we don't report to the SCM
      if (dwCurrentState == SERVICE_START_PENDING ||
	      dwCurrentState == SERVICE_STOP)
         ssStatus.dwControlsAccepted = 0;
      else
         ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

      ssStatus.dwCurrentState  = dwCurrentState;
      ssStatus.dwWin32ExitCode = dwWin32ExitCode;
      ssStatus.dwWaitHint      = dwWaitHint;

      if ( ( dwCurrentState == SERVICE_RUNNING ) ||
           ( dwCurrentState == SERVICE_STOPPED ) )
         ssStatus.dwCheckPoint = 0;
      else
         ssStatus.dwCheckPoint = dwCheckPoint++;

      // Report the status of the service to the service control manager.
      //
      if (!(fResult = SetServiceStatus( sshStatusHandle, &ssStatus)))
         AddToMessageLog(TEXT("SetServiceStatus"), GetLastError());
   }

   return fResult;
}

//
//  FUNCTION: AddToMessageLog(LPTSTR lpszMsg, DWORD dwErr)
//
//  PURPOSE: Allows any thread to log an error message
//
//  PARAMETERS:
//    lpszMsg - text for message
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID AddToMessageLog (LPTSTR lpszMsg, DWORD dwErr)
{
	TCHAR    szMsg [(sizeof(SZSERVICENAME) / sizeof(TCHAR)) + 100 ];
	HANDLE    hEventSource;
	LPTSTR lpszStrings[2];
	
   if ( !bDebug ) {
      // Use event logging to log the error.
      //
      hEventSource = RegisterEventSource(NULL, TEXT(SZSERVICENAME));

      _stprintf_s(szMsg,(sizeof(SZSERVICENAME) / sizeof(TCHAR)) + 100, TEXT("%s error: %d"), TEXT(SZSERVICENAME), dwErr);
      lpszStrings[0] = szMsg;
      lpszStrings[1] = lpszMsg;

      if (hEventSource) {
         ReportEvent(hEventSource, // handle of event source
                     EVENTLOG_ERROR_TYPE,  // event type
                     0,                    // event category
                     0,                    // event ID
                     NULL,                 // current user's SID
                     2,                    // strings in lpszStrings
                     0,                    // no bytes of raw data
                     lpszStrings,          // array of error strings
                     NULL);                // no raw data

		 (VOID) DeregisterEventSource(hEventSource);
      }
   }
}

///////////////////////////////////////////////////////////////////
//
//  The following code handles service installation and removal
//

//
//  FUNCTION: CmdInstallService()
//
//  PURPOSE: Installs the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdInstallService()
{
   SC_HANDLE schService;
   SC_HANDLE schSCManager;
   DWORD     dwRetry = 30;
   SERVICE_DESCRIPTION Description = {
							TEXT("Provides global storage for RSocket status and statistics information.")
						};
   TCHAR szPath[512];

   if ( GetModuleFileName( NULL, szPath, 512 ) == 0 ) {
      _tprintf(TEXT("Unable to install %s - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
      return;
   }

   schSCManager = OpenSCManager(
                               NULL,                        // machine (NULL == local)
                               NULL,                        // database (NULL == default)
                                 SC_MANAGER_CONNECT
							   | SC_MANAGER_CREATE_SERVICE  // access required
                               );
   if ( schSCManager ) {
      schService = CreateService(
                                schSCManager,               // SCManager database
                                TEXT(SZSERVICENAME),        // name of service
                                TEXT(SZSERVICEDISPLAYNAME), // name to display
								  SERVICE_START             // desired access
                                | SERVICE_QUERY_STATUS
								| SERVICE_CHANGE_CONFIG,
                                SERVICE_WIN32_OWN_PROCESS,  // service type
                                SERVICE_AUTO_START,         // start type
                                SERVICE_ERROR_NORMAL,       // error control type
                                szPath,                     // service's binary
                                NULL,                       // no load ordering group
                                NULL,                       // no tag identifier
                                TEXT(SZDEPENDENCIES),       // dependencies
                                NULL,                       // LocalSystem account
                                NULL);                      // no password
      if ( schService ) {
		 _tprintf(TEXT("%s installed.\n"), TEXT(SZSERVICEDISPLAYNAME) );

         if ( !ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &Description) )
            _tprintf(TEXT("ChangeServiceConfig2 failed - %s\n"), GetLastErrorText(szErr, 256));

         if ( StartService( schService, 0, NULL ) ) {
            _tprintf(TEXT("Starting %s."), TEXT(SZSERVICEDISPLAYNAME));
            Sleep( 100 );

            while ( dwRetry-- // Avoid infinite loop
			        && QueryServiceStatus( schService, &ssStatus )
			        && ssStatus.dwCurrentState == SERVICE_START_PENDING ) {
               _tprintf(TEXT("."));
               Sleep( 100 );
            }

            if ( ssStatus.dwCurrentState == SERVICE_RUNNING )
               _tprintf(TEXT("\n%s started.\n"), TEXT(SZSERVICEDISPLAYNAME) );
            else
               _tprintf(TEXT("\n%s failed to start - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
         } else
            _tprintf(TEXT("Failed to start %s - %s"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));

         CloseServiceHandle(schService);
      } else
         _tprintf(TEXT("CreateService failed - %s\n"), GetLastErrorText(szErr, 256));

      CloseServiceHandle(schSCManager);
   } else
      _tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr, 256));
}

//
//  FUNCTION: CmdRemoveService()
//
//  PURPOSE: Stops and removes the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdRemoveService()
{
   SC_HANDLE schService;
   SC_HANDLE schSCManager;
   DWORD     dwRetry = 30;

   schSCManager = OpenSCManager(
                               NULL,                // machine  (NULL == local)
                               NULL,                // database (NULL == default)
                               SC_MANAGER_CONNECT   // access required
                               );
   if ( schSCManager ) {
      schService = OpenService(
                      schSCManager,
					  TEXT(SZSERVICENAME),
					    DELETE
					  | SERVICE_STOP
					  | SERVICE_QUERY_STATUS);
      if (schService) {
	     if (QueryServiceStatus( schService, &ssStatus )
			 && ssStatus.dwCurrentState == SERVICE_RUNNING ) {
			 // try to stop the service
			 if ( ControlService( schService, SERVICE_CONTROL_STOP, &ssStatus ) ) {
				_tprintf(TEXT("Stopping %s."), TEXT(SZSERVICEDISPLAYNAME));
				Sleep( 100 );

				while ( dwRetry-- // Avoid infinite loop
						&& QueryServiceStatus( schService, &ssStatus )
						&& ssStatus.dwCurrentState == SERVICE_STOP_PENDING ) {
				   _tprintf(TEXT("."));
				   Sleep( 100 );
				}

				if ( ssStatus.dwCurrentState == SERVICE_STOPPED )
				   _tprintf(TEXT("\n%s stopped.\n"), TEXT(SZSERVICEDISPLAYNAME) );
				else
				   _tprintf(TEXT("\n%s failed to stop - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
			 } else
				_tprintf(TEXT("Failed to stop %s - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
         }

         // now remove the service
         if ( DeleteService(schService) )
            _tprintf(TEXT("%s removed.\n"), TEXT(SZSERVICEDISPLAYNAME) );
         else
            _tprintf(TEXT("DeleteService failed - %s\n"), GetLastErrorText(szErr, 256));

         CloseServiceHandle(schService);
      } else
         _tprintf(TEXT("OpenService failed - %s\n"), GetLastErrorText(szErr, 256));

      CloseServiceHandle(schSCManager);
   } else
      _tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr, 256));
}

///////////////////////////////////////////////////////////////////
//
//  The following code is for running the service as a console app
//

//
//  FUNCTION: CmdDebugService(int argc, char ** argv)
//
//  PURPOSE: Runs the service as a console application
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdDebugService(int argc, char ** argv)
{
   DWORD dwArgc;
   LPTSTR *lpszArgv;

#ifdef UNICODE
   lpszArgv = CommandLineToArgvW(GetCommandLineW(), &(dwArgc) );
   if (NULL == lpszArgv) {
       // CommandLineToArvW failed!!
       _tprintf(TEXT("CmdDebugService CommandLineToArgvW returned NULL\n"));
       return;
   }
#else
   dwArgc   = (DWORD) argc;
   lpszArgv = argv;
#endif

   _tprintf(TEXT("Debugging %s.\n"), TEXT(SZSERVICEDISPLAYNAME));

   SetConsoleCtrlHandler( ControlHandler, TRUE );

   ServiceStart( dwArgc, lpszArgv );

#ifdef UNICODE
// Must free memory allocated for arguments

   GlobalFree(lpszArgv);
#endif // UNICODE

}

//
//  FUNCTION: ControlHandler ( DWORD dwCtrlType )
//
//  PURPOSE: Handled console control events
//
//  PARAMETERS:
//    dwCtrlType - type of control event
//
//  RETURN VALUE:
//    True - handled
//    False - unhandled
//
//  COMMENTS:
//
BOOL WINAPI ControlHandler ( DWORD dwCtrlType )
{
   switch ( dwCtrlType ) {
   case CTRL_BREAK_EVENT:  // use Ctrl+C or Ctrl+Break to simulate
   case CTRL_C_EVENT:      // SERVICE_CONTROL_STOP in debug mode
      _tprintf(TEXT("Stopping %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
      ServiceStop();
      return TRUE;
      break;
   }

   return FALSE;
}

//
//  FUNCTION: GetLastErrorText
//
//  PURPOSE: copies error message text to string
//
//  PARAMETERS:
//    lpszBuf - destination buffer
//    dwSize - size of buffer
//
//  RETURN VALUE:
//    destination buffer
//
//  COMMENTS:
//
LPTSTR GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize )
{
   DWORD dwRet;
   LPTSTR lpszTemp = NULL;

   dwRet = FormatMessage(  FORMAT_MESSAGE_ALLOCATE_BUFFER
						 | FORMAT_MESSAGE_FROM_SYSTEM
						 | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                          NULL,
                          GetLastError(),
                          LANG_NEUTRAL,
                          (LPTSTR)&lpszTemp,
                          0,
                          NULL );
   // supplied buffer is not long enough
   if ( !dwRet || ( (long)dwSize < (long)dwRet+14 ) )
      lpszBuf[0] = TEXT('\0');
   else if (NULL != lpszTemp) {
           lpszTemp[lstrlen(lpszTemp)-2] = TEXT('\0');  //remove cr and newline character
           _stprintf_s( lpszBuf, dwSize, TEXT("%s (0x%x)"), lpszTemp, GetLastError() );
   }

   if ( lpszTemp )
      LocalFree((HLOCAL) lpszTemp );

   return lpszBuf;
}
