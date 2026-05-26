/**
 * @ Author: luoqi
 * @ Create Time: 2025-06-24 21:06
 * @ Modified by: luoqi
 * @ Modified time: 2025-07-20 14:37
 * @ Description: Ring buffer (kfifo) — optimized single/double-segment I/O
 */

#include "qfifo.h"
#include <stdint.h>

#if QFIFO_USING_STDLIBC_
#include <string.h>
#define memcpy_ memcpy
#else
static void *memcpy_(void *dest, const void *src, size_t n)
{
    if(!dest || !src) {
        return NULL;
    }
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while(n--) {
        *d++ = *s++;
    }
    return dest;
}
#endif

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/** Reset monotonically-growing indices when they approach SIZE_MAX/2,
 *  keeping the logical content unchanged.  Always safe because used <= sz
 *  and sz << SIZE_MAX/2 on any real platform. */
static void qfifo_reset_indices(QFifo *fifo)
{
    size_t used = fifo->in - fifo->out;
    fifo->in = used;
    fifo->out = 0;
}

static void qfifo_check_overflow(QFifo *fifo)
{
    if(fifo->in >= (SIZE_MAX >> 1) || fifo->out >= (SIZE_MAX >> 1)) {
        qfifo_reset_indices(fifo);
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
    if(!fifo || !buf || sz == 0) {
        return -1;
    }
    fifo->out = 0;
    fifo->in = 0;
    fifo->sz = sz;
    fifo->buf = buf;
    fifo->max_used = 0;
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
    if(!fifo || !data || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    size_t free = fifo->sz - used;
    size_t wsz = (sz < fifo->sz) ? sz : fifo->sz;
    size_t skip = (sz > fifo->sz) ? (sz - fifo->sz) : 0;

    /* Advance out-pointer to make room (cover semantics) */
    if(wsz > free) {
        fifo->out += wsz - free;
    }

    /* Write in 1 or 2 contiguous segments (tail of input when sz > fifo->sz) */
    const uint8_t *src = (const uint8_t *)data + skip;
    size_t off = fifo->in % fifo->sz;
    size_t n = fifo->sz - off; /* bytes until physical wrap */

    if(wsz <= n) {
        memcpy_((char *)fifo->buf + off, src, wsz);
    } else {
        memcpy_((char *)fifo->buf + off, src, n);
        memcpy_(fifo->buf, src + n, wsz - n);
    }
    fifo->in += wsz;

    qfifo_check_overflow(fifo);
    qfifo_update_max(fifo);

    if(fifo->lock) {
        fifo->lock(false);
    }
    return wsz;
}

size_t qfifo_write_non_cover(QFifo *fifo, const void *data, size_t sz)
{
    if(!fifo || !data || sz == 0) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    size_t free = fifo->sz - used;
    size_t wsz = (sz < free) ? sz : free;

    if(wsz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Write in 1 or 2 contiguous segments */
    size_t off = fifo->in % fifo->sz;
    size_t n = fifo->sz - off;

    if(wsz <= n) {
        memcpy_((char *)fifo->buf + off, data, wsz);
    } else {
        memcpy_((char *)fifo->buf + off, data, n);
        memcpy_(fifo->buf, (const char *)data + n, wsz - n);
    }
    fifo->in += wsz;

    qfifo_check_overflow(fifo);
    qfifo_update_max(fifo);

    if(fifo->lock) {
        fifo->lock(false);
    }
    return wsz;
}

size_t qfifo_read(QFifo *fifo, void *buf, size_t sz)
{
    if(!fifo || !buf) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    sz = (sz < used) ? sz : used;

    if(sz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Read in 1 or 2 contiguous segments */
    size_t off = fifo->out % fifo->sz;
    size_t n = fifo->sz - off;

    if(sz <= n) {
        memcpy_(buf, (char *)fifo->buf + off, sz);
    } else {
        memcpy_(buf, (char *)fifo->buf + off, n);
        memcpy_((char *)buf + n, fifo->buf, sz - n);
    }
    fifo->out += sz;

    qfifo_check_overflow(fifo);

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
    if(!fifo || !buf) {
        return 0;
    }

    if(fifo->lock) {
        fifo->lock(true);
    }

    size_t used = fifo->in - fifo->out;
    sz = (sz < used) ? sz : used;

    if(sz == 0) {
        if(fifo->lock) {
            fifo->lock(false);
        }
        return 0;
    }

    /* Peek in 1 or 2 contiguous segments (read-only, out unchanged) */
    size_t off = fifo->out % fifo->sz;
    size_t n = fifo->sz - off;

    if(sz <= n) {
        memcpy_(buf, (char *)fifo->buf + off, sz);
    } else {
        memcpy_(buf, (char *)fifo->buf + off, n);
        memcpy_((char *)buf + n, fifo->buf, sz - n);
    }

    if(fifo->lock) {
        fifo->lock(false);
    }
    return sz;
}

size_t qfifo_used(QFifo *fifo)
{
    if(!fifo) {
        return 0;
    }
    return fifo->in - fifo->out;
}

size_t qfifo_available(QFifo *fifo)
{
    if(!fifo) {
        return 0;
    }
    return fifo->sz - (fifo->in - fifo->out);
}

size_t qfifo_max_used(QFifo *fifo)
{
    if(!fifo) {
        return 0;
    }
    return fifo->max_used;
}
