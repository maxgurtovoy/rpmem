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
static SLIST_HEAD(, rpmem_conn) file_list =
	SLIST_HEAD_INITIALIZER(file_list);

pthread_mutex_t file_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static void rpmem_handle_wc(struct ibv_wc *wc,
			    struct priv_rpmem_file *priv_rfile)
{
	struct rpmem_conn *conn = &priv_rfile->conn;

	printf("file %p conn %p handle opcode %d status %d wc %p\n",
		priv_rfile, conn, wc->opcode, wc->status, wc);

	if (wc->status == IBV_WC_SUCCESS) {
		if (wc->opcode == IBV_WC_RECV) {
			sem_post(&priv_rfile->sem_command);
		} else if (wc->opcode == IBV_WC_SEND) {
			printf("conn %p got send_comp %d wc %p\n", conn, wc->opcode, wc);
		} else if (wc->opcode == IBV_WC_RDMA_READ) {
			printf("conn %p got rdma_read %d wc %p\n", conn, wc->opcode, wc);
			sem_post(&priv_rfile->sem_command);
		} else if (wc->opcode == IBV_WC_RDMA_WRITE) {
			printf("conn %p got rdma_write %d wc %p\n", conn, wc->opcode, wc);
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

static int cq_event_handler(struct priv_rpmem_file *priv_rfile)
{
	struct rpmem_conn *conn = &priv_rfile->conn;
	struct ibv_wc wc[16];
	unsigned int i;
	unsigned int n;
	unsigned int completed = 0;

	while ((n = ibv_poll_cq(conn->cq, 16, wc)) > 0) {
		for (i = 0; i < n; i++)
			rpmem_handle_wc(&wc[i], priv_rfile);

		completed += n;
		if (completed >= 64)
			goto out;
	}

        if (n) {
                perror("poll error");
                return -1;
        }
out:
        return 0;
}

static void *cq_thread(void *arg)
{
	struct priv_rpmem_file *priv_rfile = arg;
	struct rpmem_conn *conn = &priv_rfile->conn;
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

                ret = cq_event_handler(priv_rfile);

                ibv_ack_cq_events(conn->cq, 1);
                if (ret)
                        pthread_exit(NULL);
        }
}

static int rpmem_addr_handler(struct rdma_cm_id *cma_id)
{
	struct priv_rpmem_file *priv_rfile = cma_id->context;
	struct rpmem_conn *conn = &priv_rfile->conn;
        int ret, max_cqe;

        ret = rdma_resolve_route(cma_id, 1000);
        if (ret) {
		perror("rdma_resolve_route");
		goto error;
        }

	conn->pd = ibv_alloc_pd(cma_id->verbs);
	if (!conn->pd) {
		perror("ibv_alloc_pd");
		goto error;
	}
	conn->rsp_mr = ibv_reg_mr(conn->pd,
				  &conn->rsp,
				  RPMEM_CMD_SIZE,
				  IBV_ACCESS_LOCAL_WRITE);
	if (!conn->rsp_mr) {
		perror("recv ibv_reg_mr");
		goto pd_error;
	}
	conn->req_mr = ibv_reg_mr(conn->pd,
				  &conn->req,
				  RPMEM_CMD_SIZE,
				  IBV_ACCESS_LOCAL_WRITE);
	if (!conn->req_mr) {
		perror("send ibv_reg_mr");
		goto recv_mr_error;
	}

	conn->comp_channel = ibv_create_comp_channel(cma_id->verbs);
	if (!conn->comp_channel) {
		perror("ibv_create_comp_channel failed");
		goto send_mr_error;
	}

	/* TODO: check if need to change to nonblock
	flags = fcntl(priv_rfile->comp_channel->fd, F_GETFL);
	ret = fcntl(priv_rfile->comp_channel->fd, F_SETFL, flags | O_NONBLOCK);
	if (ret) {
		perror("Failed to set channel to nonblocking");
		goto comp_error;
	}
	*/

        max_cqe = 8;

        conn->cq = ibv_create_cq(cma_id->verbs,
				 max_cqe,
				 priv_rfile,
				 conn->comp_channel,
				 0);
        if (!conn->cq) {
		perror("Failed to create cq");
		goto comp_error;
	}

        if (ibv_req_notify_cq(conn->cq, 0)) {
		perror("ibv_req_notify_cq failed");
		goto cq_error;
	}

	pthread_create(&conn->cqthread, NULL, cq_thread, priv_rfile);

        return 0;

cq_error:
	ibv_destroy_cq(conn->cq);
comp_error:
	ibv_destroy_comp_channel(conn->comp_channel);
send_mr_error:
	ibv_dereg_mr(conn->req_mr);
recv_mr_error:
	ibv_dereg_mr(conn->rsp_mr);
pd_error:
        ibv_dealloc_pd(conn->pd);
error:
	sem_post(&priv_rfile->sem_connect);

        return -1;
}

static int rpmem_route_handler(struct rdma_cm_id *cma_id)
{
	struct priv_rpmem_file *priv_rfile = (struct priv_rpmem_file *)cma_id->context;
	struct rpmem_conn *conn = &priv_rfile->conn;
        struct rdma_conn_param conn_param;
	struct ibv_qp_init_attr init_attr;
        int ret;

	printf("rfile %p rpmem_route_handler cma_ctx %p id %p\n", priv_rfile, cma_id->context,conn->cma_id);

        memset(&init_attr, 0, sizeof(init_attr));
        init_attr.qp_context  = (void *) conn->cma_id->context;
        init_attr.send_cq     = conn->cq;
        init_attr.recv_cq     = conn->cq;
        init_attr.cap.max_send_wr  = 4;
        init_attr.cap.max_recv_wr  = 4;
        init_attr.cap.max_send_sge = 1;
        init_attr.cap.max_recv_sge = 1;
        init_attr.qp_type     = IBV_QPT_RC;

        ret = rdma_create_qp(conn->cma_id, conn->pd, &init_attr);
	if (ret) {
		perror("rdma_create_qp");
		return -1;
	}

        conn->qp = conn->cma_id->qp;

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
        ibv_destroy_qp(conn->qp);

        return -1;
}

static int rpmem_cma_handler(struct rdma_cm_id *cma_id,
			     struct rdma_cm_event *event)
{
	struct priv_rpmem_file *priv_rfile = cma_id->context;
        int ret = 0;

	printf("rfile %p cma_event type %s cma_id %p\n", priv_rfile, rdma_event_str(event->event), cma_id);

	pthread_mutex_lock(&priv_rfile->state_mutex);
        switch(event->event) {
		case RDMA_CM_EVENT_ADDR_RESOLVED:
			ret = rpmem_addr_handler(cma_id);
                        break;
                case RDMA_CM_EVENT_ROUTE_RESOLVED:
                        ret = rpmem_route_handler(cma_id);
                        break;
                case RDMA_CM_EVENT_ESTABLISHED:
			priv_rfile->state = RPMEM_ESTABLISHED;
			sem_post(&priv_rfile->sem_connect);
                        break;
                case RDMA_CM_EVENT_ADDR_ERROR:
                case RDMA_CM_EVENT_ROUTE_ERROR:
                case RDMA_CM_EVENT_CONNECT_ERROR:
                case RDMA_CM_EVENT_UNREACHABLE:
                case RDMA_CM_EVENT_REJECTED:
			priv_rfile->state = RPMEM_ERROR;
                        ret = -1;
			sem_post(&priv_rfile->sem_connect);
                        break;
                case RDMA_CM_EVENT_DISCONNECTED:
                case RDMA_CM_EVENT_ADDR_CHANGE:
                case RDMA_CM_EVENT_TIMEWAIT_EXIT:
			priv_rfile->state = RPMEM_DISCONNECTED;
			ret = -1;
			sem_post(&priv_rfile->sem_connect);
                        break;
                default:
			priv_rfile->state = RPMEM_ERROR;
                        ret = -1;
			sem_post(&priv_rfile->sem_connect);
                        break;
        }
	pthread_mutex_unlock(&priv_rfile->state_mutex);

        return ret;
}

static void *cm_thread(void *arg)
{
	struct priv_rpmem_file *priv_rfile = arg;
        struct rdma_cm_event *event;
        int ret;

        while (1) {
		ret = rdma_get_cm_event(priv_rfile->cma_channel, &event);
		if (ret) {
			perror("Failed to get RDMA-CM Event");
			pthread_exit(NULL);
		}
		ret = rpmem_cma_handler(event->id, event);
		if (ret) {
			perror("Failed to handle event");
			pthread_exit(NULL);
		}
		rdma_ack_cm_event(event);
	}
}

struct rpmem_file *
rpmem_open(struct sockaddr *dst_addr)
{
	struct priv_rpmem_file *priv_rfile;
	struct rpmem_file *rfile;
	struct rpmem_conn *conn;
	int ret;

	priv_rfile = calloc(1, sizeof(struct priv_rpmem_file));
	if (!priv_rfile) {
		perror("No Memory");
		return NULL;
	}

	conn = &priv_rfile->conn;
	rfile = &priv_rfile->rfile;
	rfile->rmr = &priv_rfile->priv_mr.rmr;

	pthread_mutex_init(&priv_rfile->state_mutex, NULL);
	sem_init(&priv_rfile->sem_connect, 0, 0);
	sem_init(&priv_rfile->sem_command, 0, 0);

	priv_rfile->cma_channel = rdma_create_event_channel();
	if (!priv_rfile->cma_channel) {
		perror("rdma_create_event_channel");
		goto destroy_rfile;
	}

	if (rdma_create_id(priv_rfile->cma_channel, &conn->cma_id, priv_rfile, RDMA_PS_TCP)) {
		perror("rdma_create_id");
		goto destroy_event_channel;
	}

	ret = pthread_create(&priv_rfile->cmthread, NULL, cm_thread, priv_rfile);
	if (ret) {
		perror("Failed create Connection Manager Thread");
		goto destroy_id;
	}

	if (rdma_resolve_addr(conn->cma_id, NULL, dst_addr, 2000)) {
		perror("rdma_resolve_addr");
		goto destroy_thread;
	}

	sem_wait(&priv_rfile->sem_connect);

	pthread_mutex_lock(&priv_rfile->state_mutex);
	if (priv_rfile->state != RPMEM_ESTABLISHED) {
		perror("Conn Error");
		pthread_mutex_unlock(&priv_rfile->state_mutex);
		goto destroy_thread;
	}
	pthread_mutex_unlock(&priv_rfile->state_mutex);

	/* RDMA connection established - need to send OPEN request */
	ret = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_recv");
		goto destroy_thread;
	}

	pack_open_req(&conn->req);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (ret) {
		perror("rpmem_post_send");
		goto destroy_thread;
	}

	printf("before sem_wait_command OPEN\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command OPEN\n");

	ret = unpack_open_rsp(&conn->rsp, &rfile->size);
        if (ret) {
		perror("unpack_open_rsp");
		goto destroy_thread;
	}

	printf("opened resourse sized %d \n", rfile->size);

	pthread_mutex_lock(&file_list_mutex);
	SLIST_INSERT_HEAD(&file_list, conn, entry);
	pthread_mutex_unlock(&file_list_mutex);

	return rfile;

destroy_thread:
	pthread_cancel(priv_rfile->cmthread);
destroy_id:
	rdma_destroy_id(conn->cma_id);
destroy_event_channel:
	rdma_destroy_event_channel(priv_rfile->cma_channel);
destroy_rfile:
	sem_destroy(&priv_rfile->sem_command);
	sem_destroy(&priv_rfile->sem_connect);
	pthread_mutex_destroy(&priv_rfile->state_mutex);
	free(priv_rfile);

	return NULL;
}

int rpmem_close(struct rpmem_file *rfile)
{
	struct priv_rpmem_file *priv_rfile = container_of(rfile, struct priv_rpmem_file, rfile);
	struct rpmem_conn *conn = &priv_rfile->conn;
	int ret, close_ret;

	pthread_mutex_lock(&file_list_mutex);
	SLIST_REMOVE(&file_list,
                     conn,
                     rpmem_conn,
                     entry);
	pthread_mutex_unlock(&file_list_mutex);

	ret = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_recv");
		goto out;
	}

