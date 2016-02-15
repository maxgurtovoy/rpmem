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

#ifndef RPMEM_COMMON_H
#define RPMEM_COMMON_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <sys/queue.h>

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include "rpmem_protocol.h"

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                       \
        for ((var) = SLIST_FIRST((head));                                \
                        (var) && ((tvar) = SLIST_NEXT((var), field), 1); \
                        (var) = (tvar))
#endif

#define MAX_BUF_SIZE	128

/*
 * conn struct.
 */
struct rpmem_conn {

	/* for server  */
	SLIST_ENTRY(rpmem_conn)		entry;

	struct rdma_cm_id 		*cma_id;

	struct ibv_device		*ib_device;
	struct ibv_device_attr        	dev_attr;
	struct ibv_pd                	*pd;
	struct ibv_cq                	*cq;
	struct ibv_qp                	*qp;
	struct ibv_comp_channel      	*comp_channel;

	pthread_t	                cqthread;

	struct ibv_mr			*req_mr;
	struct ibv_mr			*rsp_mr;

	struct rpmem_req		req;
	struct rpmem_rsp		rsp;

};

int get_addr(char *dst_addr, struct sockaddr *addr, uint16_t port);

int rpmem_post_recv(struct rpmem_conn *conn, struct rpmem_cmd *cmd, struct ibv_mr *mr);
int rpmem_post_send(struct rpmem_conn *conn, struct rpmem_cmd *cmd, struct ibv_mr *mr);
int rpmem_post_rdma_read(struct rpmem_conn *conn,
			 uint32_t rkey,
			 uint32_t lkey,
			 uint64_t remote_addr,
			 void* local_addr,
			 int len);


#endif /* RPMEM_COMMON_H */
