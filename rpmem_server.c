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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <linux/fs.h>

#include "rpmem_common.h"


struct inargs {
        char     *server_addr;
	uint16_t server_port;
        char     *resource;
};

/*
 * resource struct.
 */
struct rpmem_resource {
	int				fd;
	struct stat64			stbuf;
	char				*name;
	struct ibv_mr			*mr; // mapped buffer mr
	void				*addr; // mapped buffer
	int				map_len; // mapped len

	SLIST_ENTRY(rpmem_resource)	entry;
};

/*
 * unit struct.
 */
struct rpmem_server_unit {
	struct rpmem_server		*server;

	struct rpmem_conn		conn;
	struct rpmem_resource		*res;
};

/*
 * server struct.
 */
struct rpmem_server {
	struct sockaddr_storage ssin;

	struct rdma_cm_id		*cma_id;
	struct rdma_event_channel	*cma_channel;

	pthread_t	                cmthread;
	sem_t 				sem_connect;
	
	pthread_mutex_t 		conns_mutex;
	SLIST_HEAD(, rpmem_conn)	conns_list;
	pthread_spinlock_t              res_lock;
	SLIST_HEAD(, rpmem_resource)	res_list;
	int				total_size;
	int				in_use_size;
	int				free_size;

};

static int rpmem_bind_server(struct rpmem_server *server, struct inargs *in)
{
	int ret;

	ret = get_addr(in->server_addr, (struct sockaddr *)&server->ssin, in->server_port);
	if (ret) {
		perror("get_addr");
		return ret;
	}

	ret = rdma_bind_addr(server->cma_id, (struct sockaddr *) &server->ssin);
	if (ret) {
		perror("rdma_bind_addr");
		return ret;
	}
	printf("rdma_bind_addr successful\n");

	printf("rdma_listen\n");
	ret = rdma_listen(server->cma_id, 3);
	if (ret) {
		perror("rdma_listen");
		return ret;
	}

	return 0;
}

static int rpmem_handle_open(struct rpmem_conn *conn)
{
	struct rpmem_server_unit *unit = container_of(conn, struct rpmem_server_unit, conn);
	int size;
	int err, ret = 0;

	printf("conn %p got open req\n", conn);

	size = unit->server->free_size;

	err = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (err) {
		perror("rpmem_post_recv");
		ret = -1;
		goto out;
	}
out:
	pack_open_rsp(size, &conn->rsp);

	err = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (err) {
		perror("rpmem_post_send");
		ret = -1;
	}

	return ret;

}

static int rpmem_handle_close(struct rpmem_conn *conn)
{
	int ret = 0;

	printf("conn %p: got close request\n", conn);

	pack_close_rsp(ret, &conn->rsp);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_send");
		ret = -1;
	}

	return ret;
}

static int rpmem_handle_map(struct rpmem_conn *conn)
{
	struct rpmem_server_unit *unit = container_of(conn, struct rpmem_server_unit, conn);
	struct rpmem_server *server = unit->server;
	struct rpmem_map_req *req = (struct rpmem_map_req *)&conn->req;
	struct rpmem_resource *res, *tmp_res;
	void *addr;
	uint32_t rkey;
	int len;
	int err, ret = 0;

	len = ntohl(req->len);

	printf("conn %p got map req %d\n", conn, len);


	err = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (err) {
		perror("rpmem_post_recv");
		ret = -1;
		goto out;
	}

	pthread_spin_lock(&server->res_lock);
	SLIST_FOREACH_SAFE(res, &server->res_list, entry, tmp_res) {
		printf("resource %p fd %d size %d name %s\n", res, res->fd, (int)res->stbuf.st_size, res->name);
		if (len < (int)res->stbuf.st_size) {
			printf("found suitable space in fd %d\n", res->fd);
			unit->res = res;
			server->free_size -= (int)(res->stbuf.st_size);
			server->in_use_size += (int)(res->stbuf.st_size);
			SLIST_REMOVE(&server->res_list,
				     res,
				     rpmem_resource,
				     entry);
			break;
		}
	}
	pthread_spin_unlock(&server->res_lock);

	if (!unit->res) {
		printf("didn't find suitable mapping\n");
		ret = -1;
		goto out;
	}

	if (posix_fallocate(res->fd, 0, len)) {
		perror("posix_fallocate");
		ret = -1;
        }

	addr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE, res->fd, 0);
	if (addr == MAP_FAILED) {
		addr = NULL;
		printf("failed to map\n");
		ret = -1;
	}

	res->addr = addr;
	res->map_len = len;
	res->mr = ibv_reg_mr(conn->pd,
			     addr,
			     len,
			     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	if (!res->mr) {
		perror("map ibv_reg_mr");
		munmap(addr, len);
		addr = NULL;
		ret = -1;
		goto out;
	}

	rkey = (uint32_t)(res->mr->rkey);
out:
	printf("map rkey %d addr %lld\n", (int)rkey, (long long int)addr);
	pack_map_rsp(&conn->rsp, rkey, (uint64_t)addr);

	err = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (err) {
		perror("rpmem_post_send");
		ret = -1;
	}

	return ret;

}

