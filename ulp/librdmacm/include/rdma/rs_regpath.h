/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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

#ifndef _RS_REGPATH_H_
#define _RS_REGPATH_H_

/* these definitions are common for installSP and WSD projects */
#define RS_PM_REGISTRY_PATH	\
	TEXT("SYSTEM\\CurrentControlSet\\Services\\RSockets\\")
#define RS_PM_EVENTLOG_PATH	\
	TEXT("SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\RSockets")
#define RS_PM_SUBKEY_NAME		TEXT("RSockets")
#define RS_PM_SUBKEY_PERF		TEXT("Performance")
#define RS_PM_INI_FILE		"rs_perfcounters.ini"
#define RS_PM_SYM_H_FILE		"rs_perfini.h"

enum RS_PM_COUNTERS
{
	BYTES_SEND = 0,
	BYTES_RECV,
	BYTES_WRITE,
	BYTES_READ,
	BYTES_TOTAL,
	COMP_SEND,
	COMP_RECV,
	COMP_TOTAL,
	INTR_TOTAL,
	RS_PM_NUM_COUNTERS
};

/* counter symbol names */
#define RS_PM_OBJ						0
#define RS_PM_COUNTER( X )			((X + 1) * 2)

#endif /* _RS_REGPATH_H_ */
