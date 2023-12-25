#ifndef __KERN_FS_DEVS_DEV_H__
#define __KERN_FS_DEVS_DEV_H__

#include <defs.h>

struct inode;
struct iobuf;


/*
 * 文件系统命名空间可访问的设备。
 * d_io既用于读取也用于写入；iobuf将指示方向。
 */
struct device {
    size_t d_blocks;
    size_t d_blocksize;
    int (*d_open)(struct device *dev, uint32_t open_flags);//打开设备
    int (*d_close)(struct device *dev);//关闭设备
    int (*d_io)(struct device *dev, struct iobuf *iob, bool write);//读写io
    int (*d_ioctl)(struct device *dev, int op, void *data);//input output control
};

#define dop_open(dev, open_flags)           ((dev)->d_open(dev, open_flags))
#define dop_close(dev)                      ((dev)->d_close(dev))
#define dop_io(dev, iob, write)             ((dev)->d_io(dev, iob, write))
#define dop_ioctl(dev, op, data)            ((dev)->d_ioctl(dev, op, data))

void dev_init(void);
struct inode *dev_create_inode(void);

#endif /* !__KERN_FS_DEVS_DEV_H__ */

