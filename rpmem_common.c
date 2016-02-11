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

#include "rpmem_common.h"

int get_addr(char *dst_addr, struct sockaddr *addr, uint16_t port)
{
        struct addrinfo *res;
        int ret;

        ret = getaddrinfo(dst_addr, NULL, NULL, &res);
        if (ret) {
                fprintf(stderr, "getaddrinfo failed - invalid hostname or IP address\n");
                return ret;
        }

        if (res->ai_family == PF_INET) {
                memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
		((struct sockaddr_in *)addr)->sin_port = port;
	} else if (res->ai_family == PF_INET6) {
                memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in6));
		((struct sockaddr_in6 *)addr)->sin6_port = port;
	} else
                ret = -1;

        freeaddrinfo(res);

        return ret;
}

int rpmem_post_send(struct rpmem_conn *conn) {

	int ret;
	struct ibv_send_wr send_wr;
	struct ibv_send_wr *send_wr_failed;
	struct ibv_sge sge;

	sge.addr   = (uintptr_t)conn->send_buf;
	sge.length = MAX_BUF_SIZE;
	sge.lkey   = conn->send_mr->lkey;

	memset(&send_wr, 0, sizeof(send_wr));
	send_wr.next       = NULL;
	send_wr.wr_id      = (uintptr_t)conn->send_buf;
	send_wr.sg_list    = &sge;
	send_wr.num_sge    = 1;
	send_wr.opcode     = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED;

	ret = ibv_post_send(conn->qp, &send_wr, &send_wr_failed);
	if (ret) {
		perror("ibv_post_send");
		return -1;
	}

	return 0;
}

int rpmem_post_recv(struct rpmem_conn *conn) {

	struct ibv_recv_wr recv_wr;
	struct ibv_recv_wr *recv_wr_failed;
	struct ibv_sge sge;
	int ret;

	memset(conn->recv_buf, 0, MAX_BUF_SIZE);
	sge.addr   = (uintptr_t)conn->recv_buf;
	sge.length = MAX_BUF_SIZE;
	sge.lkey   = conn->recv_mr->lkey;

	memset(&recv_wr, 0, sizeof(recv_wr));
	recv_wr.wr_id   = (uintptr_t)conn->recv_buf;
	recv_wr.sg_list = &sge;
	recv_wr.num_sge = 1;
	recv_wr.next = NULL;

	ret = ibv_post_recv(conn->qp, &recv_wr, &recv_wr_failed);
	if (ret) {
		perror("ibv_post_recv");
		return -1;
	}

	return 0;
}

