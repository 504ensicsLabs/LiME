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

#include "lime.h"

ssize_t write_vaddr_disk(void *, size_t);

static struct file * f = NULL;

static int dio_write_test(char *path, int oflags)
{
    int ok;

    f = filp_open(path, oflags | O_DIRECT | O_SYNC, 0444);
    if (f && !IS_ERR(f)) {
        ok = write_vaddr_disk("DIO", 3) == 3;
        filp_close(f, NULL);
    } else {
        ok = 0;
    }

    return ok;
}

int setup_disk(char *path, int dio) {
    int oflags = O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC;
    int err = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);
#endif

    if (dio && dio_write_test(path, oflags)) {
        oflags |= O_DIRECT | O_SYNC;
    } else {
        DBG("Direct IO Disabled");
    }

    f = filp_open(path, oflags, 0444);

    if (!f || IS_ERR(f)) {
        DBG("Error opening file %ld", PTR_ERR(f));

        err = (f) ? PTR_ERR(f) : -EIO;
        f = NULL;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    set_fs(fs);
#endif

    return err;
}

void cleanup_disk(void) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);
#endif

    if(f) filp_close(f, NULL);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    set_fs(fs);
#endif
}

ssize_t write_vaddr_disk(void * v, size_t is) {
    ssize_t s;
    loff_t pos;

    pos = f->f_pos;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);
    s = vfs_write(f, v, is, &pos);
    set_fs(fs);
#else
    s = kernel_write(f, v, is, &pos);
#endif

    if (s == is) {
        f->f_pos = pos;
    }

    return s;
}
