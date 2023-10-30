#include <assert.h>
#include <defs.h>
#include <fs.h>
#include <ide.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <riscv.h>

void ide_init(void) {}

#define MAX_IDE 2
#define MAX_DISK_NSECS 56
static char ide[MAX_DISK_NSECS * SECTSIZE];

// ideno是硬盘编号,假设的是有多块硬盘,实际上我们只有一个,所以这个参数在各个函数里都没啥用
bool ide_device_valid(unsigned short ideno) { return ideno < MAX_IDE; } //这个硬盘编号是否合法

size_t ide_device_size(unsigned short ideno) { return MAX_DISK_NSECS; }// 这像是在模拟每个扇区最大容量

int ide_read_secs(unsigned short ideno, uint32_t secno, void *dst,
                  size_t nsecs) {
    int iobase = secno * SECTSIZE;
    // mark 硬盘I/O模拟,就是在内存里copy一下
    memcpy(dst, &ide[iobase], nsecs * SECTSIZE);// mark 我们要求按照扇区大小来作为传输数据的基本单位,所以要对齐扇区大小SECTSIZE(512)
    return 0;
}

int ide_write_secs(unsigned short ideno, uint32_t secno, const void *src,
                   size_t nsecs) {
    int iobase = secno * SECTSIZE;
    memcpy(&ide[iobase], src, nsecs * SECTSIZE);
    return 0;
}
