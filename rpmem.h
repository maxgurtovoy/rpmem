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

#ifndef RPMEM_H
#define RPMEM_H

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>

#include "rpmem_common.h"

enum file_state {
	RPMEM_INIT,
	RPMEM_ERROR,
	RPMEM_ESTABLISHED,
	RPMEM_DISCONNECTED
};

struct rpmem_mr {
	void		*addr;
	size_t		len;
};

struct priv_rpmem_mr {
	struct rpmem_mr		rmr;
	struct ibv_mr		*mr;
	uint32_t		rkey;
	uint64_t		remote_addr;
};


struct rpmem_file {
	struct sockaddr			dst_addr;
	int				size;
	struct rpmem_mr			*rmr;

};

struct priv_rpmem_file {
	struct rpmem_file		rfile;

	struct rpmem_conn		conn;
	struct rdma_event_channel	*cma_channel;

	sem_t	                        sem_connect;
	pthread_t	                cmthread;

	enum file_state         	state;
	pthread_mutex_t 		state_mutex;

	sem_t	                        sem_command;
	struct priv_rpmem_mr		priv_mr;
	/* information of remote persistance */
	//struct list_head		rmap_list;
	//int				nrmaps;
};

struct rpmem_sge {
	off_t			addr;
	size_t			len;
};

/*
struct rpmem_comp {
	void (*done)(struct struct rpmem_comp *comp);
	bool persistent;
};
*/

struct rpmem_file *
rpmem_open(struct sockaddr *dst_addr);

struct rpmem_mr *
rpmem_map(struct rpmem_file *file, size_t len);

void
rpmem_unmap(struct rpmem_mr *mr);

struct rpmem_mr *
rpmem_map_shadow(struct rpmem_file *file,
		 off_t offset, void *addr, size_t len);

int rpmem_close(struct rpmem_file *);

/* blocking */
int rpmem_flush_sync(struct rpmem_mr *mr, struct rpmem_sge *sges, int nr_sges);

/* non-blocking */
/*
int rpmem_flush_async(struct rpmem_sge *sges, int nr_sges,
		      struct rpmem_comp *comp);
*/
/* drains rdma all completions on mr */
int rpmem_drain(struct rpmem_mr *mr);

/* flush async and drain all rpmems */
int rpmem_persist_sync(struct rpmem_sge *sges, int nr_sges);

/* flush async and drain all rpmems */
/*
int rpmem_persist_async(struct rpmem_sge *sges, int nr_sges,
			 struct rpmem_comp *comp);
*/

#endif /* RPMEM_H */
