# qfifo

`qfifo` 是一个轻量级、基于调用方提供内存的 FIFO 字节环形缓冲区。它不负责分配或释放内存，适用于嵌入式系统、驱动和其他需要固定内存占用的场景。

## 设计思想

- **字节队列**：缓冲区存放原始字节，不解释数据结构、消息边界或协议。调用方负责序列化和解析。
- **调用方管理内存**：`QFifo` 保存状态，数据缓冲区由调用方提供；初始化不会分配内存。
- **使用单调递增索引**：`in` 和 `out` 分别表示累计写入和读出的逻辑位置，已用字节数为 `in - out`。环形缓冲区不需要预留一个空位，因此容量为传入的完整 `sz`。
- **支持任意容量**：容量为 2 的幂时，通过掩码计算物理位置；否则使用取模，并在适当时归一化索引以保持环绕位置正确。
- **跨尾部读写**：逻辑数据跨越缓冲区末尾时，读写会拆成至多两段连续内存拷贝。
- **两种写入策略**：覆盖写优先保留新数据；非覆盖写只写入当前可用空间。

## API

在代码中包含 `qfifo.h`。以下函数处理数据的单位均为**字节**：

| 函数 | 说明 |
| --- | --- |
| `int qfifo_init(QFifo *fifo, void *buf, size_t sz, void (*lock)(bool))` | 使用调用方提供的缓冲区初始化队列。成功返回 `0`；参数无效时返回 `-1`，并将非空 `fifo` 置为未初始化状态。 |
| `size_t qfifo_write(QFifo *fifo, const void *data, size_t sz)` | 覆盖写入。空间不足时丢弃最旧数据；返回实际写入字节数。 |
| `size_t qfifo_write_non_cover(QFifo *fifo, const void *data, size_t sz)` | 非覆盖写入，只写入当前可用空间；返回实际写入字节数。 |
| `size_t qfifo_read(QFifo *fifo, void *buf, size_t sz)` | 读出并移除最多 `sz` 字节；返回实际读出字节数。 |
| `size_t qfifo_peek(QFifo *fifo, void *buf, size_t sz)` | 查看队首最多 `sz` 字节但不移除；返回实际查看字节数。 |
| `void qfifo_reset(QFifo *fifo)` | 清空队列并将历史峰值占用重置为 0。 |
| `size_t qfifo_used(QFifo *fifo)` | 返回当前已用字节数。 |
| `size_t qfifo_available(QFifo *fifo)` | 返回当前可用字节数。 |
| `size_t qfifo_max_used(QFifo *fifo)` | 返回自初始化或上次重置以来的峰值占用。 |

未初始化的队列、空指针参数或长度为 0 的读写请求不会执行数据操作；读写和查询函数返回 `0`，`qfifo_reset` 不做操作。

### 写入长度与返回值

- `qfifo_write` 的请求长度不超过容量时写入全部请求数据；超过容量时仅写入输入数据的**最后 `sz` 个字节**（其中 `sz` 指队列容量），返回容量。队列满或空间不足时会推进读位置，丢弃较旧数据。
- `qfifo_write_non_cover` 写入 `min(请求长度, 当前可用空间)` 个字节，保留队列中已有数据；队列已满时返回 `0`。请求长度超过容量时，它写入输入数据的前段，最多写满当前可用空间。
- `qfifo_read` 和 `qfifo_peek` 返回实际可取得的字节数：不会超过请求长度、队列中已有数据量或队列容量。队列为空时返回 `0`。
- `qfifo_available` 始终满足 `qfifo_used + qfifo_available == 容量`（对于有效队列）。

## 使用方法

```c
#include "qfifo.h"

#include <stdio.h>

int main(void)
{
    QFifo fifo;
    unsigned char storage[128];
    const char message[] = "hello";
    char result[sizeof(message)] = {0};

    /* NULL 表示不使用锁；需要并发访问时应提供合适的同步回调。 */
    if(qfifo_init(&fifo, storage, sizeof(storage), NULL) != 0) {
        return 1;
    }

    size_t written = qfifo_write_non_cover(&fifo, message, sizeof(message));
    size_t read = qfifo_read(&fifo, result, sizeof(result));

    if(written == sizeof(message) && read == sizeof(message)) {
        printf("%s\n", result);
    }
    return 0;
}
```

使用系统 C 库构建时，将 `qfifo.c` 与应用一起编译并链接，例如：

```sh
cc -std=c99 -I. example.c qfifo.c -o example
```

默认实现使用 `<string.h>` 中的 `memcpy`。若构建环境不依赖 C 库，可在编译 `qfifo.c` 时定义 `QFIFO_LIBC_UNDEPENDENT`，改用源码中的轻量拷贝实现。

## 并发与锁

`lock` 是可选的无返回值回调：传入 `true` 表示加锁，传入 `false` 表示解锁。读、写、窥视和重置操作会在修改或访问队列数据期间调用该回调。`qfifo_used`、`qfifo_available` 和 `qfifo_max_used` 不会调用锁回调；并发读写时，这些查询结果不应作为同步依据，调用方应自行协调。

共享队列时应提供能够保护该队列的同步机制。若队列会在中断服务程序（ISR）中访问，回调必须是中断安全的短临界区；不要在 ISR 中使用可能阻塞的互斥锁。未提供锁回调时，调用方必须自行保证访问不会发生冲突。
