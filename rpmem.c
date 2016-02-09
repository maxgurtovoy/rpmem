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


#include "rpmem.h"

/*---------------------------------------------------------------------------*/
/* globals								     */
/*---------------------------------------------------------------------------*/
static SLIST_HEAD(, mlx_rpmem_file) file_list =
	SLIST_HEAD_INITIALIZER(file_list);

pthread_mutex_t file_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static void rpmem_handle_wc(struct ibv_wc *wc,
			    struct mlx_rpmem_file *mlx_rfile)
{
	printf("file %p handle %p wc\n", mlx_rfile, wc);
}

static int cq_event_handler(struct mlx_rpmem_file *mlx_rfile)
{
	struct ibv_wc wc[16];
	unsigned int i;
	unsigned int n;
	unsigned int completed = 0;

	while ((n = ibv_poll_cq(mlx_rfile->cq, 16, wc)) > 0) {
		for (i = 0; i < n; i++)
			rpmem_handle_wc(&wc[i], mlx_rfile);

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
	struct mlx_rpmem_file *mlx_rfile = arg;
        struct ibv_cq *ev_cq;
        void *ev_ctx;
        int ret;

        while (1) {
                ret = ibv_get_cq_event(mlx_rfile->comp_channel, &ev_cq, &ev_ctx);
                if (ret) {
                        perror("Failed to get cq event!");
                        pthread_exit(NULL);
                }

                if (ev_cq != mlx_rfile->cq) {
                        perror("Unknown CQ!");
                        pthread_exit(NULL);
                }

                ret = ibv_req_notify_cq(mlx_rfile->cq, 0);
                if (ret) {
                        perror("Failed to set notify!");
                        pthread_exit(NULL);
                }

                ret = cq_event_handler(mlx_rfile);

                ibv_ack_cq_events(mlx_rfile->cq, 1);
                if (ret)
                        pthread_exit(NULL);
        }
}

static int rpmem_addr_handler(struct rdma_cm_id *cma_id)
{
	struct mlx_rpmem_file *mlx_rfile = cma_id->context;
        int ret, flags, max_cqe;

        ret = rdma_resolve_route(cma_id, 1000);
        if (ret) {
		perror("rdma_resolve_route");
		goto error;
        }

        mlx_rfile->pd = ibv_alloc_pd(cma_id->verbs);
        if (!mlx_rfile->pd) {
		perror("ibv_alloc_pd");
		goto error;
        }

        mlx_rfile->comp_channel = ibv_create_comp_channel(cma_id->verbs);
	if (!mlx_rfile->comp_channel) {
		perror("ibv_create_comp_channel failed");
		goto pd_error;
	}

	/* TODO: check if need to change to nonblock
	flags = fcntl(mlx_rfile->comp_channel->fd, F_GETFL);
	ret = fcntl(mlx_rfile->comp_channel->fd, F_SETFL, flags | O_NONBLOCK);
	if (ret) {
		perror("Failed to set channel to nonblocking");
		goto comp_error;
	}
	*/

        max_cqe = 8;

        mlx_rfile->cq = ibv_create_cq(cma_id->verbs,
				      max_cqe,
				      mlx_rfile,
				      mlx_rfile->comp_channel,
				      0);
        if (!mlx_rfile->cq) {
		perror("Failed to create cq");
		goto comp_error;
	}

        if (ibv_req_notify_cq(mlx_rfile->cq, 0)) {
		perror("ibv_req_notify_cq failed");
		goto cq_error;
	}

	pthread_create(&mlx_rfile->cqthread, NULL, cq_thread, mlx_rfile);

        return 0;

cq_error:
        ibv_destroy_cq(mlx_rfile->cq);
comp_error:
	ibv_destroy_comp_channel(mlx_rfile->comp_channel);
pd_error:
        ibv_dealloc_pd(mlx_rfile->pd);
error:
	sem_post(&mlx_rfile->sem_connect);
        return -1;
}

static int rpmem_route_handler(struct rdma_cm_id *cma_id)
{
	struct mlx_rpmem_file *mlx_rfile = (struct mlx_rpmem_file *)cma_id->context;
        struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr init_attr;
        int ret;

	printf("rfile %p rpmem_route_handler cma_ctx %p id %p\n", mlx_rfile, cma_id->context, mlx_rfile->cma_id);

        memset(&init_attr, 0, sizeof(init_attr));
        init_attr.qp_context  = (void *) mlx_rfile->cma_id->context;
        init_attr.send_cq     = mlx_rfile->cq;
        init_attr.recv_cq     = mlx_rfile->cq;
        init_attr.cap.max_send_wr  = 4;
        init_attr.cap.max_recv_wr  = 4;
        init_attr.cap.max_send_sge = 2;
        init_attr.cap.max_recv_sge = 1;
        init_attr.qp_type     = IBV_QPT_RC;

        ret = rdma_create_qp(mlx_rfile->cma_id, mlx_rfile->pd, &init_attr);
	if (ret) {
		perror("rdma_create_qp");
		return -1;
	}

        mlx_rfile->qp = mlx_rfile->cma_id->qp;

        memset(&conn_param, 0, sizeof(struct rdma_conn_param));
        conn_param.responder_resources = 1;    // Need to be attribute of device 
        conn_param.initiator_depth     = 1;
        conn_param.retry_count         = 7;
        conn_param.rnr_retry_count     = 7;

        ret = rdma_connect(cma_id, &conn_param);
        if (ret) {
		perror("rdma_connect");
		goto destroy_qp;
        }
        return ret;

destroy_qp:
        ibv_destroy_qp(mlx_rfile->qp);

        return -1;
}

static int rpmem_cma_handler(struct rdma_cm_id *cma_id,
			     struct rdma_cm_event *event)
{
	struct mlx_rpmem_file *mlx_rfile = cma_id->context;
        int ret = 0;

	printf("rfile %p cma_event type %s cma_id %p\n", mlx_rfile, rdma_event_str(event->event), cma_id);

	pthread_mutex_lock(&mlx_rfile->state_mutex);
        switch(event->event) {
		case RDMA_CM_EVENT_ADDR_RESOLVED:
			ret = rpmem_addr_handler(cma_id);
                        break;
                case RDMA_CM_EVENT_ROUTE_RESOLVED:
                        ret = rpmem_route_handler(cma_id);
                        break;
                case RDMA_CM_EVENT_ESTABLISHED:
			mlx_rfile->state = RPMEM_ESTABLISHED;
			sem_post(&mlx_rfile->sem_connect);
                        break;
                case RDMA_CM_EVENT_ADDR_ERROR:
                case RDMA_CM_EVENT_ROUTE_ERROR:
                case RDMA_CM_EVENT_CONNECT_ERROR:
                case RDMA_CM_EVENT_UNREACHABLE:
                case RDMA_CM_EVENT_REJECTED:
			mlx_rfile->state = RPMEM_ERROR;
                        ret = -1;
			sem_post(&mlx_rfile->sem_connect);
                        break;
                case RDMA_CM_EVENT_DISCONNECTED:
                case RDMA_CM_EVENT_ADDR_CHANGE:
                case RDMA_CM_EVENT_TIMEWAIT_EXIT:
			mlx_rfile->state = RPMEM_DISCONNECTED;
			ret = -1;
			sem_post(&mlx_rfile->sem_connect);
                        break;
                default:
			mlx_rfile->state = RPMEM_ERROR;
                        ret = -1;
			sem_post(&mlx_rfile->sem_connect);
                        break;
        }
	pthread_mutex_unlock(&mlx_rfile->state_mutex);

        return ret;
}

static void *cm_thread(void *arg)
{
	struct mlx_rpmem_file *mlx_rfile = arg;
        struct rdma_cm_event *event;
        int ret;

        while (1) {
		ret = rdma_get_cm_event(mlx_rfile->cma_channel, &event);
		if (ret) {
			perror("Failed to get RDMA-CM Event");
			pthread_exit(NULL);
		}
		ret = rpmem_cma_handler(mlx_rfile->cma_id, event);
		if (ret) {
			perror("Failed to handle event");
			pthread_exit(NULL);
		}
		rdma_ack_cm_event(event);
	}
}

struct rpmem_file *
rpmem_open(struct sockaddr *dst_addr, char *rfilepath, int flags)
{
	struct mlx_rpmem_file *mlx_rfile;
	struct rpmem_file *rfile;
	int ret;

	mlx_rfile = calloc(1, sizeof(struct mlx_rpmem_file));
	if (!mlx_rfile) {
		perror("No Memory");
		return NULL;
	}

	rfile = &mlx_rfile->rfile;
	pthread_mutex_init(&mlx_rfile->state_mutex, NULL);
	sem_init(&mlx_rfile->sem_connect, 0, 0);

	mlx_rfile->cma_channel = rdma_create_event_channel();
	if (!mlx_rfile->cma_channel) {
		perror("rdma_create_event_channel");
		goto destroy_rfile;
	}

	if (rdma_create_id(mlx_rfile->cma_channel, &mlx_rfile->cma_id, mlx_rfile, RDMA_PS_TCP)) {
		perror("rdma_create_id");
		goto destroy_event_channel;
	}

	ret = pthread_create(&mlx_rfile->cmthread, NULL, cm_thread, mlx_rfile);
	if (ret) {
		perror("Failed create Connection Manager Thread");
		goto destroy_id;
	}

	if (rdma_resolve_addr(mlx_rfile->cma_id, NULL, dst_addr, 2000)) {
		perror("rdma_resolve_addr");
		goto destroy_thread;
	}

	sem_wait(&mlx_rfile->sem_connect);

	pthread_mutex_lock(&mlx_rfile->state_mutex);
	if (mlx_rfile->state != RPMEM_ESTABLISHED) {
		perror("Conn Error");
		pthread_mutex_unlock(&mlx_rfile->state_mutex);
		goto destroy_thread;
	}
	pthread_mutex_unlock(&mlx_rfile->state_mutex);

	/* RDMA connection established - need to open remote file */
	rfile->rfilepath = strdup(rfilepath);
	if (!rfile->rfilepath) {
		perror("strdup");
		goto destroy_thread;
	}

	pthread_mutex_lock(&file_list_mutex);
	SLIST_INSERT_HEAD(&file_list, mlx_rfile, entry);
	pthread_mutex_unlock(&file_list_mutex);

	return rfile;

destroy_thread:
	pthread_cancel(mlx_rfile->cmthread);
destroy_id:
	rdma_destroy_id(mlx_rfile->cma_id);
destroy_event_channel:
	rdma_destroy_event_channel(mlx_rfile->cma_channel);
destroy_rfile:
	sem_destroy(&mlx_rfile->sem_connect);
	pthread_mutex_destroy(&mlx_rfile->state_mutex);
	free(mlx_rfile);

	return NULL;
}

int rpmem_close(struct rpmem_file *rfile)
{
	struct mlx_rpmem_file *mlx_rfile = container_of(rfile, struct mlx_rpmem_file, rfile);

	pthread_mutex_lock(&file_list_mutex);
	SLIST_REMOVE(&file_list,
                     mlx_rfile,
                     mlx_rpmem_file,
                     entry);
	pthread_mutex_unlock(&file_list_mutex);

	free(rfile->rfilepath);

	/* Destroy cm stuff */
	pthread_cancel(mlx_rfile->cmthread);
	rdma_disconnect(mlx_rfile->cma_id);
	ibv_destroy_qp(mlx_rfile->qp);
	rdma_destroy_id(mlx_rfile->cma_id);
	rdma_destroy_event_channel(mlx_rfile->cma_channel);

	/* Destroy cq stuff */
	pthread_cancel(mlx_rfile->cqthread);
        ibv_destroy_cq(mlx_rfile->cq);
	ibv_destroy_comp_channel(mlx_rfile->comp_channel);
	ibv_dealloc_pd(mlx_rfile->pd);

	/* Destroy mlx_rfile stuff */
	sem_destroy(&mlx_rfile->sem_connect);
	pthread_mutex_destroy(&mlx_rfile->state_mutex);
	free(mlx_rfile);

	return 0;
}

