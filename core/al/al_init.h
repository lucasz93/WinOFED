/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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


#if !defined(_IB_AL_INIT_H_)
#define _IB_AL_INIT_H_

#include <iba/ib_types.h>


/****i* AL/al_initialize
* NAME
*	This function initializes the Access Layer.
*
* SYNOPSIS
*/
AL_EXPORT ib_api_status_t AL_API
al_initialize( void );
/*
* DESCRIPTION
*	This function performs a global initialization of the
*	Access Layer (AL)
*
* PARAMETERS
*	None
*
* RETURN VALUE
*	Status -
*	 TBD
*
* PORTABILITY
*	Kernel mode only.
*
* SEE ALSO
*	al_cleanup
*********/


/****i* AL/al_cleanup
* NAME
*	This function cleans up resources allocated during al_initialize.
*
* SYNOPSIS
*/
AL_EXPORT void AL_API
al_cleanup( void );
/*
* DESCRIPTION
*	This function frees up resources used by the access layer.
*
* PARAMETERS
*	None
*
* RETURN VALUE
*
* PORTABILITY
*	Kernel mode only.
*
* SEE ALSO
*	al_initialize
*********/

#endif /* _IB_AL_INIT_H_ */