static int rpmem_handle_unmap(struct rpmem_conn *conn)
{
	struct rpmem_server_unit *unit = container_of(conn, struct rpmem_server_unit, conn);
	struct rpmem_server *server = unit->server;
	struct rpmem_unmap_req *req = (struct rpmem_unmap_req *)&conn->req;
	struct rpmem_resource *res = unit->res;
	uint64_t addr;
	int len;
	int err, ret = 0;

	len = ntohl(req->len);
	addr = be64toh(req->remote_addr);

	printf("conn %p got unmap req %d addr %lld\n", conn, len, (long long int)addr);

	err = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (err) {
		perror("rpmem_post_recv");
		ret = -1;
		goto out;
	}

	/* initialize all dynamic stuff */
	ibv_dereg_mr(res->mr);
	res->map_len = 0;
	res->addr = 0;

	ret = munmap((void *)addr, len);
	if (ret) {
		perror("munmap");
		goto out;
	}


	pthread_spin_lock(&server->res_lock);
	SLIST_INSERT_HEAD(&server->res_list, res, entry);
	printf("resource %p fd %d size %d name %s is BACK to the pool\n", res, res->fd, (int)res->stbuf.st_size, res->name);
	unit->res = NULL;
	server->free_size += (int)(res->stbuf.st_size);
	server->in_use_size -= (int)(res->stbuf.st_size);
	pthread_spin_unlock(&server->res_lock);

out:
	pack_unmap_rsp(&conn->rsp, ret);

	err = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (err) {
		perror("rpmem_post_send");
		ret = -1;
	}

	return ret;

}

int
rpmem_rcv_completion(struct rpmem_conn *conn)
{
	int opcode = ntohl(conn->req.opcode);

	printf("conn %p rpmem_rcv_completion %d opcode\n", conn, opcode);


	switch (opcode) {
	case RPMEM_OPEN_REQ:
		rpmem_handle_open(conn);
		break;
	case RPMEM_CLOSE_REQ:
		rpmem_handle_close(conn);
		break;
	case RPMEM_MAP_REQ:
		rpmem_handle_map(conn);
		break;
	case RPMEM_UNMAP_REQ:
		rpmem_handle_unmap(conn);
		break;
	default:
		printf("conn %p unknown request %d\n",
			conn,
			opcode);
		break;
	};
	return 0;
}

int
rpmem_snd_completion(struct rpmem_conn *conn)
{

	printf("conn %p rpmem_snd_completion\n", conn);
	/* For now don't need to do anything here */
	return 0;
}

static void rpmem_handle_wc(struct ibv_wc *wc,
			    struct rpmem_conn *conn)
{
	printf("conn %p handle opcode %d status %d wc %p\n", conn, wc->opcode, wc->status, wc);

	if (wc->status == IBV_WC_SUCCESS) {
		if (wc->opcode == IBV_WC_RECV) {
			rpmem_rcv_completion(conn);
		} else if (wc->opcode == IBV_WC_SEND) {
			rpmem_snd_completion(conn);
		} else {
			printf("conn %p got unknown opcode %d wc %p\n", conn, wc->opcode, wc);
		}
	} else {
		if (wc->status == IBV_WC_WR_FLUSH_ERR) {
			printf("conn %p got flush err %p wc\n", conn, wc);
		} else {
			printf("conn %p got err_comp %p wc\n", conn, wc);
		}
	}

}

