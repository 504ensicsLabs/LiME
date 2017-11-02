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
int ldigest_init(char *);
int ldigest_update(void *, size_t);
int ldigest_final(void);
void ldigest_clean(void);

static struct crypto_ahash *tfm;
static struct ahash_request *req;
static u8 *output;
static int digestsize;

int ldigest_init(char * digest) {
    DBG("Initializing digest transformation.");

    tfm = crypto_alloc_ahash(digest, 0, CRYPTO_ALG_ASYNC);
    if(unlikely(IS_ERR(tfm))) goto tfm_fail;

    req = ahash_request_alloc(tfm, GFP_ATOMIC);
    if(unlikely(!req))
        DBG("Failed to allocate request for %s", CRYPTO_ALG_ASYNC);

    digestsize = crypto_ahash_digestsize(tfm);
    output = kzalloc(sizeof(u8) * digestsize, GFP_ATOMIC);

    ahash_request_set_callback(req, 0, NULL, NULL);
    crypto_ahash_init(req);

    return LIME_DIGEST_COMPUTE;

tfm_fail:
    DBG("Failed to load transform for %s error:%ld",
            CRYPTO_ALG_ASYNC, PTR_ERR(tfm));
    ahash_request_free(req);
    crypto_free_ahash(tfm);
    ldigest_clean();

    return LIME_DIGEST_FAILED;
}

int ldigest_update(void * v, size_t is) {
    int ret;
    struct scatterlist sg;

    if (likely(virt_addr_valid((unsigned long) v))) {
        sg_init_one(&sg, (u8*) v, is);
    } else {
        DBG("Invalid virtual address, manually scanning page.");
        int nbytes = is;
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

    ahash_request_set_crypt(req, &sg, output, is);
    ret = crypto_ahash_update(req);
    if(ret < 0){
        DBG("Failed to update transform %i", ret);
        ldigest_clean();

        return LIME_DIGEST_FAILED;
    }

    return LIME_DIGEST_COMPUTE;
}

int ldigest_final(void) {
    int ret, i;
    char digest_value [digestsize * 2 + 1];
    DBG("Finalizing the digest.");
    
    ret = crypto_ahash_final(req);
    if(ret < 0){
        DBG("Failed to finalize digest %i", ret);
    }

    for(i = 0; i<digestsize; i++){
        sprintf(digest_value + i*2, "%02x", output[i]);
    }

    DBG("Digest is:\n %s", digest_value);

    ldigest_clean();

    return LIME_DIGEST_COMPUTE;
}

void ldigest_clean(void) {
    kfree(output);
    ahash_request_free(req);
    crypto_free_ahash(tfm);
}