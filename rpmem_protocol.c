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
void pack_open_req(const char *pathname, int flags, void *buf)
{
	uint32_t path_len = strlen(pathname) + 1;
	char *buffer = (char *)buf;
	uint32_t overall_size = sizeof(flags) + path_len;
	struct rpmem_req req = { RPMEM_OPEN_REQ, overall_size };

	pack_mem(pathname, path_len,
	pack_u32((uint32_t *)&flags,
	pack_u32(&req.data_len,
	pack_u32(&req.cmd,
		 buffer))));

}

/*---------------------------------------------------------------------------*/
/* pack_open_rsp				                             */
/*---------------------------------------------------------------------------*/
void pack_open_rsp(int fd, void *buf)
{
	char *buffer = (char *)buf;
	uint32_t overall_size = sizeof(fd);
	struct rpmem_rsp rsp = { RPMEM_OPEN_RSP, overall_size };

	pack_u32((uint32_t *)&fd,
	pack_u32(&rsp.data_len,
	pack_u32(&rsp.cmd,
		 buffer)));


}

/*---------------------------------------------------------------------------*/
/* unpack_open_rsp                                                           */
/*---------------------------------------------------------------------------*/
int unpack_open_rsp(char *buf, int *fd)
{
	char *buffer = (char *)buf;
	struct rpmem_rsp rsp;

	unpack_u32((uint32_t *)fd,
	unpack_u32(&rsp.data_len,
	unpack_u32(&rsp.cmd,
                   buffer)));

	if ((rsp.cmd != RPMEM_OPEN_RSP) || sizeof(*fd) != rsp.data_len) {
		printf("got %d cmd\n", rsp.cmd);
                return -EINVAL;
	}

	return 0;
}

/*---------------------------------------------------------------------------*/
/* pack_close_req                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_req(int fd, void *buf)
{
        char            *buffer = buf;
        unsigned int    overall_size = sizeof(fd);
        struct rpmem_req req = { RPMEM_CLOSE_REQ, overall_size };

        pack_u32((uint32_t *)&fd,
        pack_u32(&req.data_len,
        pack_u32(&req.cmd,
                 buffer)));

}

/*---------------------------------------------------------------------------*/
/* pack_close_rsp                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_rsp(int ret, void *buf)
{
        char            *buffer = buf;
        unsigned int    overall_size = sizeof(ret);
        struct rpmem_rsp rsp = { RPMEM_CLOSE_RSP, overall_size };

        pack_u32((uint32_t *)&ret,
        pack_u32(&rsp.data_len,
        pack_u32(&rsp.cmd,
                 buffer)));

}

/*---------------------------------------------------------------------------*/
/* unpack_close_rsp                                                          */
/*---------------------------------------------------------------------------*/
int unpack_close_rsp(char *buf, int *ret)
{
	char *buffer = (char *)buf;
	struct rpmem_rsp rsp;

	unpack_u32((uint32_t *)ret,
	unpack_u32(&rsp.data_len,
	unpack_u32(&rsp.cmd,
                   buffer)));

	if ((rsp.cmd != RPMEM_CLOSE_RSP) || sizeof(*ret) != rsp.data_len) {
		printf("got %d cmd\n", rsp.cmd);
                return -EINVAL;
	}

	return 0;
}