static int cq_event_handler(struct rpmem_conn *conn)
{
	struct ibv_wc wc[16];
	unsigned int i;
	unsigned int n;
	unsigned int completed = 0;

	while ((n = ibv_poll_cq(conn->cq, 16, wc)) > 0) {
		for (i = 0; i < n; i++)
			rpmem_handle_wc(&wc[i], conn);

		completed += n;
		if (completed >= 64)
			break;
	}

        if (n) {
                perror("poll error");
                return -1;
        }

        return 0;
}

static void *cq_thread(void *arg)
{
	struct rpmem_conn *conn = arg;
        struct ibv_cq *ev_cq;
        void *ev_ctx;
        int ret;

        while (1) {
                ret = ibv_get_cq_event(conn->comp_channel, &ev_cq, &ev_ctx);
                if (ret) {
                        perror("Failed to get cq event!");
                        pthread_exit(NULL);
                }

                if (ev_cq != conn->cq) {
                        perror("Unknown CQ!");
                        pthread_exit(NULL);
                }

                ret = ibv_req_notify_cq(conn->cq, 0);
                if (ret) {
                        perror("Failed to set notify!");
                        pthread_exit(NULL);
                }

                ret = cq_event_handler(conn);

                ibv_ack_cq_events(conn->cq, 1);
                if (ret)
                        pthread_exit(NULL);
        }
}

static int rpmem_conn_init(struct rpmem_conn *conn)
{
	struct rdma_cm_id *cma_id = conn->cma_id;
	int max_cqe = 128;
	int err = 1;


        conn->pd = ibv_alloc_pd(cma_id->verbs);
        if (!conn->pd) {
		perror("ibv_alloc_pd");
		goto error;
        }

	conn->req_mr = ibv_reg_mr(conn->pd,
				  &conn->req,
				  RPMEM_CMD_SIZE,
				  IBV_ACCESS_LOCAL_WRITE);
	if (!conn->req_mr) {
		perror("recv ibv_reg_mr");
		goto pd_error;
	}
	conn->rsp_mr = ibv_reg_mr(conn->pd,
				  &conn->rsp,
				  RPMEM_CMD_SIZE,
				  IBV_ACCESS_LOCAL_WRITE);
	if (!conn->rsp_mr) {
		perror("send ibv_reg_mr");
		goto recv_mr_error;
	}

        conn->comp_channel = ibv_create_comp_channel(cma_id->verbs);
	if (!conn->comp_channel) {
		perror("ibv_create_comp_channel failed");
		goto send_mr_error;
	}

	printf("conn %p creating CQ\n", conn);

	conn->cq = ibv_create_cq(cma_id->verbs,
		      	         max_cqe,
			         conn,
				 conn->comp_channel,
				 0);
        if (!conn->cq) {
		perror("Failed to create cq");
		goto comp_error;
	}

	printf("cq: %p\n", conn->cq);

        if (ibv_req_notify_cq(conn->cq, 0)) {
		perror("ibv_req_notify_cq failed");
		goto cq_error;
	}

	pthread_create(&conn->cqthread, NULL, cq_thread, conn);

	return 0;

cq_error:
        ibv_destroy_cq(conn->cq);
comp_error:
	ibv_destroy_comp_channel(conn->comp_channel);
send_mr_error:
	ibv_dereg_mr(conn->rsp_mr);
recv_mr_error:
	ibv_dereg_mr(conn->req_mr);
pd_error:
        ibv_dealloc_pd(conn->pd);
error:
	return err;
}

static int rpmem_create_qp(struct rpmem_conn *conn)
{
	struct ibv_qp_init_attr init_attr;
	int ret;

	memset(&init_attr, 0, sizeof(init_attr));
        init_attr.qp_context  = (void *) conn;
	init_attr.cap.max_send_wr = 128;
	init_attr.cap.max_recv_wr = 128;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.send_cq = conn->cq;
	init_attr.recv_cq = conn->cq;

	ret = rdma_create_qp(conn->cma_id, conn->pd, &init_attr);
	if (ret) {
		perror("rdma_create_qp");
		return -1;
	}

        conn->qp = conn->cma_id->qp;

	return ret;
}


