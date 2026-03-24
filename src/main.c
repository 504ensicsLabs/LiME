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

static ssize_t write_lime_header(struct resource *);
static ssize_t write_padding(size_t);
static void write_range(struct resource *);
static int init(void);
static ssize_t write_vaddr(void *, size_t);
static ssize_t write_flush(void);
static ssize_t try_write(void *, ssize_t);
static int setup(void);
static void cleanup(void);

/*
 * Helpers for walking the iomem_resource tree depth-first.
 *
 * Since kernel 5.8, drivers like virtio-mem and dax/kmem create
 * "System RAM" entries nested inside non-busy parent resources.
 * A sibling-only walk misses all such memory, so we must descend
 * into children when searching for RAM ranges.
 *
 * However, once we find a matching range, we must NOT descend into
 * its children — sub-resources like "Kernel code" and "Kernel data"
 * also carry IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY flags but are
 * sub-ranges already covered by the parent dump.
 */

/* Advance past p and all its descendants to the next unrelated node. */
static struct resource *lime_skip_subtree(struct resource *p)
{
    while (!p->sibling && p->parent)
        p = p->parent;
    return p->sibling;
}

/* Depth-first: try child first, then sibling/ancestor's sibling. */
static struct resource *lime_next_resource(struct resource *p)
{
    if (p->child)
        return p->child;
    return lime_skip_subtree(p);
}

/*
 * Check whether a resource represents busy System RAM.
 * Since 4.6 we use the IORESOURCE_SYSTEM_RAM flag which matches all
 * variants ("System RAM", "System RAM (virtio_mem)", "System RAM (kmem)").
 * On older kernels fall back to exact string comparison, which is fine
 * because nested System RAM entries did not exist before 5.8.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
static inline int lime_is_ram(struct resource *r)
{
    return (r->flags & (IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY)) ==
           (IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY);
}
#else
static inline int lime_is_ram(struct resource *r)
{
    return r->name && strcmp(r->name, LIME_RAMSTR) == 0;
}
#endif

static char * format = NULL;
static int mode = 0;
static int method = 0;

static void * vpage;

#ifdef LIME_SUPPORTS_DEFLATE
static void *deflate_page_buf;
#endif

char * path = NULL;
static int dio = 0;
int port = 0;
int localhostonly = 0;

char * digest = NULL;
static int compute_digest = 0;

module_param(path, charp, S_IRUGO);
module_param(dio, int, S_IRUGO);
module_param(format, charp, S_IRUGO);
module_param(localhostonly, int, S_IRUGO);
module_param(digest, charp, S_IRUGO);

#ifdef LIME_SUPPORTS_TIMING
static long timeout = 1000;
module_param(timeout, long, S_IRUGO);
#endif

#ifdef LIME_SUPPORTS_DEFLATE
static int compress = 0;
module_param(compress, int, S_IRUGO);
#endif

static int __init lime_init_module (void)
{
    if(!path) {
        DBG("No path parameter specified");
        return -EINVAL;
    }

    if(!format) {
        DBG("No format parameter specified");
        return -EINVAL;
    }

    DBG("Parameters");
    DBG("  PATH: %s", path);
    DBG("  DIO: %u", dio);
    DBG("  FORMAT: %s", format);
    DBG("  LOCALHOSTONLY: %u", localhostonly);
    DBG("  DIGEST: %s", digest);

#ifdef LIME_SUPPORTS_TIMING
    DBG("  TIMEOUT: %lu", timeout);
#endif

#ifdef LIME_SUPPORTS_DEFLATE
    DBG("  COMPRESS: %u", compress);
#endif

    if (!strcmp(format, "raw")) mode = LIME_MODE_RAW;
    else if (!strcmp(format, "lime")) mode = LIME_MODE_LIME;
    else if (!strcmp(format, "padded")) mode = LIME_MODE_PADDED;
    else {
        DBG("Invalid format parameter specified.");
        return -EINVAL;
    }

    method = (sscanf(path, "tcp:%d", &port) == 1) ? LIME_METHOD_TCP : LIME_METHOD_DISK;

    return init();
}

static int init(void) {
    struct resource *p;
    int err = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
    resource_size_t p_last = -1;
#else
    __PTRDIFF_TYPE__ p_last = -1;
#endif

    DBG("Initializing Dump...");

    if ((err = setup())) {
        DBG("Setup Error");
        cleanup();
        return err;
    }

    if (digest)
        compute_digest = ldigest_init();

    vpage = (void *) __get_free_page(GFP_NOIO);
    if (!vpage) {
        DBG("Failed to allocate page");
        err = -ENOMEM;
        goto err_digest;
    }

#ifdef LIME_SUPPORTS_DEFLATE
    if (compress) {
        deflate_page_buf = kmalloc(PAGE_SIZE, GFP_NOIO);
        if (!deflate_page_buf) {
            DBG("Failed to allocate deflate buffer");
            err = -ENOMEM;
            goto err_vpage;
        }
        err = deflate_begin_stream(deflate_page_buf, PAGE_SIZE);
        if (err < 0) {
            DBG("ZLIB begin stream failed");
            goto err_deflate_buf;
        }
    }
#endif

    for (p = iomem_resource.child; p; ) {

        if (!lime_is_ram(p)) {
            /* Not RAM — descend into children to find nested RAM. */
            p = lime_next_resource(p);
            continue;
        }

        if (mode == LIME_MODE_LIME && write_lime_header(p) < 0) {
            DBG("Error writing header 0x%llx - 0x%llx", (unsigned long long) p->start, (unsigned long long) p->end);
            break;
        } else if (mode == LIME_MODE_PADDED && write_padding((size_t) ((p->start - 1) - p_last)) < 0) {
            DBG("Error writing padding 0x%llx - 0x%llx", (unsigned long long) p_last, (unsigned long long) (p->start - 1));
            break;
        }

        write_range(p);

        p_last = p->end;

        /* Children are sub-ranges already covered — skip them. */
        p = lime_skip_subtree(p);
    }

    write_flush();

    DBG("Memory Dump Complete...");

    cleanup();

    if (compute_digest == LIME_DIGEST_COMPUTE) {
        DBG("Writing Out Digest.");

        compute_digest = ldigest_final();

        if (compute_digest == LIME_DIGEST_COMPLETE) {
            if (method == LIME_METHOD_TCP)
                err = ldigest_write_tcp();
            else
                err = ldigest_write_disk();

            DBG("Digest Write %s.", (err == 0) ? "Complete" : "Failed");
        }
    }

    if (digest)
        ldigest_clean();

