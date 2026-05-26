/**
 * @ Author: luoqi
 * @ Create Time: 2024-07-17 11:39
 * @ Modified by: luoqi
 * @ Modified time: 2025-07-20 14:36
 * @ Description: Header file for the ring buffer library
 */

#ifndef _QFIFO_H_
#define _QFIFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

#ifndef QFIFO_USE_STDLIBC
#define QFIFO_USING_STDLIBC_ 0
#else
#define QFIFO_USING_STDLIBC_ 1
#endif

/**
 * @brief Structure representing a kfifo.
 */
typedef struct {
    size_t in;          // Write index of the qfifo
    size_t out;         // Read index of the qfifo
    size_t sz;          // Total size of the qfifo
    void  *buf;         // Pointer to the buffer
    size_t max_used;    // Maximum number of bytes used in the qfifo
    void (*lock)(bool); // Pointer to the mutex lock function, true for blocking, false for non-blocking
} QFifo;

/**
 * @brief Initialize the fifo.
 * @param fifo Pointer to the fifo structure.
 * @param buf Pointer to the buffer.
 * @param sz Size of the buffer.
 * @param mutex_lock Pointer to the mutex lock function.
 * @param mutex_unlock Pointer to the mutex unlock function.
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