static void rpmem_connect_request_handler(struct rdma_cm_event *ev)
{
	struct rdma_cm_id *cma_id = ev->id;
	struct rpmem_server *server = cma_id->context;
        struct rdma_conn_param conn_param;
	struct rpmem_server_unit *unit;
	struct rpmem_conn *conn;
	int err;

	/* build a new unit structure */
	unit = calloc(1, sizeof(struct rpmem_server_unit));
	if (!unit) {
		printf("cm_id:%p malloc unit failed\n", cma_id);
		goto reject;
	}
	unit->server = server;
	conn = &unit->conn;
	conn->cma_id = cma_id;
	err = rpmem_conn_init(conn);
	if (err) {
		free(unit);
		goto reject;
	}

	printf("alloc conn:%p cm_id:%p\n", conn, cma_id);

	/* replace cma_id context from rpmem_server to rpmem_conn for future */
	cma_id->context = conn;

	err = rpmem_create_qp(conn);
	if (err) {
		/* TODO: free resources from rpmem_conn_init */
		free(unit);
		goto reject;
	}

	printf("conn:%p cm_id:%p, created qp:%p\n", conn, cma_id, conn->qp);

	/*
	 * Post incoming buffer.
	 */
	err = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (err) {
		perror("rpmem_post_recv");
		goto free_conn;
	}

        memset(&conn_param, 0, sizeof(struct rdma_conn_param));
        conn_param.responder_resources = 1;
        conn_param.initiator_depth     = 1;
        conn_param.retry_count         = 7;
        conn_param.rnr_retry_count     = 7;

	/* now we can actually accept the connection */
	err = rdma_accept(conn->cma_id, &conn_param);
	if (err) {
		printf("conn:%p cm_id:%p rdma_accept failed, %m\n",
			conn, cma_id);
		goto free_conn;
	}

	pthread_mutex_lock(&server->conns_mutex);
	SLIST_INSERT_HEAD(&server->conns_list, conn, entry);
	pthread_mutex_unlock(&server->conns_mutex);

	return;

free_conn:
/* TODO: add free_conn */
reject:
	err = rdma_reject(cma_id, NULL, 0);
	if (err)
		printf("cm_id:%p rdma_reject failed, %m\n", cma_id);
}


/*
 * Handle RDMA CM events.
 */
static int rpmem_server_cma_handler(struct rdma_cm_id *cma_id,
			            struct rdma_cm_event *event)

{
        int ret = 0;

	printf("ctx %p cma_event type %s cma_id %p\n", cma_id->context, rdma_event_str(event->event), cma_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		rpmem_connect_request_handler(event);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_REJECTED:
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_DISCONNECTED:
		break;
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		break;
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
	case RDMA_CM_EVENT_UNREACHABLE:
		printf("Active side event:%d, %s - ignored\n", event->event,
			rdma_event_str(event->event));
		break;

	default:
		printf("Illegal event:%d - ignored\n", event->event);
		break;
	}

	return ret;

}

static void *cm_thread(void *arg)
{
	struct rpmem_server *server = arg;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		ret = rdma_get_cm_event(server->cma_channel, &event);
		if (ret) {
			perror("rdma_get_cm_event");
			pthread_exit(NULL);
		}
		ret = rpmem_server_cma_handler(event->id, event);
		if (ret) {
			perror("Failed to handle event");
			pthread_exit(NULL);
		}
		rdma_ack_cm_event(event);
	}
}

static void usage(const char *argv0)
{
        printf("Usage:\n");
        printf("  %s            start RPMEM server\n", argv0);
        printf("\n");
        printf("Options:\n");
        printf("  -a, --addr=<addr>           server IP address\n");
        printf("  -p, --port=<port>           server port\n");
        printf("  -r, --resources=<path>      resource for mapping\n");
        printf("  -h, --help                  display this output\n");
}

