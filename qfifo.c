/**
 * Author: luoqi
 * Created Date: 2026-09-04 23:25:22
 * Last Modified: 2026-11-21 01:24:34
 * Modified By: luoqi at <**@****>
 * Copyright (c) 2026 <*****>
 * Description:
 */

#include "qfifo.h"
#include <stdint.h>

#ifndef QFIFO_LIBC_UNDEPENDENT
#include <string.h>
#define memcpy_ memcpy
#else
/* Word-wise fallback memcpy for libc-independent builds */
static void *memcpy_(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if((((uintptr_t)d | (uintptr_t)s) & (sizeof(size_t) - 1)) == 0) {
        while(n >= sizeof(size_t)) {
            *(size_t *)d = *(const size_t *)s;
            d += sizeof(size_t);
            s += sizeof(size_t);
            n -= sizeof(size_t);
        }
    }
    while(n--) {
        *d++ = *s++;
    }
    return dest;
}
#endif

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* NOTE: in/out are monotonically growing size_t indices. Unsigned wrap-around
 * is well-defined and keeps (in - out) correct, so no overflow guard is needed.
 * Physical positions are derived via qfifo_off() below. */

/** Map a logical index to a physical buffer offset (mask fast path for
 *  power-of-two sizes, modulo fallback otherwise). */
static size_t qfifo_off(const QFifo *fifo, size_t idx)
{
    return fifo->mask ? (idx & fifo->mask) : (idx % fifo->sz);
}

/** An uninitialized (or failed-init) fifo has sz == 0 / buf == NULL. Using it
 *  would divide by zero in qfifo_off() and copy to a NULL buffer, so every
 *  public entry point must reject it. */
static inline bool qfifo_ready(const QFifo *fifo)
{
    return fifo != NULL && fifo->buf != NULL && fifo->sz != 0;
}

/** Keep the monotonic indices bounded for non power-of-two sizes.
 *  Subtracting a whole multiple of sz leaves (idx % sz) - and therefore the
 *  physical position - unchanged, while avoiding the 2^32 index wrap where
 *  (2^32 % sz) != 0 would shift the write position relative to the read one.
 *  Power-of-two sizes need no fix-up because 2^32 is a multiple of sz.
 *  Must be called with the lock held. */
static void qfifo_normalize(QFifo *fifo)
{
    if(fifo->mask) {
        return;
    }
    size_t rings = fifo->out / fifo->sz;
    if(rings == 0) {
        return;
    }
    size_t k = rings * fifo->sz;
    fifo->in -= k;
    fifo->out -= k;
}

/** Copy @p sz bytes starting at the read position into @p buf (no state change). */
static void qfifo_copy_out(const QFifo *fifo, void *buf, size_t sz)
{
    size_t off = qfifo_off(fifo, fifo->out);
    size_t n = fifo->sz - off; /* bytes until physical wrap */
    if(sz <= n) {
        memcpy_(buf, (const char *)fifo->buf + off, sz);
    } else {
        memcpy_(buf, (const char *)fifo->buf + off, n);
        memcpy_((char *)buf + n, fifo->buf, sz - n);
    }
}

