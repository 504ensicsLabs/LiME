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

#ifndef __LIME_H_
#define __LIME_H_

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/scatterlist.h>

#include <net/sock.h>
#include <net/tcp.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
#include <crypto/hash.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
#include <linux/crypto.h>
#endif

#define LIME_RAMSTR "System RAM"
#define LIME_MAX_FILENAME_SIZE 256
#define LIME_MAGIC 0x4C694D45 //LiME

#define LIME_MODE_RAW 0
#define LIME_MODE_LIME 1
#define LIME_MODE_PADDED 2

#define LIME_METHOD_UNKNOWN 0
#define LIME_METHOD_TCP 1
#define LIME_METHOD_DISK 2

#define LIME_DIGEST_FAILED -1
#define LIME_DIGEST_COMPLETE 0
#define LIME_DIGEST_COMPUTE 1

#ifdef LIME_DEBUG
#define DBG(fmt, args...) do { printk("[LiME] "fmt"\n", ## args); } while (0)
#else
#define DBG(fmt, args...) do {} while(0)
#endif

#define RETRY_IF_INTERRUPTED(f) ({ \
    ssize_t err; \
    do { err = f; } while(err == -EAGAIN || err == -EINTR); \
    err; \
})

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
#define LIME_SUPPORTS_TIMING
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
#define LIME_USE_KMAP_ATOMIC
#endif

#ifdef CONFIG_ZLIB_DEFLATE
#define LIME_SUPPORTS_DEFLATE
#endif

#ifdef LIME_RISCV
#define RISCV_LOAD_ACCESS_FAULT 5

#define riscv_test_load_access(v_addr)                                         \
  ({                                                                           \
    int __v = 0xdeadbeef;                                                      \
    __asm__ __volatile__("1:\n"                                                \
                         "ld t0, 0(%1)\n"                                      \
                         "2:\n"                                                \
                         ".section .fixup,\"ax\"\n"                            \
                         "3:\n"                                                \
                         "csrr %0, scause\n"                                   \
                         "j 2b\n"                                              \
                         ".previous\n" _ASM_EXTABLE(1b, 3b)                    \
                         : "=r"(__v)                                           \
                         : "r"(v_addr)                                         \
                         : "memory");                                          \
    __v;                                                                       \
  })

typedef struct {
  unsigned long long start;
  unsigned long long end;
} riscv_pmp_region;
#endif

//structures

typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned long long s_addr;
    unsigned long long e_addr;
    unsigned char reserved[8];
} __attribute__ ((__packed__)) lime_mem_range_header;



#endif //__LIME_H_