static int process_inargs(int argc, char *argv[], struct inargs *in)
{
        struct option long_options[] = {
                { .name = "addr",      .has_arg = 1, .val = 'a' },
                { .name = "port",      .has_arg = 1, .val = 'p' },
                { .name = "resource",  .has_arg = 1, .val = 'r' },
                { .name = "help",      .has_arg = 0, .val = 'h' },
                { 0 }
        };
	char *short_options = "a:p:r:h";
	int c;
	
	optind = 0;
	opterr = 0;

	while (1) {
		c = getopt_long(argc, argv, short_options,
				long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			in->server_addr = strdup(optarg);
			break;
		case 'p':
			in->server_port = htons((uint16_t)strtol(optarg, NULL, 0));
			break;
		case 'r':
			in->resource = strdup(optarg);
			break;
		case 'h':
			usage(argv[0]);
			if (in->server_addr)
				free(in->server_addr);
			exit(0);
			break;
		default:
			fprintf(stderr, "\nError:\n\tInvalid param: %s\n",
				argv[optind - 1]);
			goto cleanup;
			break;
		}
	}
	if (argc == 1)
		usage(argv[0]);
	if (optind < argc) {
		fprintf(stderr, "\nError:\n\tInvalid param: %s\n",
			argv[optind]);
		goto cleanup;
	}

	return 0;

cleanup:
	if (in->server_addr)
		free(in->server_addr);

	fprintf(stderr,	"Failed to parse command line params.\n\n");
	usage(argv[0]);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct rpmem_server server;
	struct rpmem_resource res;
        struct inargs in;
        int err;

        err = process_inargs(argc, argv, &in);
        if (err)
                return err;


	res.name = in.resource;
	res.fd = open(res.name, O_RDWR);
	if (res.fd == -1) {
		perror("open");
		goto out;
	}
	err = fstat64(res.fd, &res.stbuf);
	if (err == 0) {
		if (S_ISBLK(res.stbuf.st_mode)) {
			err = ioctl(res.fd, BLKGETSIZE64, &res.stbuf.st_size);
			if (err < 0) {
				printf("Cannot get size from %s\n", res.name);
				goto close_fd;
			}
		}
	} else {
		printf("Cannot stat file %s\n", res.name);
		goto close_fd;
        }

	printf("file %s size %d fd %d\n", res.name, (int)res.stbuf.st_size, res.fd);

	pthread_spin_init(&server.res_lock, PTHREAD_PROCESS_PRIVATE);
	SLIST_INIT(&server.res_list);
	SLIST_INIT(&server.conns_list);

	pthread_spin_lock(&server.res_lock);
	SLIST_INSERT_HEAD(&server.res_list, &res, entry);
	server.total_size = (int)res.stbuf.st_size;
	server.free_size = (int)res.stbuf.st_size;
	server.in_use_size = 0;
	pthread_spin_unlock(&server.res_lock);

	pthread_mutex_init(&server.conns_mutex, NULL);
	sem_init(&server.sem_connect, 0, 0);

        server.cma_channel = rdma_create_event_channel();
	if (!server.cma_channel) {
		perror("rdma_create_event_channel");
		goto free_inargs;
	}

	if (rdma_create_id(server.cma_channel, &server.cma_id, &server, RDMA_PS_TCP)) {
		perror("rdma_create_id");
		goto destroy_event_channel;
	}

	err = pthread_create(&server.cmthread, NULL, cm_thread, &server);
	if (err) {
		perror("Failed create Connection Manager Thread");
		goto destroy_id;
	}

	printf("binding server %p cma_id %p\n", &server, server.cma_id);

	err = rpmem_bind_server(&server, &in);
        if (err) {
                perror("bind server");
                goto destroy_thread;
        }

	/* join the thread */
	pthread_join(server.cmthread, NULL);

        return 0;

destroy_thread:
        pthread_cancel(server.cmthread);
destroy_id:
	rdma_destroy_id(server.cma_id);
destroy_event_channel:
	rdma_destroy_event_channel(server.cma_channel);
free_inargs:
	sem_destroy(&server.sem_connect);
	pthread_mutex_destroy(&server.conns_mutex);
	pthread_spin_destroy(&server.res_lock);
close_fd:
	close(res.fd);
out:
	free(in.resource);
	free(in.server_addr);

	return 1;
}

