/*
 * LiME - Linux Memory Extractor
 * Author:
 * Joe Sylve       - joe.sylve@gmail.com, @jtsylve
 *
 * MIT License
 * 
 * Copyright (c) 2011-2021 Joe Sylve - 504ENSICS Labs
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>

#include <net/sock.h>
#include <net/tcp.h>

#include "lime.h"

ssize_t write_vaddr_tcp(void *, size_t);
int setup_tcp(void);
void cleanup_tcp(void);

extern int port;
extern int localhostonly;

static struct socket *control;
static struct socket *accept;

int setup_tcp() {
    struct sockaddr_in saddr;
    int r;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
    r = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &control);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2,6,5)
    r = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &control);
#else
    r = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &control);
#endif

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
    int opt = 1;
    r = kernel_setsockopt(control, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof (opt));
    if (r < 0) {
        DBG("Error setting socket options");

        return r;
    }
#else
    sock_set_reuseaddr(control->sk);
#endif

    r = kernel_bind(control,(struct sockaddr*) &saddr,sizeof(saddr));
    if (r < 0) {
        DBG("Error binding control socket");
        return r;
    }

    r = kernel_listen(control,1);
    if (r) {
        DBG("Error listening on socket");
        return r;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
    r = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &accept);
#elif LINUX_VERSION_CODE > KERNEL_VERSION(2,6,5)
    r = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &accept);
#else
    r = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &accept);
#endif

    if (r < 0) {
        DBG("Error creating accept socket");
        return r;
    }

    r = kernel_accept(control, &accept, 0);

    if (r < 0) {
        DBG("Error accepting socket");
        return r;
    }

    return 0;
}

void cleanup_tcp() {
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

    memset(&iov, 0, sizeof(struct iovec));
    memset(&msg, 0, sizeof(struct msghdr));

    iov.iov_base = v;
    iov.iov_len = is;

    s = kernel_sendmsg(accept, &msg, &iov, 1, is);

    return s;
}
