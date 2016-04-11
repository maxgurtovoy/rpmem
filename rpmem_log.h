/*
 * Copyright (c) 2016 Mellanox Technologies®. All rights reserved.
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the Mellanox Technologies® BSD license
 * below:
 *
 *      - Redistribution and use in source and binary forms, with or without
 *        modification, are permitted provided that the following conditions
 *        are met:
 *
 *      - Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither the name of the Mellanox Technologies® nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef RPMEM_LOG_H
#define RPMEM_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __GNUC__
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

/*---------------------------------------------------------------------------*/
/* enum									     */
/*---------------------------------------------------------------------------*/

/**
 * @enum rpmem_log_level
 * @brief logging levels
 */
enum rpmem_log_level {
        RPMEM_LOG_LEVEL_ERROR,               /**< error logging level         */
        RPMEM_LOG_LEVEL_WARN,                /**< warnings logging level      */
        RPMEM_LOG_LEVEL_INFO,                /**< informational logging level */
        RPMEM_LOG_LEVEL_DEBUG,               /**< debugging logging level     */
        RPMEM_LOG_LEVEL_LAST
};

void rpmem_vlog(const char *fmt, ...);

extern enum rpmem_log_level rpmem_logging_level;

#define rpmem_log(level, fmt, ...) \
	do { \
		if (unlikely(((level) < RPMEM_LOG_LEVEL_LAST) &&  \
			      (level) <= rpmem_logging_level)) { \
			rpmem_vlog(fmt, ## __VA_ARGS__); \
		} \
	} while (0)

#define ERROR_LOG(fmt, ...)	rpmem_log(RPMEM_LOG_LEVEL_ERROR, fmt, \
							## __VA_ARGS__)
#define WARN_LOG(fmt, ...)	rpmem_log(RPMEM_LOG_LEVEL_WARN, fmt,\
							## __VA_ARGS__)
#define INFO_LOG(fmt, ...)	rpmem_log(RPMEM_LOG_LEVEL_INFO, fmt,\
							## __VA_ARGS__)
#define DEBUG_LOG(fmt, ...)	rpmem_log(RPMEM_LOG_LEVEL_DEBUG, fmt,\
							##  __VA_ARGS__)

#endif /* RPMEM_LOG_H */
