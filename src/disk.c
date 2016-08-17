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

#include "lime.h"


ssize_t write_vaddr_disk(void *, size_t);
int setup_disk(void);
void cleanup_disk(void);

static void disable_dio(void);

static struct file * f = NULL;
extern char * path;
extern int dio;
static int reopen = 0;

static void disable_dio() {
    DBG("Direct IO may not be supported on this file system. Retrying.");
    dio = 0;
    reopen = 1;
    cleanup_disk();
    setup_disk();
}

int setup_disk() {
    mm_segment_t fs;
    int err;

    fs = get_fs();
    set_fs(KERNEL_DS);

    if (dio && reopen) {
        f = filp_open(path, O_WRONLY | O_CREAT | O_LARGEFILE | O_SYNC | O_DIRECT, 0444);
    } else if (dio) {
        f = filp_open(path, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC | O_SYNC | O_DIRECT, 0444);
    }

    if(!dio || (f == ERR_PTR(-EINVAL))) {
        DBG("Direct IO Disabled");
        f = filp_open(path, O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC, 0444);
        dio = 0;
    }

    if (!f || IS_ERR(f)) {
        DBG("Error opening file %ld", PTR_ERR(f));
        set_fs(fs);
        err = (f) ? PTR_ERR(f) : -EIO;
        f = NULL;
        return err;
    }

    set_fs(fs);

    return 0;
}

void cleanup_disk() {
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);
    if(f) filp_close(f, NULL);
    set_fs(fs);
}

ssize_t write_vaddr_disk(void * v, size_t is) {
    mm_segment_t fs;

    ssize_t s;
    loff_t pos;

    fs = get_fs();
    set_fs(KERNEL_DS);

    pos = f->f_pos;

    s = vfs_write(f, v, is, &pos);

    if (s == is) {
        f->f_pos = pos;
    }

    set_fs(fs);

    if (s != is && dio) {
        disable_dio();
        f->f_pos = pos;
        return write_vaddr_disk(v, is);
    }

    return s;
}
