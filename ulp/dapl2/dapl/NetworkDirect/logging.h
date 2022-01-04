// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

_inline_ void INIT_LOG( const LPWSTR testname )
{
	wprintf( L"Beginning test: %s\n", testname );
}

inline void END_LOG( const LPWSTR testname ) 
{
	wprintf( L"End of test: %s\n", testname );
}

inline void LOG_FAILURE_HRESULT_AND_EXIT( HRESULT hr, const LPWSTR errormessage, int LINE ) 
{
	wprintf( errormessage, hr );
	wprintf( L"  Line: %d\n", LINE );

	exit( LINE );
}

inline void LOG_FAILURE_AND_EXIT( const LPWSTR errormessage, int LINE ) 
{
	wprintf( errormessage );
	wprintf( L"  Line: %d\n", LINE );

	exit( LINE );
}

inline void LOG_FAILURE_HRESULT( HRESULT hr, const LPWSTR errormessage, int LINE ) 
{
	wprintf( errormessage, hr );
	wprintf( L"  Line: %d\n", LINE );
}

inline void LOG_FAILURE( const LPWSTR errormessage, int LINE ) 
{
	wprintf( errormessage );
	wprintf( L"  Line: %d\n", LINE );
}
