/*
* Copyright (c) 2016 Mellanox Technologies.  All rights reserved.
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
*/

#include "rpmem_protocol.h"

/*---------------------------------------------------------------------------*/
/* pack_open_req				                             */
/*---------------------------------------------------------------------------*/
void pack_open_req(void *buf)
{
	char *buffer = (char *)buf;
	struct rpmem_req req = { RPMEM_OPEN_REQ, 0 };

	pack_u32(&req.opcode, buffer);

}

/*---------------------------------------------------------------------------*/
/* pack_open_rsp				                             */
/*---------------------------------------------------------------------------*/
void pack_open_rsp(int size, void *buf)
{
	char *buffer = (char *)buf;
	struct rpmem_rsp rsp = { RPMEM_OPEN_RSP, 0 };

	pack_u32((uint32_t *)&size,
	pack_u32(&rsp.opcode,
		 buffer));


}

/*---------------------------------------------------------------------------*/
/* unpack_open_rsp                                                           */
/*---------------------------------------------------------------------------*/
int unpack_open_rsp(char *buf, int *size)
{
	char *buffer = (char *)buf;
	struct rpmem_rsp rsp;

	unpack_u32((uint32_t *)size,
	unpack_u32(&rsp.opcode,
                   buffer));

	if (rsp.opcode != RPMEM_OPEN_RSP) {
		printf("got %d opcode\n", rsp.opcode);
                return -EINVAL;
	}

	return 0;
}

/*---------------------------------------------------------------------------*/
/* pack_close_req                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_req(void *buf)
{
        char            *buffer = buf;
        struct rpmem_req req = { RPMEM_CLOSE_REQ, 0 };

        pack_u32(&req.opcode, buffer);

}

/*---------------------------------------------------------------------------*/
/* pack_close_rsp                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_rsp(int ret, void *buf)
{
        char            *buffer = buf;
        struct rpmem_rsp rsp = { RPMEM_CLOSE_RSP, 0 };

        pack_u32((uint32_t *)&ret,
        pack_u32(&rsp.opcode,
                 buffer));

}

/*---------------------------------------------------------------------------*/
/* unpack_close_rsp                                                          */
/*---------------------------------------------------------------------------*/
int unpack_close_rsp(char *buf, int *ret)
{
	char *buffer = (char *)buf;
	struct rpmem_rsp rsp;

	unpack_u32((uint32_t *)ret,
	unpack_u32(&rsp.opcode,
                   buffer));

	if (rsp.opcode != RPMEM_CLOSE_RSP) {
		printf("got %d opcode\n", rsp.opcode);
                return -EINVAL;
	}

	return 0;
}