#ifdef LIME_SUPPORTS_DEFLATE
    if (compress) {
        deflate_end_stream();
        kfree(deflate_page_buf);
    }
#endif

    free_page((unsigned long) vpage);

    return 0;

#ifdef LIME_SUPPORTS_DEFLATE
err_deflate_buf:
    kfree(deflate_page_buf);
err_vpage:
#endif
    free_page((unsigned long) vpage);
err_digest:
    if (digest)
        ldigest_clean();
    cleanup();
    return err;
}

static ssize_t write_lime_header(struct resource * res) {
    lime_mem_range_header header;

    memset(&header, 0, sizeof(lime_mem_range_header));
    header.magic = LIME_MAGIC;
    header.version = 1;
    header.s_addr = res->start;
    header.e_addr = res->end;

    return write_vaddr(&header, sizeof(lime_mem_range_header));
}

static ssize_t write_padding(size_t s) {
    size_t i = 0;
    ssize_t r;

    memset(vpage, 0, PAGE_SIZE);

    while(s -= i) {

        i = min((size_t) PAGE_SIZE, s);
        r = write_vaddr(vpage, i);

        if (r != i) {
            DBG("Error sending zero page: %zd", r);
            return r;
        }
    }

    return 0;
}

static void write_range(struct resource * res) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
    resource_size_t i, is;
#else
    __PTRDIFF_TYPE__ i, is;
#endif
    struct page * p;
    void * v;

    ssize_t s;

#ifdef LIME_SUPPORTS_TIMING
    ktime_t start,end;
