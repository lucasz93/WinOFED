/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#if !defined(__UAL_RES_MGR_H__)
#define __UAL_RES_MGR_H__

/* Commented out until we define them */
/*
#include "ual_pd.h"
#include "ual_mr.h"
#include "ual_mw.h"
#include "ual_qp.h"
#include "ual_cq.h"
#include "ual_av.h"
#include "ual_mcast.h"
*/



/*
 * Global handle to the access layer.  This is used by internal components
 * when calling the external API.  This handle is initialized by the access
 * layer manager.
 */
extern ib_al_handle_t			gh_al;


/*
 *
 *
 * Resource list structure with a lock
 *
 */
typedef struct _ual_res
{
	cl_qlist_t		list;
	cl_spinlock_t	lock;

} ual_res_t;

#endif // __UAL_RES_MGR_H__