	pack_close_req(&conn->req);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (ret) {
		perror("rpmem_post_send");
		goto out;
	}

	printf("before sem_wait_command CLOSE\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command CLOSE\n");

	ret = unpack_close_rsp(&conn->rsp, &close_ret);
        if (ret) {
		perror("unpack_close_rsp");
		goto out;
	}

	printf("conn %p after receiving close rsp %d\n", conn, close_ret);
out:

	/* Destroy cm stuff */
	pthread_cancel(priv_rfile->cmthread);
	rdma_disconnect(conn->cma_id);
	ibv_destroy_qp(conn->qp);
	rdma_destroy_id(conn->cma_id);
	rdma_destroy_event_channel(priv_rfile->cma_channel);

	/* Destroy cq stuff */
	pthread_cancel(conn->cqthread);
	ibv_destroy_cq(conn->cq);
	ibv_destroy_comp_channel(conn->comp_channel);
	ibv_dealloc_pd(conn->pd);

	/* Destroy priv_rfile stuff */
	sem_destroy(&priv_rfile->sem_command);
	sem_destroy(&priv_rfile->sem_connect);
	pthread_mutex_destroy(&priv_rfile->state_mutex);
	free(priv_rfile);

	return 0;
}

struct rpmem_mr *
rpmem_map(struct rpmem_file *rfile, size_t len)
{
	struct priv_rpmem_file *priv_rfile = container_of(rfile, struct priv_rpmem_file, rfile);
	struct rpmem_conn *conn = &priv_rfile->conn;
	struct rpmem_mr *rmr = rfile->rmr;
	int ret;

	rmr->len = len;
	rmr->addr = calloc(1, len);
	if (!rmr->addr) {
		perror("calloc");
		goto out;
	}

	priv_rfile->priv_mr.mr = ibv_reg_mr(conn->pd,
					    rmr->addr,
					    len,
					    IBV_ACCESS_LOCAL_WRITE);
	if (!priv_rfile->priv_mr.mr) {
		perror("buff ibv_reg_mr");
		goto destroy_addr;
	}

	ret = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_recv");
		goto destroy_mr;
	}

	pack_map_req(&conn->req, len);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (ret) {
		perror("rpmem_post_send");
		goto destroy_mr;
	}

	printf("before sem_wait_command MAP\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command MAP\n");

	ret = unpack_map_rsp(&conn->rsp, &priv_rfile->priv_mr.rkey, &priv_rfile->priv_mr.remote_addr);
        if (ret) {
		perror("unpack_map_rsp");
		goto destroy_mr;
	}

	printf("conn %p after receiving map rsp. rkey %d addr %lld\n", conn, (int)priv_rfile->priv_mr.rkey,
		(long long int)priv_rfile->priv_mr.remote_addr);

	ret = rpmem_post_rdma_wr(conn,
				 priv_rfile->priv_mr.rkey,
				 priv_rfile->priv_mr.mr->lkey,
				 priv_rfile->priv_mr.remote_addr,
				 rmr->addr,
				 rmr->len,
				 IBV_WR_RDMA_READ);
	if (ret) {
		perror("rpmem_post_rdma_read");
		goto destroy_rkey;
	}

	printf("before sem_wait_command RDMA_READ\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command RDMA_READ\n");

	return rmr;

