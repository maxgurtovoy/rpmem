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

#ifndef RPMEM_PROTOCOL_H
#define RPMEM_PROTOCOL_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <errno.h>

#define RPMEM_CMD_SIZE 48

/* protocol commands */
enum rpmem_opcode {
        RPMEM_OPEN_REQ,
        RPMEM_OPEN_RSP,
        RPMEM_CLOSE_REQ,
        RPMEM_CLOSE_RSP,
        RPMEM_MAP_REQ,
        RPMEM_MAP_RSP,
        RPMEM_UNMAP_REQ,
        RPMEM_UNMAP_RSP,
        RPMEM_COMMIT_REQ,
        RPMEM_COMMIT_RSP,

        RPMEM_CMD_LAST
};

/* general command structure */
struct rpmem_cmd {
	uint8_t  cmd_specific_data[RPMEM_CMD_SIZE];
};

/* general request structure */
struct rpmem_req {
	uint32_t opcode;
	uint8_t  cmd_specific_data[44];
};

struct rpmem_open_req {
	uint32_t opcode;
	uint8_t  reserved[44];
};

struct rpmem_close_req {
	uint32_t opcode;
	uint8_t  reserved[44];
};

struct rpmem_map_req {
	uint32_t opcode;
	uint32_t len;
	uint8_t  reserved[40];
};

struct rpmem_unmap_req {
	uint32_t opcode;
	uint32_t len;
	uint64_t remote_addr;
	uint8_t  reserved[32];
};

struct rpmem_commit_req {
	uint32_t opcode;
	uint32_t len;
	uint64_t remote_addr;
	uint8_t  reserved[32];
};


/* general response structure */
struct rpmem_rsp {
	uint32_t opcode;
	uint8_t  cmd_specific_data[44];
};

struct rpmem_open_rsp {
	uint32_t opcode;
	uint32_t size; //remote resource size
	uint8_t  reserved[40];
};

struct rpmem_close_rsp {
	uint32_t opcode;
	uint32_t ret;
	uint8_t  reserved[40];
};

struct rpmem_map_rsp {
	uint32_t opcode;
	uint32_t rkey;
	uint64_t remote_addr;
	uint8_t  reserved[32];
};

struct rpmem_unmap_rsp {
	uint32_t opcode;
	uint32_t ret;
	uint8_t  reserved[40];
};

struct rpmem_commit_rsp {
	uint32_t opcode;
	uint32_t ret;
	uint8_t  reserved[40];
};


void pack_open_req(struct rpmem_req *req);
void pack_open_rsp(int size, struct rpmem_rsp *rsp);
int unpack_open_rsp(struct rpmem_rsp *rsp, int *size);
void pack_close_req(struct rpmem_req *req);
void pack_close_rsp(int ret, struct rpmem_rsp *rsp);
int unpack_close_rsp(struct rpmem_rsp *rsp, int *ret);
void pack_map_req(struct rpmem_req *req, int len);
void pack_map_rsp(struct rpmem_rsp *rsp, uint32_t rkey, uint64_t remote_addr);
int unpack_map_rsp(struct rpmem_rsp *rsp, uint32_t *rkey, uint64_t *remote_addr);
void pack_unmap_req(struct rpmem_req *req, int len, uint64_t remote_addr);
void pack_unmap_rsp(struct rpmem_rsp *rsp, int ret);
int unpack_unmap_rsp(struct rpmem_rsp *rsp, int *ret);
void pack_commit_req(struct rpmem_req *req, int len, uint64_t remote_addr);
void pack_commit_rsp(struct rpmem_rsp *rsp, int ret);
int unpack_commit_rsp(struct rpmem_rsp *rsp, int *ret);

#endif /* RPMEM_PROTOCOL_H */
