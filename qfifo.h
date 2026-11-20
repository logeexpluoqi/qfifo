/**
 * Author: luoqi
 * Created Date: 2026-03-15 17:18:40
 * Last Modified: 2026-11-21 01:24:24
 * Modified By: luoqi at <**@****>
 * Copyright (c) 2026 <*****>
 * Description:
 */

#ifndef _QFIFO_H_
#define _QFIFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/* By default the libc <string.h> memcpy is used.
 * Define QFIFO_LIBC_UNDEPENDENT to use the built-in fallback instead. */

/**
 * @brief Structure representing a kfifo.
 */
typedef struct {
    volatile size_t in;  // Write index of the qfifo (monotonic, unsigned wrap is safe)
    volatile size_t out; // Read index of the qfifo (monotonic, unsigned wrap is safe)
    size_t sz;           // Total size of the qfifo
    size_t mask;         // sz - 1 when sz is a power of two, otherwise 0
    void  *buf;          // Pointer to the buffer
    size_t max_used;     // Maximum number of bytes used in the qfifo
    /* Optional lock callback: true = lock, false = unlock (may be NULL).
     * When the fifo is shared with an ISR, the callback MUST be interrupt
     * safe (e.g. a short critical section). A blocking RTOS mutex such as
     * tx_mutex_get(..., TX_WAIT_FOREVER) must not be used from an ISR. */
    void (*lock)(bool);
} QFifo;

/**
 * @brief Initialize the fifo.
 * @param fifo Pointer to the fifo structure.
 * @param buf Pointer to the buffer.
 * @param sz Size of the buffer (power-of-two sizes use a faster offset path;
 *           non power-of-two sizes are also supported and kept wrap-safe).
 * @param lock Optional lock callback, true = lock, false = unlock. May be NULL.
 *             Must be interrupt safe when the fifo is written from an ISR.
 * @return 0 on success, -1 on failure.
 */
int qfifo_init(QFifo *fifo, void *buf, size_t sz, void (*lock)(bool));

/**
 * @brief Get the number of bytes currently in the fifo.
 * @param fifo Pointer to the fifo structure.
 * @return Number of bytes used in the fifo.
 */
size_t qfifo_used(QFifo *fifo);

/**
 * @brief Get the number of free bytes available in the fifo.
 * @param fifo Pointer to the fifo structure.
 * @return Number of free bytes in the fifo.
 */
size_t qfifo_available(QFifo *fifo);

/**
 * @brief Write data to the fifo. Overwrite old data if there is insufficient space.
 * @param fifo Pointer to the fifo structure.
 * @param data Pointer to the data to be written.
 * @param sz Size of the data to be written.
 * @return The actual number of bytes written.
 */
size_t qfifo_write(QFifo *fifo, const void *data, size_t sz);

/**
 * @brief Write data to the fifo. non-cover mode.
 * @param fifo Pointer to the fifo structure.
 * @param data Pointer to the data to be written.
 * @param sz Size of the data to be written.
 * @return The actual number of bytes written.
 */
size_t qfifo_write_non_cover(QFifo *fifo, const void *data, size_t sz);

/**
 * @brief Read data from the fifo.
 * @param fifo Pointer to the fifo structure.
 * @param buf Pointer to store the read data.
 * @param sz Expected number of bytes to read.
 * @return The actual number of bytes read.
 */
size_t qfifo_read(QFifo *fifo, void *buf, size_t sz);

/**
 * @brief Peek data from the fifo.
 * @param fifo Pointer to the fifo structure.
 * @param rdata Pointer to store the peeked data.
 * @param sz Expected number of bytes to peek.
 * @return The actual number of bytes peeked.
 */
size_t qfifo_peek(QFifo *fifo, void *rdata, size_t sz);

/**
 * @brief Clear the fifo.
 * @param fifo Pointer to the fifo structure.
 */
void qfifo_reset(QFifo *fifo);

/**
 * @brief Get the maximum number of bytes used in the fifo.
 * @param fifo Pointer to the fifo structure.
 * @return Maximum number of bytes used in the fifo.
 */
size_t qfifo_max_used(QFifo *fifo);

#ifdef __cplusplus
}
#endif
#endif