destroy_rkey:
	priv_rfile->priv_mr.rkey = 0;
	priv_rfile->priv_mr.remote_addr = 0;
destroy_mr:
	ibv_dereg_mr(priv_rfile->priv_mr.mr);
destroy_addr:
	free(rmr->addr);
out:
	rmr->len = 0;

	return NULL;
}

int
rpmem_unmap(struct rpmem_mr *rmr)
{
	struct priv_rpmem_mr *priv_mr = container_of(rmr, struct priv_rpmem_mr, rmr);
	struct priv_rpmem_file *priv_rfile = container_of(priv_mr, struct priv_rpmem_file, priv_mr);
	struct rpmem_conn *conn = &priv_rfile->conn;
	int unmap_ret;
	int ret = 0;

	ret = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_recv");
		goto destroy_mr;
	}

	pack_unmap_req(&conn->req, rmr->len, priv_mr->remote_addr);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (ret) {
		perror("rpmem_post_send");
		goto destroy_mr;
	}

	printf("before sem_wait_command UNMAP\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command UNMAP\n");

	ret = unpack_unmap_rsp(&conn->rsp, &unmap_ret);
        if (ret) {
		perror("unpack_unmap_rsp");
		goto destroy_mr;
	}

	printf("got unmap_ret %d\n", unmap_ret);
	ret = unmap_ret;
