/*
 * LiME - Linux Memory Extractor
 * Copyright (c) 2011-2014 Joe Sylve - 504ENSICS Labs
 *
 *
 * Author:
 * Joe Sylve       - joe.sylve@gmail.com, @jtsylve
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

int write_vaddr_tcp(void *, size_t);
int setup_tcp(void);
void cleanup_tcp(void);

extern int port;

static struct socket *control;
static struct socket *accept;

int setup_tcp() {
	struct sockaddr_in saddr;
	int r;
	
	mm_segment_t fs;

	int buffsize = PAGE_SIZE;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,5)
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
	saddr.sin_addr.s_addr = INADDR_ANY;

	fs = get_fs();
	set_fs(KERNEL_DS);

	sock_setsockopt(control, SOL_SOCKET, SO_SNDBUF, (void *) &buffsize, sizeof (int));

	set_fs(fs);

	if (r < 0) {
		DBG("Error setting buffsize %d", r);
		return r;
	}

	r = control->ops->bind(control,(struct sockaddr*) &saddr,sizeof(saddr));
	if (r < 0) {
		DBG("Error binding control socket");
		return r;
	}

	r = control->ops->listen(control,1);
	if (r) {
		DBG("Error listening on socket");
		return r;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,5)
	r = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &accept);
#else
	r = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &accept);
#endif
    
	if (r < 0) {
		DBG("Error creating accept socket");
		return r;
	}

	r = accept->ops->accept(control,accept,0);
	if (r < 0) {
		DBG("Error accepting socket");
		return r;
	}

	return 0;
}

void cleanup_tcp() {
	if (accept && accept->ops) {
		accept->ops->shutdown(accept, 0);
		accept->ops->release(accept);
	}
    
	if (control && control->ops) {
		control->ops->shutdown(control, 0);
		control->ops->release(control);
	}
}

int write_vaddr_tcp(void * v, size_t is) {
	mm_segment_t fs;

	long s;

	struct iovec iov;
	struct msghdr msg  = { .msg_iov = &iov, .msg_iovlen = 1 };

	fs = get_fs();
	set_fs(KERNEL_DS);

	iov.iov_base = v;	
	iov.iov_len = is;
    
	s = sock_sendmsg(accept, &msg, is);

	set_fs(fs);

	return s;
}
