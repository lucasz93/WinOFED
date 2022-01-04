/*
 * Copyright (c) 2007 Intel Corporation.  All rights reserved.
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

/*
 * broadcast a settings changed message to all top-level windows in the system,
 * including disabled or invisible unowned windows, overlapped windows,
 * and pop-up windows; message is not sent to child windows.
 *
 * Utilized to notify top-level windows that an environment variable has 
 * been modified; specifically PATH. Functionality utilized during WinOF
 * installation.
 *
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
 
int main(int argc, char* argv[])
{
	DWORD dwReturnValue;
	LRESULT rc;
 
	rc = SendMessageTimeout ( HWND_BROADCAST,
				  WM_SETTINGCHANGE,
				  0,
				  (LPARAM) "Environment",
				  SMTO_ABORTIFHUNG,
				  5000,
				  (PDWORD_PTR)&dwReturnValue );
	if (rc != 1)
		printf("%s() SendMessageTimeout() returns %d\n",
			__FUNCTION__,rc);

	return 0;
}