#endif

    DBG("Writing range %llx - %llx.", (unsigned long long) res->start, (unsigned long long) res->end);

    for (i = res->start; i <= res->end; i += is) {
#ifdef LIME_SUPPORTS_TIMING
        start = ktime_get_real();
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
        is = min((resource_size_t) PAGE_SIZE, (resource_size_t) (res->end - i + 1));
#else
        is = min((size_t) PAGE_SIZE, (size_t) (res->end - i + 1));
#endif

        if (is < PAGE_SIZE) {
            // We can't map partial pages and
            // the linux kernel doesn't use them anyway
            DBG("Padding partial page: addr 0x%llx size: %lu", (unsigned long long) i, (unsigned long) is);
            write_padding(is);
        } else if (unlikely(!pfn_valid(i >> PAGE_SHIFT))) {
            // Guard against invalid PFNs which can occur on SPARSEMEM
            // configs, during memory hotremove, or on unusual NUMA layouts
            DBG("Invalid PFN 0x%llx, writing padding", (unsigned long long)(i >> PAGE_SHIFT));
            write_padding(is);
        } else {
            p = pfn_to_page(i >> PAGE_SHIFT);
            v = lime_map_page(p);
#ifdef copy_mc_to_kernel
            {
                unsigned long mc_err;
                mc_err = copy_mc_to_kernel(vpage, v, PAGE_SIZE);
                if (mc_err) {
                    DBG("Hardware memory error at PFN 0x%llx (%lu bytes unreadable)",
                        (unsigned long long)(i >> PAGE_SHIFT), mc_err);
                    memset((char *)vpage + PAGE_SIZE - mc_err, 0, mc_err);
                }
            }
#else
            copy_page(vpage, v);
#endif
            lime_unmap_page(v, p);

            s = write_vaddr(vpage, is);
            if (s < 0) {
                DBG("Failed to write page: addr 0x%llx. Skipping Range...", (unsigned long long) i);
                break;
            }
        }

#ifdef LIME_SUPPORTS_TIMING
        end = ktime_get_real();

        if (timeout > 0 && ktime_to_ms(ktime_sub(end, start)) > timeout) {
            DBG("Reading is too slow.  Skipping Range...");
            write_padding(res->end - i + 1 - is);
            break;
        }
#endif

    }
}

static ssize_t write_vaddr(void * v, size_t is) {
    ssize_t ret;

    if (compute_digest == LIME_DIGEST_COMPUTE)
        compute_digest = ldigest_update(v, is);

#ifdef LIME_SUPPORTS_DEFLATE
    if (compress) {
        /* Run deflate() on input until output buffer is not full. */
        do {
            ret = try_write(deflate_page_buf, deflate(v, is));
            if (ret < 0)
                return ret;
        } while (ret == PAGE_SIZE);
        return is;
    }
#endif

    return try_write(v, is);
}

static ssize_t write_flush(void) {
#ifdef LIME_SUPPORTS_DEFLATE
    if (compress) {
        try_write(deflate_page_buf, deflate(NULL, 0));
    }
#endif
    return 0;
}

static ssize_t try_write(void * v, ssize_t is) {
    ssize_t ret;

    if (is <= 0)
        return is;

    ret = RETRY_IF_INTERRUPTED(
        (method == LIME_METHOD_TCP) ? write_vaddr_tcp(v, is) : write_vaddr_disk(v, is)
    );

    if (ret < 0) {
        DBG("Write error: %zd", ret);
    } else if (ret != is) {
        DBG("Short write %zd instead of %zd.", ret, is);
        ret = -1;
    }

    return ret;
}

static int setup(void) {
    return (method == LIME_METHOD_TCP) ? setup_tcp() : setup_disk(path, dio);
}

static void cleanup(void) {
    if (method == LIME_METHOD_TCP)
        cleanup_tcp();
    else
        cleanup_disk();
}

static void __exit lime_cleanup_module(void) {

}

module_init(lime_init_module);
module_exit(lime_cleanup_module);

MODULE_LICENSE("GPL");
