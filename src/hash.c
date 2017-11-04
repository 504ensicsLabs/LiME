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

// This file
int ldigest_init(void);
int ldigest_update(void *, size_t);
int ldigest_final(void);
static void ldigest_clean(void);
static int ldigest_write(void);

// External
extern ssize_t write_vaddr(void *, size_t);
extern int setup(void);
extern void cleanup(void);

static u8 * output;
static int digestsize;
static char * digest_value;

extern char * digest;
extern char * path;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
static struct crypto_ahash *tfm;
static struct ahash_request *req;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static struct crypto_hash *tfm;
static struct hash_desc desc;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
struct crypto_tfm *tfm;
#endif


int ldigest_init() {
    DBG("Initializing digest transformation.");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    tfm = crypto_alloc_ahash(digest, 0, CRYPTO_ALG_ASYNC);
    if (unlikely(IS_ERR(tfm))) goto tfm_fail;

    req = ahash_request_alloc(tfm, GFP_ATOMIC);
    if (unlikely(!req))
        DBG("Failed to allocate request for %s", CRYPTO_ALG_ASYNC);

    digestsize = crypto_ahash_digestsize(tfm);

    ahash_request_set_callback(req, 0, NULL, NULL);
    crypto_ahash_init(req);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    tfm = crypto_alloc_hash(digest, 0, CRYPTO_ALG_ASYNC);
    if (unlikely(IS_ERR(tfm))) goto tfm_fail;

    desc.tfm = tfm;
    desc.flags = 0;

    digestsize = crypto_hash_digestsize(tfm);
    crypto_hash_init(&desc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    tfm = crypto_alloc_tfm(digest, 0);
    if (unlikely(tfm == NULL)) goto tfm_fail;
    crypto_digest_init(tfm);
#else
    DBG("Digest not supported for this kernel version.");
    return LIME_DIGEST_FAILED;
#endif

    output = kzalloc(sizeof(u8) * digestsize, GFP_ATOMIC);

    return LIME_DIGEST_COMPUTE;

tfm_fail:
    DBG("Failed to load transform for %s error:%ld",
            CRYPTO_ALG_ASYNC, PTR_ERR(tfm));
    ldigest_clean();

    return LIME_DIGEST_FAILED;
}

int ldigest_update(void * v, size_t is) {
    int ret;
    struct scatterlist sg;

    if (likely(virt_addr_valid((unsigned long) v))) {
        sg_init_one(&sg, (u8*) v, is);
    } else {
        int nbytes = is;

        DBG("Invalid virtual address, manually scanning page.");
        while (nbytes > 0) {
            int len = nbytes;
            int off = offset_in_page(v);
            if (off + len > (int)PAGE_SIZE)
                len = PAGE_SIZE - off;
            sg_init_table(&sg, 1);
            sg_set_page(&sg, vmalloc_to_page((u8*) v), len, off);
             
            v += len;
            nbytes -= len;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ahash_request_set_crypt(req, &sg, output, is);
    ret = crypto_ahash_update(req);
    if(ret < 0){
        DBG("Failed to update transform %i", ret);
        ldigest_clean();

        return LIME_DIGEST_FAILED;
    }
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    ret = crypto_hash_update(&desc, &sg, is);
    if(ret < 0){
        DBG("Failed to update transform %i", ret);
        ldigest_clean();

        return LIME_DIGEST_FAILED;
    }
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    ret = crypto_digest_update(tfm, &sg, is);
#endif

    return LIME_DIGEST_COMPUTE;
}

int ldigest_final(void) {
    int ret, i;
    digest_value = kmalloc(digestsize * 2 + 1, GFP_KERNEL);
    DBG("Finalizing the digest.");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ret = crypto_ahash_final(req);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    ret = crypto_hash_final(&desc, output);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    ret = crypto_digest_final(tfm, output);
#endif

    if(ret < 0){
        DBG("Failed to finalize digest %i", ret);
    }

    for(i = 0; i<digestsize; i++){
        sprintf(digest_value + i*2, "%02x", output[i]);
    }

    DBG("Digest is: %s", digest_value);

    ldigest_write();
    ldigest_clean();

    return LIME_DIGEST_COMPLETE;
}

static int ldigest_write(void) {
    int i;
    int err = 0;
    void *ptr;

    DBG("Writing Out Digest.");
    ptr = krealloc(path, strlen(path) + strlen(digest) + 2, GFP_KERNEL);
    if(unlikely(!ptr)) {
        DBG("Reallocation failed");
        cleanup();
    }

    strcpy(path, path);
    strcat(path,".");
    strcat(path, digest);

    if((err = setup())) {
        DBG("Setup Error for Digest File");
        cleanup();
    }

    for(i = 0; i<digestsize*2; i++){
        write_vaddr(&digest_value[i], 1);  
    }

    DBG("Digest File Write Complete.");

    cleanup();

    return 0;
}

static void ldigest_clean(void) {
    kfree(output);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    crypto_free_ahash(tfm);
    ahash_request_free(req);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    crypto_free_hash(tfm);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    crypto_free_tfm(tfm);
#endif
}
