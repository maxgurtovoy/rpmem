/*
 * Copyright (c) 2015 Mellanox Technologies.  All rights reserved.
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

/*
 * simple_open_close.c -- show how to use rpmem_open() and rpmem_close()
 *
 * usage: simple_copy dst-file server_ip server_port
 *
 * Opens remote dst-file and closes it.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "rpmem.h"


int
main(int argc, char *argv[])
{
	struct rpmem_file *rfile;
	struct rpmem_mr *rmr;
	struct sockaddr_storage ssin;
	uint16_t port;
	int size;
	int ret = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s dst-file server-ip server-port\n", argv[0]);
		exit(1);
	}

	port = htons((uint16_t)atoi(argv[2]));

	printf("port %u net_port %u\n", (uint16_t)atoi(argv[2]), port);

	if (get_addr(argv[1], (struct sockaddr *)&ssin, port)) {
		fprintf(stderr, "failed to get addr %s\n", argv[1]);
		exit(1);

	}

	/* open channel to remote server */
	rfile = rpmem_open((struct sockaddr *)&ssin);
	if (!rfile) {
		perror("rpmem_open");
		exit(1);
	}

	/* minimum map 1024B*/
	size = ((rfile->size/1024) > 1 ? 1024 * ((rfile->size/1024) - 1) : 1024);

	printf("maximum map size is %d, we map %d modulu %d\n", rfile->size, size, rfile->size/1024);
	rmr = rpmem_map(rfile, size);
	if (!rmr) {
		perror("rpmem_map");
		ret = 1;
		goto out;
	}

	ret = rpmem_commit(rmr);
	if (ret) {
		perror("rpmem_commit");
		ret = 1;
		goto out;
	}

	ret = rpmem_unmap(rmr);
	if (ret) {
		perror("rpmem_unmap");
		ret = 1;
		goto out;
	}

out:
	/* close dst-file in remote server */
	if (rpmem_close(rfile)) {
		perror("rpmem_close");
		exit(1);
	}

	exit(ret);
}