static void qfifo_update_max(QFifo *fifo)
{
    size_t used = fifo->in - fifo->out;
    if(used > fifo->max_used) {
        fifo->max_used = used;
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int qfifo_init(QFifo *fifo, void *buf, size_t sz, void (*lock)(bool))
{
    if(!fifo) {
        return -1;
    }
    /* Leave the object in a well-defined "not initialized" state even on
     * failure, otherwise a caller that ignores the return value would later
     * hit a divide-by-zero (sz == 0) or a NULL buffer. */
    fifo->in = 0;
    fifo->out = 0;
    fifo->sz = 0;
    fifo->mask = 0;
    fifo->buf = NULL;
    fifo->max_used = 0;
    fifo->lock = NULL;
    if(!buf || sz == 0) {
        return -1;
    }
    fifo->sz = sz;
    fifo->mask = ((sz & (sz - 1)) == 0) ? (sz - 1) : 0;
    fifo->buf = buf;
    fifo->lock = lock;
    return 0;
}

/**
 * @brief Cover-write: always succeeds (sz bytes accepted).
 *        If the buffer is full, oldest data is silently overwritten.
 *        Writes at most fifo->sz bytes of the *tail* of @p data when
 *        @p sz exceeds the buffer size.
 */
size_t qfifo_write(QFifo *fifo, const void *data, size_t sz)
{
    if(!qfifo_ready(fifo) || !data || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    if(used > fifo->sz) {
        /* Indices were corrupted (race or reset in between): treat the fifo as
         * full and repair the read position so "sz - used" cannot underflow. */
        fifo->out = fifo->in - fifo->sz;
        used = fifo->sz;
    }
    size_t freesp = fifo->sz - used;
    size_t wsz = (sz < fifo->sz) ? sz : fifo->sz;
    size_t skip = (sz > fifo->sz) ? (sz - fifo->sz) : 0;

    /* Advance out-pointer to make room (cover semantics) */
    if(wsz > freesp) {
        fifo->out += wsz - freesp;
    }

    /* Write in 1 or 2 contiguous segments (tail of input when sz > fifo->sz) */
    const uint8_t *src = (const uint8_t *)data + skip;
    size_t off = qfifo_off(fifo, fifo->in);
    size_t n = fifo->sz - off; /* bytes until physical wrap */

    if(wsz <= n) {
        memcpy_((char *)fifo->buf + off, src, wsz);
    } else {
        memcpy_((char *)fifo->buf + off, src, n);
        memcpy_(fifo->buf, src + n, wsz - n);
    }
    fifo->in += wsz;

    qfifo_update_max(fifo);
    qfifo_normalize(fifo);

    if(fifo->lock) {
        fifo->lock(false);
    }
    return wsz;
}

size_t qfifo_write_non_cover(QFifo *fifo, const void *data, size_t sz)
{
    if(!qfifo_ready(fifo) || !data || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    if(used > fifo->sz) {
        /* Same defensive repair as the cover-write path. */
        fifo->out = fifo->in - fifo->sz;
        used = fifo->sz;
    }
    size_t freesp = fifo->sz - used;
    size_t wsz = (sz < freesp) ? sz : freesp;

    if(wsz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Write in 1 or 2 contiguous segments */
    size_t off = qfifo_off(fifo, fifo->in);
    size_t n = fifo->sz - off;

    if(wsz <= n) {
        memcpy_((char *)fifo->buf + off, data, wsz);
    } else {
        memcpy_((char *)fifo->buf + off, data, n);
        memcpy_(fifo->buf, (const char *)data + n, wsz - n);
    }
    fifo->in += wsz;

    qfifo_update_max(fifo);
    qfifo_normalize(fifo);

    if(fifo->lock) {
        fifo->lock(false);
    }
    return wsz;
}

size_t qfifo_read(QFifo *fifo, void *buf, size_t sz)
{
    if(!qfifo_ready(fifo) || !buf || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    if(used > fifo->sz) {
        used = fifo->sz; /* corrupted index count: never read past the ring */
    }
    if(sz > fifo->sz) {
        sz = fifo->sz; /* the destination may be bigger than the ring */
    }
    sz = (sz < used) ? sz : used;

    if(sz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Read in 1 or 2 contiguous segments */
    qfifo_copy_out(fifo, buf, sz);
    fifo->out += sz;

    if(fifo->lock) {
        fifo->lock(false);
    }
    return sz;
}

void qfifo_reset(QFifo *fifo)
{
    if(!fifo) {
        return;
    }
    if(fifo->lock) {
        fifo->lock(true);
    }
    fifo->in = 0;
    fifo->out = 0;
    fifo->max_used = 0;
    if(fifo->lock) {
        fifo->lock(false);
    }
}

size_t qfifo_peek(QFifo *fifo, void *buf, size_t sz)
{
    if(!qfifo_ready(fifo) || !buf || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    if(used > fifo->sz) {
        used = fifo->sz; /* corrupted index count: never read past the ring */
    }
    if(sz > fifo->sz) {
        sz = fifo->sz; /* the destination may be bigger than the ring */
    }
    sz = (sz < used) ? sz : used;

    if(sz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Peek in 1 or 2 contiguous segments (read-only, out unchanged) */
    qfifo_copy_out(fifo, buf, sz);

    if(fifo->lock) {
        fifo->lock(false);
    }
    return sz;
}

size_t qfifo_used(QFifo *fifo)
{
    if(!qfifo_ready(fifo)) {
        return 0;
    }
    size_t used = fifo->in - fifo->out;
    return (used <= fifo->sz) ? used : fifo->sz;
}

size_t qfifo_available(QFifo *fifo)
{
    if(!qfifo_ready(fifo)) {
        return 0;
    }
    return fifo->sz - qfifo_used(fifo);
}

size_t qfifo_max_used(QFifo *fifo)
{
    if(!qfifo_ready(fifo)) {
        return 0;
    }
    return (fifo->max_used <= fifo->sz) ? fifo->max_used : fifo->sz;
}
