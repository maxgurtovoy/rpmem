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
void pack_open_req(struct rpmem_req *req)
{
	struct rpmem_open_req *open_req = (struct rpmem_open_req *)req;

	open_req->opcode = htonl(RPMEM_OPEN_REQ);

}

/*---------------------------------------------------------------------------*/
/* pack_open_rsp				                             */
/*---------------------------------------------------------------------------*/
void pack_open_rsp(int size, struct rpmem_rsp *rsp)
{
	struct rpmem_open_rsp *open_rsp = (struct rpmem_open_rsp *)rsp;

	open_rsp->opcode = htonl(RPMEM_OPEN_RSP);
	open_rsp->size = htonl(size);

}

/*---------------------------------------------------------------------------*/
/* unpack_open_rsp                                                           */
/*---------------------------------------------------------------------------*/
int unpack_open_rsp(struct rpmem_rsp *rsp, int *size)
{
	struct rpmem_open_rsp *open_rsp = (struct rpmem_open_rsp *)rsp;

	if (ntohl(open_rsp->opcode) != RPMEM_OPEN_RSP) {
		printf("got %d opcode\n", (int)ntohl(open_rsp->opcode));
                return -EINVAL;
	}

	*size = ntohl(open_rsp->size);

	return 0;
}

/*---------------------------------------------------------------------------*/
/* pack_close_req                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_req(struct rpmem_req *req)
{
	struct rpmem_close_req *close_req = (struct rpmem_close_req *)req;

	close_req->opcode = htonl(RPMEM_CLOSE_REQ);

}

/*---------------------------------------------------------------------------*/
/* pack_close_rsp                                                            */
/*---------------------------------------------------------------------------*/
void pack_close_rsp(int ret, struct rpmem_rsp *rsp)
{
	struct rpmem_close_rsp *close_rsp = (struct rpmem_close_rsp *)rsp;

	close_rsp->opcode = htonl(RPMEM_CLOSE_RSP);
	close_rsp->ret = htonl(ret);

}

/*---------------------------------------------------------------------------*/
/* unpack_close_rsp                                                          */
/*---------------------------------------------------------------------------*/
int unpack_close_rsp(struct rpmem_rsp *rsp, int *ret)
{
	struct rpmem_close_rsp *close_rsp = (struct rpmem_close_rsp *)rsp;

	if (ntohl(close_rsp->opcode) != RPMEM_CLOSE_RSP) {
		printf("got %d opcode\n", (int)ntohl(close_rsp->opcode));
                return -EINVAL;
	}

	*ret = ntohl(close_rsp->ret);

	return 0;
}


/*---------------------------------------------------------------------------*/
/* pack_map_req 				                             */
/*---------------------------------------------------------------------------*/
void pack_map_req(struct rpmem_req *req, int len)
{
	struct rpmem_map_req *map_req = (struct rpmem_map_req *)req;

	map_req->opcode = htonl(RPMEM_MAP_REQ);
	map_req->len = htonl(len);
}

/*---------------------------------------------------------------------------*/
/* pack_map_rsp 				                             */
/*---------------------------------------------------------------------------*/
void pack_map_rsp(struct rpmem_rsp *rsp, uint32_t rkey, uint64_t remote_addr)
{
	struct rpmem_map_rsp *map_rsp = (struct rpmem_map_rsp *)rsp;

	map_rsp->opcode = htonl(RPMEM_MAP_RSP);
	map_rsp->rkey = htobe32(rkey);
	map_rsp->remote_addr = htobe64(remote_addr);

}

/*---------------------------------------------------------------------------*/
/* unpack_map_rsp                                                           */
/*---------------------------------------------------------------------------*/
int unpack_map_rsp(struct rpmem_rsp *rsp, uint32_t *rkey, uint64_t *remote_addr)
{
	struct rpmem_map_rsp *map_rsp = (struct rpmem_map_rsp *)rsp;

	if (ntohl(map_rsp->opcode) != RPMEM_MAP_RSP) {
		printf("got %d opcode\n", (int)ntohl(map_rsp->opcode));
                return -EINVAL;
	}

	*rkey = be32toh(map_rsp->rkey);
	*remote_addr = be64toh(map_rsp->remote_addr);

	return 0;
}

/*---------------------------------------------------------------------------*/
/* pack_unmap_req 				                             */
/*---------------------------------------------------------------------------*/
void pack_unmap_req(struct rpmem_req *req, int len, uint64_t remote_addr)
{
	struct rpmem_unmap_req *unmap_req = (struct rpmem_unmap_req *)req;

	unmap_req->opcode = htonl(RPMEM_UNMAP_REQ);
	unmap_req->len = htonl(len);
	unmap_req->remote_addr = htobe64(remote_addr);

}

/*---------------------------------------------------------------------------*/
/* pack_unmap_rsp                                                            */
/*---------------------------------------------------------------------------*/
void pack_unmap_rsp(struct rpmem_rsp *rsp, int ret)
{
	struct rpmem_unmap_rsp *unmap_rsp = (struct rpmem_unmap_rsp *)rsp;

	unmap_rsp->opcode = htonl(RPMEM_UNMAP_RSP);
	unmap_rsp->ret = htonl(ret);

}

/*---------------------------------------------------------------------------*/
/* unpack_unmap_rsp                                                          */
/*---------------------------------------------------------------------------*/
int unpack_unmap_rsp(struct rpmem_rsp *rsp, int *ret)
{
	struct rpmem_unmap_rsp *unmap_rsp = (struct rpmem_unmap_rsp *)rsp;

	if (ntohl(unmap_rsp->opcode) != RPMEM_UNMAP_RSP) {
		printf("got %d opcode\n", (int)ntohl(unmap_rsp->opcode));
                return -EINVAL;
	}

	*ret = ntohl(unmap_rsp->ret);

	return 0;
}