destroy_mr:
	ibv_dereg_mr(priv_mr->mr);
	priv_mr->rkey = 0;
	priv_mr->remote_addr = 0;
	free(rmr->addr);
	rmr->len = 0;

	return ret;

}

int
rpmem_commit(struct rpmem_mr *rmr)
{
	struct priv_rpmem_mr *priv_mr = container_of(rmr, struct priv_rpmem_mr, rmr);
	struct priv_rpmem_file *priv_rfile = container_of(priv_mr, struct priv_rpmem_file, priv_mr);
	struct rpmem_conn *conn = &priv_rfile->conn;
	int commit_ret;
	int ret = 0;

	ret = rpmem_post_recv(conn, (struct rpmem_cmd *)&conn->rsp, conn->rsp_mr);
	if (ret) {
		perror("rpmem_post_recv");
		goto out;
	}
	
	printf("conn %p before rdma write. rkey %d addr %lld\n", conn, (int)priv_rfile->priv_mr.rkey,
		(long long int)priv_rfile->priv_mr.remote_addr);

	ret = rpmem_post_rdma_wr(conn,
				 priv_rfile->priv_mr.rkey,
				 priv_rfile->priv_mr.mr->lkey,
				 priv_rfile->priv_mr.remote_addr,
				 rmr->addr,
				 rmr->len,
				 IBV_WR_RDMA_WRITE);
	if (ret) {
		perror("rpmem_post_rdma_write");
		goto out;
	}

	pack_commit_req(&conn->req, rmr->len, priv_mr->remote_addr);

	ret = rpmem_post_send(conn, (struct rpmem_cmd *)&conn->req, conn->req_mr);
	if (ret) {
		perror("rpmem_post_send");
		goto out;
	}

	printf("before sem_wait_command COMMIT\n");
	sem_wait(&priv_rfile->sem_command);
	printf("after sem_wait_command COMMIT\n");

	ret = unpack_commit_rsp(&conn->rsp, &commit_ret);
        if (ret) {
		perror("unpack_unmap_rsp");
		goto out;
	}

	printf("got commit_ret %d\n", commit_ret);
	ret = commit_ret;
out:
	return ret;

}
