/*
 * Copyright (c) 2005 Mellanox Technologies LTD.  All rights reserved.
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
 *
 */

// Author: Yossi Leybovich 

#include "mlx4_debug.h"
#include <stdio.h> 
#include <stdarg.h>
#include  <strsafe.h>

#if !defined(EVENT_TRACING)


#if DBG 
uint32_t g_mlx4_dbg_level = TRACE_LEVEL_WARNING;
uint32_t g_mlx4_dbg_flags= MLX4_DBG_QP | MLX4_DBG_CQ | MLX4_DBG_MEMORY;
const int MLX4_PRINT_HELPER = 0;
#endif

VOID
_MLX4_PRINT(
    IN char*   msg,
    ...
    )

 {
#if DBG
#define     TEMP_BUFFER_SIZE        1024
    va_list    list;
    CHAR      debugMessageBuffer[TEMP_BUFFER_SIZE];
    HRESULT result;
    
    va_start(list, msg);
    
    if (msg) {

        //
        // Using new safe string functions instead of _vsnprintf. This function takes
        // care of NULL terminating if the message is longer than the buffer.
        //
        
        result = StringCbVPrintfA (debugMessageBuffer, sizeof(debugMessageBuffer), 
                                    msg, list);
        if(((HRESULT)(result) < 0)) {
            
            OutputDebugString (": StringCbVPrintfA failed \n");
            return;
        }
        OutputDebugString ( debugMessageBuffer);

    }
    va_end(list);

    return;
#else
	UNUSED_PARAM(msg);
#endif //DBG
}

#endif //EVENT_TRACING

