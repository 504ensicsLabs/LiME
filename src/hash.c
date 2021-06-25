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

// External
extern ssize_t write_vaddr_tcp(void *, size_t);
extern int setup_tcp(void);
extern void cleanup_tcp(void);

extern ssize_t write_vaddr_disk(void *, size_t);
extern int setup_disk(char *, int);
extern void cleanup_disk(void);

static u8 *output;
static int digestsize;
static char *digest_value;

extern char *digest;
extern char *path;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
static struct crypto_ahash *tfm;
static struct ahash_request *req;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
static struct crypto_hash *tfm;
static struct hash_desc desc;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
struct crypto_tfm *tfm;
#endif

int ldigest_init(void) {
    DBG("Initializing Digest Transformation.");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    tfm = crypto_alloc_ahash(digest, 0, CRYPTO_ALG_ASYNC);
    if (unlikely(IS_ERR(tfm))) goto init_fail;

    req = ahash_request_alloc(tfm, GFP_ATOMIC);
    if (unlikely(!req)) goto init_fail;

    digestsize = crypto_ahash_digestsize(tfm);

    ahash_request_set_callback(req, 0, NULL, NULL);
    crypto_ahash_init(req);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    tfm = crypto_alloc_hash(digest, 0, CRYPTO_ALG_ASYNC);
    if (unlikely(IS_ERR(tfm)))
        goto init_fail;

    desc.tfm = tfm;
    desc.flags = 0;

    digestsize = crypto_hash_digestsize(tfm);
    crypto_hash_init(&desc);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    tfm = crypto_alloc_tfm(digest, 0);
    if (unlikely(tfm == NULL))
        goto init_fail;

    crypto_digest_init(tfm);
#else
    DBG("Digest not supported for this kernel version.");
    goto init_fail;
#endif

    output = kzalloc(sizeof(u8) * digestsize, GFP_ATOMIC);

    return LIME_DIGEST_COMPUTE;

init_fail:
    DBG("Digest Initialization Failed.");
    return LIME_DIGEST_FAILED;
}

int ldigest_update(void *v, size_t is) {
    int ret;
    struct scatterlist sg;

    if (likely(virt_addr_valid((unsigned long) v))) {
        sg_init_one(&sg, (u8 *) v, is);
    } else {
        int nbytes = is;

        DBG("Invalid Virtual Address, Manually Scanning Page.");
        while (nbytes > 0) {
            int len = nbytes;
            int off = offset_in_page(v);
            if (off + len > (int)PAGE_SIZE)
                len = PAGE_SIZE - off;
            sg_init_table(&sg, 1);
            sg_set_page(&sg, vmalloc_to_page((u8 *) v), len, off);
             
            v += len;
            nbytes -= len;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ahash_request_set_crypt(req, &sg, output, is);
    ret = crypto_ahash_update(req);
    if (ret < 0)
        goto update_fail;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    ret = crypto_hash_update(&desc, &sg, is);
    if (ret < 0) 
        goto update_fail;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    crypto_digest_update(tfm, &sg, is);
#endif

    return LIME_DIGEST_COMPUTE;

update_fail:
    DBG("Digest Update Failed.");
    return LIME_DIGEST_FAILED;
}

int ldigest_final(void) {
    int ret, i;

    DBG("Finalizing the digest.");
    digest_value = kmalloc(digestsize * 2 + 1, GFP_KERNEL);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ret = crypto_ahash_final(req);
    if (ret < 0)
        goto final_fail;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    ret = crypto_hash_final(&desc, output);
    if (ret < 0)
        goto final_fail;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    crypto_digest_final(tfm, output);
#endif

    for (i = 0; i<digestsize; i++) {
        sprintf(digest_value + i*2, "%02x", output[i]);
    }

    DBG("Digest is: %s", digest_value);
    return LIME_DIGEST_COMPLETE;

final_fail:
    DBG("Failed to finalize the Digest.");
    return LIME_DIGEST_FAILED;
}

int ldigest_write_tcp(void) {
    int ret;

    ret = setup_tcp();
    if (ret < 0) {
        DBG("Socket bind failed for digest file: %d", ret);
        cleanup_tcp();
        return LIME_DIGEST_FAILED;
    }

    RETRY_IF_INTERRUPTED(write_vaddr_tcp(digest_value, digestsize * 2));

    cleanup_tcp();

    return 0;
}

int ldigest_write_disk(void) {
    char *p;
    int ret = 0;

    p = kmalloc(strlen(path) + strlen(digest) + 2, GFP_KERNEL);
    if (!p)
        return LIME_DIGEST_FAILED;

    strcpy(p, path);
    strcat(p, ".");
    strcat(p, digest);

    if (setup_disk(p, 0)) {
        ret = LIME_DIGEST_FAILED;
        goto out;
    }

    RETRY_IF_INTERRUPTED(write_vaddr_disk(digest_value, digestsize * 2));

out:
    cleanup_disk();
    kfree(p);

    return ret;
}

void ldigest_clean(void) {
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
