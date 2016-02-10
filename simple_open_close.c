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
	struct sockaddr_storage ssin;
	uint16_t port;

	if (argc != 4) {
		fprintf(stderr, "usage: %s dst-file server-ip server-port\n", argv[0]);
		exit(1);
	}

	port = htons((uint16_t)atoi(argv[3]));

	fprintf(stderr, "port %u net_port %u\n", (uint16_t)atoi(argv[3]), port);

	if (get_addr(argv[2], (struct sockaddr *)&ssin, port)) {
		fprintf(stderr, "failed to get addr %s\n", argv[2]);
		exit(1);

	}

	/* open dst-file in remote server */
	rfile = rpmem_open((struct sockaddr *)&ssin, argv[1], O_RDWR);
	if (!rfile) {
		perror("rpmem_open");
		exit(1);
	}

	/* close dst-file in remote server */
	if (rpmem_close(rfile)) {
		perror("rpmem_close");
		exit(1);
	}

	exit(0);
}
