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

#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "rpmem_common.h"

struct inargs {
        char    *server_addr;
	uint16_t server_port;
};

/*
 * unit struct.
 */
struct rpmem_server_unit {
	struct rpmem_server		*server;

	struct rpmem_conn		conn;
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

};

static int get_addr(char *dst_addr, struct sockaddr *addr, uint16_t port)
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

        conn->comp_channel = ibv_create_comp_channel(cma_id->verbs);
	if (!conn->comp_channel) {
		perror("ibv_create_comp_channel failed");
		goto pd_error;
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

	//pthread_create(&conn->cqthread, NULL, cq_thread, conn);

	return 0;

cq_error:
        ibv_destroy_cq(conn->cq);
comp_error:
	ibv_destroy_comp_channel(conn->comp_channel);
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
	init_attr.cap.max_recv_sge = 2;
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
	 * TODO: Post buffers for incoming messages.
	 */

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
        printf("  -a, --addr=<addr>          server IP address\n");
        printf("  -p, --port=<port>          server\n");
        printf("  -h, --help                 display this output\n");
}

static int process_inargs(int argc, char *argv[], struct inargs *in)
{
        int err;
        struct option long_options[] = {
                { .name = "addr",      .has_arg = 1, .val = 'a' },
                { .name = "port",      .has_arg = 1, .val = 'p' },
                { .name = "help",      .has_arg = 0, .val = 'h' },
                { 0 }
        };
	char *short_options = "a:p:h";
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
        struct inargs in;
        int err;

        err = process_inargs(argc, argv, &in);
        if (err)
                return err;

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
	free(in.server_addr);

	return 1;
}

