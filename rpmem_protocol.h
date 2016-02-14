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
	uint32_t ret; //remote resource size
	uint8_t  reserved[40];
};

static inline char *pack_mem(const void *data, const size_t size, char *buffer)
{
	memcpy(buffer, data, size);
	return buffer + size;
}

static inline const char *unpack_mem(void *data, const size_t size,
				     const char *buffer)
{
	memcpy(data, buffer, size);
	return buffer + size;
}

static inline char *pack_u32(const uint32_t *data, char *buffer)
{
	*((uint32_t *)buffer) = htonl(*data);
	return buffer + sizeof(*data);
}

static inline const char *unpack_u32(uint32_t *data, const char *buffer)
{
	*data = ntohl(*((uint32_t *)buffer));
	return buffer + sizeof(*data);
}

void pack_open_req(struct rpmem_req *req);
void pack_open_rsp(int size, struct rpmem_rsp *rsp);
int unpack_open_rsp(struct rpmem_rsp *rsp, int *size);
void pack_close_req(struct rpmem_req *req);
void pack_close_rsp(int ret, struct rpmem_rsp *rsp);
int unpack_close_rsp(struct rpmem_rsp *rsp, int *ret);

#endif /* RPMEM_PROTOCOL_H */
