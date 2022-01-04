/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#ifndef _IPOIB_XFR_MGR_H_
#define _IPOIB_XFR_MGR_H_


#include <iba/ib_al.h>
#include <complib/cl_types.h>
#include <complib/cl_qpool.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_qlist.h>
#include <complib/cl_qpool.h>
#include <complib/cl_list.h>


#include "ipoib_driver.h"
#include "ip_stats.h"
#include <ip_packet.h>


#include <complib/cl_packon.h>
/****s* IPoIB Driver/ipoib_hw_addr_t
* NAME
*   ipoib_hw_addr_t
*
* DESCRIPTION
*   The ipoib_hw_addr_t structure defines an IPoIB compatible hardware
*   address.  Values in this structure are stored in network order.
*
* SYNOPSIS
*/
typedef struct _ipoib_hw_addr
{
	uint32_t	flags_qpn;
	ib_gid_t	gid;

}	PACK_SUFFIX ipoib_hw_addr_t;
/*
* FIELDS
*	flags_qpn
*		Flags and queue pair number.  Use ipoib_addr_get_flags,
*		ipoib_addr_set_flags, ipoib_addr_set_qpn, and ipoib_addr_get_qpn
*		to manipulate the contents.
*
*	gid
*		IB GID value.
*
* SEE ALSO
*	IPoIB, ipoib_addr_get_flags, ipoib_addr_set_flags, ipoib_addr_set_qpn,
*	ipoib_addr_get_qpn
*********/
#include <complib/cl_packoff.h>

/****s* IPoIB Driver/ipoib_guid2mac_translation_t
* NAME
*   ipoib_guid2mac_translation_t
*
* DESCRIPTION
*   The ipoib_guid2mac_translation_t structure defines a GUID to MAC translation.
*   The structure holds map between known OUI to an appropriate GUID mask.
*
* SYNOPSIS
*/
typedef struct _ipoib_guid2mac_translation_
{
	uint8_t first_byte;
	uint8_t second_byte;
	uint8_t third_byte;
	uint8_t guid_mask;
	
}	ipoib_guid2mac_translation_t;
/*
* FIELDS
*	second_byte
*		second byte of OUI (located in lower three bytes of GUID).
*
*	third_byte
*		third byte of OUI (located in lower three bytes of GUID).
*
*	guid_mask
*		GUID mask that will be used to generate MAC from the GUID.      
*
* SEE ALSO
*	IPoIB, ipoib_mac_from_guid_mask
*********/

extern const ipoib_guid2mac_translation_t guid2mac_table[];

