#ifndef __KERN_FS_IOBUF_H__
#define __KERN_FS_IOBUF_H__

#include <defs.h>



/*
 * iobuf是一个缓冲区读写状态记录
 使用iobuf传递一个io请求(要写入设备的数据当前所在内存的位置和长度/从设备读取的数据需要存储到的位置)
 */
struct iobuf {
    void *io_base;     // 缓冲区的基地址（用于读写）
    off_t io_offset;   // 缓冲区中的当前读写位置，将按传输的数量递增
    size_t io_len;     // 缓冲区的长度（用于读写）
    size_t io_resid;   // 当前待读写的实际长度，将按传输的数量递减
};


#define iobuf_used(iob)                         ((size_t)((iob)->io_len - (iob)->io_resid))

struct iobuf *iobuf_init(struct iobuf *iob, void *base, size_t len, off_t offset);
int iobuf_move(struct iobuf *iob, void *data, size_t len, bool m2b, size_t *copiedp);
int iobuf_move_zeros(struct iobuf *iob, size_t len, size_t *copiedp);
void iobuf_skip(struct iobuf *iob, size_t n);

#endif /* !__KERN_FS_IOBUF_H__ */

