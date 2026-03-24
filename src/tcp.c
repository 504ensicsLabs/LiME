/*
 * LiME - Linux Memory Extractor
 * Copyright (c) 2011-2026 Joe T. Sylve, Ph.D. <joe.sylve@gmail.com>
 *
 * Author:
 * Joe T. Sylve, Ph.D.       - joe.sylve@gmail.com, @jtsylve
 *
 * SPDX-FileCopyrightText: 2011-2026 Joe T. Sylve, Ph.D. <joe.sylve@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "lime.h"

static struct socket *control;
static struct socket *accept;

static int create_tcp_sock(struct socket **sock, int family) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
    return sock_create_kern(&init_net, family, SOCK_STREAM, IPPROTO_TCP, sock);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,6)
    return sock_create_kern(family, SOCK_STREAM, IPPROTO_TCP, sock);
#else
    return sock_create(family, SOCK_STREAM, IPPROTO_TCP, sock);
#endif
}

int setup_tcp(void) {
    struct sockaddr_in saddr;
    int r;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0)
    int opt = 1;
#endif

    r = create_tcp_sock(&control, AF_INET);
    if (r < 0) {
        DBG("Error creating control socket");
        return r;
    }

    memset(&saddr, 0, sizeof(saddr));

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    if (localhostonly) {
        saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0)
    r = kernel_setsockopt(control, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof (opt));
    if (r < 0) {
        DBG("Error setting socket options");

        return r;
    }
#else
    sock_set_reuseaddr(control->sk);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,19,0)
    r = kernel_bind(control,(struct sockaddr*) &saddr,sizeof(saddr));
#else
    r = kernel_bind(control,(struct sockaddr_unsized *) &saddr,sizeof(saddr));
#endif
    if (r < 0) {
        DBG("Error binding control socket");
        return r;
    }

    r = kernel_listen(control,1);
    if (r) {
        DBG("Error listening on socket");
        return r;
    }

    r = kernel_accept(control, &accept, 0);

    if (r < 0) {
        DBG("Error accepting socket");
        return r;
    }

    return 0;
}

void cleanup_tcp(void) {
    if (accept) {
        kernel_sock_shutdown(accept, SHUT_RDWR);
        sock_release(accept);
        accept = NULL;
    }

    if (control) {
        kernel_sock_shutdown(control, SHUT_RDWR);
        sock_release(control);
        control = NULL;
    }
}

ssize_t write_vaddr_tcp(void * v, size_t is) {
    ssize_t s;
    struct kvec iov;
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));

    iov.iov_base = v;
    iov.iov_len = is;

    s = kernel_sendmsg(accept, &msg, &iov, 1, is);

    return s;
}