#ifdef __cplusplus
extern "C"
{
#endif


/*
 * Address accessors
 */

static inline uint8_t
ipoib_addr_get_flags(
	IN		const	ipoib_hw_addr_t* const		p_addr )
{
	return (uint8_t)(cl_ntoh32( p_addr->flags_qpn ) >> 24);
}

static inline void
ipoib_addr_set_flags(
	IN				ipoib_hw_addr_t* const		p_addr,
	IN		const	uint8_t						flags )
{
	p_addr->flags_qpn &= cl_ntoh32( 0xFFFFFF00 );
	p_addr->flags_qpn |= cl_ntoh32( flags );
}

static inline net32_t
ipoib_addr_get_qpn(
	IN		const	ipoib_hw_addr_t* const		p_addr )
{
	return cl_ntoh32( cl_ntoh32( p_addr->flags_qpn ) >> 8 );
}

static inline void
ipoib_addr_set_qpn(
	IN				ipoib_hw_addr_t* const		p_addr,
	IN		const	net32_t						qpn )
{
	p_addr->flags_qpn = cl_ntoh32( (cl_ntoh32(
		p_addr->flags_qpn ) & 0x000000FF ) | (cl_ntoh32( qpn ) << 8) );
}


/****f* IPOIB/ipoib_mac_from_sst_guid
* NAME
*	ipoib_mac_from_sst_guid
*
* DESCRIPTION
*	Generates an ethernet MAC address given a SilverStorm port GUID.
*
* SYNOPSIS
*/
static inline ib_api_status_t
ipoib_mac_from_sst_guid(
	IN		const	net64_t						port_guid,
		OUT			mac_addr_t* const			p_mac_addr )
{
	const uint8_t	*p_guid = (const uint8_t*)&port_guid;
	uint32_t		low24;

	/* Port guid is in network byte order.  OUI is in lower 3 bytes. */
	ASSERT( p_guid[0] == 0x00 && p_guid[1] == 0x06 && p_guid[2] == 0x6a );

	/*
	 * We end up using only the lower 23-bits of the GUID.  Trap that
	 * the 24th (bit 23) through 27th (bit 26) bit aren't set.
	 */
	if( port_guid & CL_HTON64( 0x0000000007800000 ) )
		return IB_INVALID_GUID;

	low24 = 0x00FFF000 -
		((((uint32_t)cl_ntoh64( port_guid ) & 0x00FFFFFF) - 0x101) * 2);
	low24 -= p_guid[3]; /* minus port number */

	p_mac_addr->addr[0] = p_guid[0];
	p_mac_addr->addr[1] = p_guid[1];
	p_mac_addr->addr[2] = p_guid[2];
	p_mac_addr->addr[3] = (uint8_t)(low24 >> 16);
	p_mac_addr->addr[4] = (uint8_t)(low24 >> 8);
	p_mac_addr->addr[5] = (uint8_t)low24;
	
	return IB_SUCCESS;
}
/*
* PARAMETERS
*	port_guid
*		The port GUID, in network byte order, for which to generate a
*		MAC address.
*
*	p_mac_addr
*		Pointer to a mac address in which to store the results.
*
* RETURN VALUES
*	IB_SUCCESS
*		The MAC address was successfully converted.
*
*	IB_INVALID_GUID
*		The port GUID provided was not a known GUID format.
*
* NOTES
*	The algorithm to convert portGuid to MAC address is as per DN0074, and
*	assumes a 2 port HCA.
*
* SEE ALSO
*	IPOIB
*********/


/****f* IPOIB/ipoib_mac_from_mlx_guid
* NAME
*	ipoib_mac_from_mlx_guid
*
* DESCRIPTION
*	Generates an ethernet MAC address given a Mellanox port GUID.
*
* SYNOPSIS
*/
static inline ib_api_status_t
ipoib_mac_from_mlx_guid(
	IN		const	net64_t						port_guid,
		OUT			mac_addr_t* const			p_mac_addr )
{
	const uint8_t	*p_guid = (const uint8_t*)&port_guid;
	uint32_t		low24;
	net16_t			guid_middle;

	/* Port guid is in network byte order.  OUI is in lower 3 bytes. */
	ASSERT( p_guid[0] == 0x00 && p_guid[1] == 0x02 && p_guid[2] == 0xc9 );

	guid_middle = (net16_t)((port_guid & CL_HTON64( 0x000000ffff000000 )) >>24);

	if (guid_middle == 2) {
			p_mac_addr->addr[0] = 0;
	} else if (guid_middle == 3) {
			p_mac_addr->addr[0] = 2;
	} else {
		return IB_INVALID_GUID;
	}
	low24 = ((uint32_t)cl_ntoh64( port_guid ) & 0x00FFFFFF);

	p_mac_addr->addr[1] = p_guid[1];
	p_mac_addr->addr[2] = p_guid[2];
	p_mac_addr->addr[3] = (uint8_t)(low24 >> 16);
	p_mac_addr->addr[4] = (uint8_t)(low24 >> 8);
	p_mac_addr->addr[5] = (uint8_t)low24;
	
	return IB_SUCCESS;
}
/*
* PARAMETERS
*	port_guid
*		The port GUID, in network byte order, for which to generate a
*		MAC address.
*
*	p_mac_addr
*		Pointer to a mac address in which to store the results.
*
* RETURN VALUES
*	IB_SUCCESS
*		The MAC address was successfully converted.
*
*	IB_INVALID_GUID
*		The port GUID provided was not a known GUID format.
*
*********/


/****f* IPOIB/ipoib_mac_from_voltaire_guid
* NAME
*	ipoib_mac_from_voltaire_guid
*
* DESCRIPTION
*	Generates an ethernet MAC address given a Voltaire port GUID.
*
* SYNOPSIS
*/
static inline ib_api_status_t
ipoib_mac_from_voltaire_guid(
	IN		const	net64_t						port_guid,
		OUT			mac_addr_t* const			p_mac_addr )
{
	const uint8_t	*p_guid = (const uint8_t*)&port_guid;

	/* Port guid is in network byte order.  OUI is in lower 3 bytes. */
	ASSERT( p_guid[0] == 0x00 && p_guid[1] == 0x08 && p_guid[2] == 0xf1 );

	p_mac_addr->addr[0] = p_guid[0];
	p_mac_addr->addr[1] = p_guid[1];
	p_mac_addr->addr[2] = p_guid[2];
	p_mac_addr->addr[3] = p_guid[4] ^ p_guid[6];
	p_mac_addr->addr[4] = p_guid[5] ^ p_guid[7];
	p_mac_addr->addr[5] = p_guid[5] + p_guid[6] + p_guid[7];

	return IB_SUCCESS;
}


/****f* IPOIB/ipoib_mac_from_guid_mask
* NAME
*	ipoib_mac_from_guid_mask
*
* DESCRIPTION
*	Generates an ethernet MAC address given general port GUID and a bitwise mask
*
* SYNOPSIS
*/
static inline ib_api_status_t
ipoib_mac_from_guid_mask(
	IN		const	uint8_t						*p_guid,
	IN				uint32_t					guid_mask,
		OUT			mac_addr_t* const			p_mac_addr )
{
	static const mac_addr_size =  HW_ADDR_LEN;
	uint8_t i;
	int digit_counter = 0;

	// All non-zero bits of guid_mask indicates the number of an appropriate
	// byte in port_guid, that will be used in MAC address construction
	for (i = 7; guid_mask; guid_mask >>= 1, --i )
	{
		if( guid_mask & 1 )
		{
			++digit_counter;
			if( digit_counter > mac_addr_size )
			{
				//to avoid negative index
				return IB_INVALID_GUID_MASK;
			}
			p_mac_addr->addr[mac_addr_size - digit_counter] = p_guid [i];
		}
	}

	// check for the mask validity: it should have 6 non-zero bits
	if( digit_counter != mac_addr_size )
		return IB_INVALID_GUID_MASK;

	return IB_SUCCESS;
}
/*
* PARAMETERS
*	port_guid
*		The port GUID, in network byte order, for which to generate a
*		MAC address.
*
*	guid_mask
*		Each BIT in the mask indicates whether to include the appropriate BYTE
*		to the MAC address. Bit 0 corresponds to the less significant BYTE , i.e.
*		highest index in the MAC array
*
*	p_mac_addr
*		Pointer to a mac address in which to store the results.
*
* RETURN VALUES
*	IB_SUCCESS
*		The MAC address was successfully converted.
*
*	IB_INVALID_GUID
*		The port GUID provided was not a known GUID format.
*
* SEE ALSO
*	IPOIB
*********/


/****f* IPOIB/ipoib_mac_from_guid
* NAME
*	ipoib_mac_from_guid
*
* DESCRIPTION
*	Generates an ethernet MAC address given a port GUID.
*
* SYNOPSIS
*/
static inline ib_api_status_t
ipoib_mac_from_guid(
	IN		const	net64_t						port_guid,
	IN				uint32_t					guid_mask,
		OUT			mac_addr_t* const			p_mac_addr
		)
{
	ib_api_status_t	status = IB_INVALID_GUID;
	const uint8_t	*p_guid = (const uint8_t*)&port_guid;
	uint32_t		laa, idx = 0;

	/* Port guid is in network byte order.  OUI is in lower 3 bytes. */

	if( p_guid[0] == 0 && p_guid[1] == 0x02 && p_guid[2] == 0xc9 )
		{
			status = ipoib_mac_from_mlx_guid( port_guid, p_mac_addr );
		}
	else if( p_guid[0] == 0 && p_guid[1] == 0x08 && p_guid[2] == 0xf1 )
		{
			status = ipoib_mac_from_voltaire_guid( port_guid, p_mac_addr );
		}
	else if( p_guid[0] == 0 && p_guid[1] == 0x06 && p_guid[2] == 0x6a )
		{
			status = ipoib_mac_from_sst_guid( port_guid, p_mac_addr );
		}
		else
		{
			while( guid2mac_table[idx].second_byte != 0x00 ||
			   guid2mac_table[idx].third_byte != 0x00 )  //first byte can be equal to 0
			{
			if( p_guid[0] == guid2mac_table[idx].first_byte &&
				p_guid[1] == guid2mac_table[idx].second_byte &&
					p_guid[2] == guid2mac_table[idx].third_byte )
				{
					status = ipoib_mac_from_guid_mask(p_guid, guid2mac_table[idx].guid_mask,
														p_mac_addr);
					break;
				}
				++idx;
			}
		}

		if( status == IB_SUCCESS )
			return status;


	if( guid_mask )
		return ipoib_mac_from_guid_mask( p_guid, guid_mask, p_mac_addr );

	/* Value of zero is reserved. */
	laa = cl_atomic_inc( &g_ipoib.laa_idx );

	if( !laa )
		return IB_INVALID_GUID;

	p_mac_addr->addr[0] = 2; /* LAA bit */
	p_mac_addr->addr[1] = 0;
	p_mac_addr->addr[2] = (uint8_t)(laa >> 24);
	p_mac_addr->addr[3] = (uint8_t)(laa >> 16);
	p_mac_addr->addr[4] = (uint8_t)(laa >> 8);
	p_mac_addr->addr[5] = (uint8_t)laa;
	
	return IB_SUCCESS;
}
/*
* PARAMETERS
*	port_guid
*		The port GUID, in network byte order, for which to generate a
*		MAC address.
*
*	p_mac_addr
*		Pointer to a mac address in which to store the results.
*
* RETURN VALUES
*	IB_SUCCESS
*		The MAC address was successfully converted.
*
*	IB_INVALID_GUID
*		The port GUID provided was not a known GUID format.
*
* NOTES
*	Creates a locally administered address using a global incrementing counter.
*
* SEE ALSO
*	IPOIB
*********/


/****f* IPOIB/ipoib_is_voltaire_router_gid
* NAME
*	ipoib_is_voltaire_router_gid
*
* DESCRIPTION
*	Checks whether the GID belongs to Voltaire IP router
*
* SYNOPSIS
*/
boolean_t
static inline
ipoib_is_voltaire_router_gid(
	IN		const	ib_gid_t					*p_gid )
{
	static const uint8_t VOLTAIRE_GUID_PREFIX[] = {0, 0x08, 0xf1, 0, 0x1};

	return !cl_memcmp( &p_gid->unicast.interface_id, VOLTAIRE_GUID_PREFIX,
		sizeof(VOLTAIRE_GUID_PREFIX) );
}


#ifdef __cplusplus
}
#endif

#endif	/* _IPOIB_XFR_MGR_H_ */
