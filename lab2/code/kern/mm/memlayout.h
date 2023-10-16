#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

//---------------------------------------
/*
1. 虚拟地址是39位,用64位(4*16,16个十六进制数)保存,只有低39位有效,高位进行符号扩展
2. 物理地址是56位
3. 虚拟地址和物理地址低12位都是页内偏移,所以物理页号一共有44位,虚拟页号有27位
-------------------------------------------------------------
页表项结构
1. 53-10一共44位表示一个物理页号(44+12=56)
2. 9-0描述映射的状态信息
|   63-54  |  53-28 |  27-19 |  18-10 | 9-8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|:--------:|:------:|:------:|:------:|:---:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| Reserved | PPN[2] | PPN[1] | PPN[0] | RSW | D | A | G | U | X | W | R | V |
| 10       | 26     | 9      | 9      | 2   | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |

RSW：两位留给 S Mode 的应用程序，我们可以用来进行拓展。
D：即 Dirty ，如果 D=1 表示自从上次 D 被清零后，有虚拟地址通过这个页表项进行写入。
A，即 Accessed，如果 A=1 表示自从上次 A 被清零后，有虚拟地址通过这个页表项进行读、或者写、或者取指。
G，即 Global，如果 G=1 表示这个页表项是”全局”的，也就是所有的地址空间（所有的页表）都包含这一项
U，即 user，U为 1 表示用户态 (U Mode)的程序 可以通过该页表项进映射。在用户态运行时也只能够通过 U=1 的页表项进行虚实地址映射。 注意，S Mode 不一定可以通过 U=1 的页表项进行映射。我们需要将 S Mode 的状态寄存器 sstatus 上的 SUM 位手动设置为 1 才可以做到这一点（通常情况不会把它置1）。否则通过 U=1 的页表项进行映射也会报出异常。另外，不论sstatus的SUM位如何取值，S Mode都不允许执行 U=1 的页面里包含的指令，这是出于安全的考虑。
R,W,X 为许可位，分别表示是否可读 (Readable)，可写 (Writable)，可执行 (Executable)。
----------------------------------------------------------------
三级页表
1. 虚拟地址页表号有27位,每个页表的页表号有9位,即512个页表项
2. 选择512的原因:一页是4096,一个页表项是64位(8字节),4096/8=512
3. 一级页表一个页表项对应2^12=4KB,二级页表的每个页表项对应9=2MB,三级页表的每个页表项对应9=1GB

4. 页表基址:三级页表保存在寄存器satp(一个csr)中,他的低44位是三级页表的页表号(因为三级页表是一个页,所以只保存他的页表号就行,不需要保存完整的地址)
            高4位mode: 0000表示不适用页表,直接使用物理地址;0100使用sv39;0101使用sv48
            中间16位ASID: 地址空间标识符.目前不用,os要为不同应用建立不同页表(实现内存隔离).每次切换访问的页表时,要更新satp的ppn,这个标识符可以告诉os现在使用的是哪个应用的页表
-----------------------------------------------------------------------

*/
//---------------------------------------







// 0x80200000是物理内存的起始地址,加上0xFFFFFFFF40000000这个偏移量把这个物理地址映射到虚拟地址上
//这个偏移量就在下面,PHYSICAL_MEMORY_OFFSET 
#define KERNBASE            0xFFFFFFFFC0200000 // = 0x80200000(物理内存里内核的起始位置, KERN_BEGIN_PADDR) + 0xFFFFFFFF40000000(偏移量, PHYSICAL_MEMORY_OFFSET)
// 虚拟内存的大小(和物理内存大小一致,即0x80200000~0x88000000)
#define KMEMSIZE            0x7E00000          // the maximum amount of physical memory
// 0x7E00000 = 0x8000000 - 0x200000
// QEMU 缺省的RAM为 0x80000000到0x88000000, 128MiB, 0x80000000到0x80200000被OpenSBI占用
// 虚拟内存最高位(0x88000000的虚拟地址,这个地址是FFFF FFFF C800 0000)
#define KERNTOP             (KERNBASE + KMEMSIZE) // 0x88000000对应的虚拟地址


#define PHYSICAL_MEMORY_END         0x88000000 // 物理地址结束
#define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000 // 物理地址映射到虚拟地址的偏移量
#define KERNEL_BEGIN_PADDR          0x80200000 //物理地址起始,PADDR
#define KERNEL_BEGIN_VADDR          0xFFFFFFFFC0200000 // 和KERNBASE一致,VADDR

// 内核栈中页面数量
#define KSTACKPAGE          2                           // # of pages in kernel stack
// 内核栈大小
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack

#ifndef __ASSEMBLER__

#include <defs.h>
#include <atomic.h>
#include <list.h>

typedef uintptr_t pte_t;
typedef uintptr_t pde_t;

/* *
 * struct Page - Page descriptor structures. Each Page describes one
 * physical page. In kern/mm/pmm.h, you can find lots of useful functions
 * that convert Page to other data types, such as physical address.
 * */

//物理页
struct Page {
    int ref;// 页帧的引用计数，即映射到此物理页的虚拟页个数
    uint64_t flags;// 物理页的状态标记，两个标记位，页帧的状态 Reserve 表示是否被内核保留，保留则置为1； 另一个表示是否可分配，可以分配则置1，为0表示已经分配出去，不能再分配,在下面的宏里有定义
    unsigned int property;// 记录连续空闲页块的数量,只在第一个页块上使用该变量
    //page_link是便于把多个连续内存空闲块链接在一起的双向链表指针，连续内存空闲块利用这个页的成员变量page_link来链接比它地址小和大的其他连续内存空闲块
    list_entry_t page_link;// 直接将 Page 这个结构体加入链表中会有点浪费空间 因此在 Page 中设置一个链表的结点 将其结点加入到链表中 还原的方法是将 链表中的 page_link 的地址 减去它所在的结构体中的偏移 就得到了 Page 的起始地址
};

/* Flags describing the status of a page frame */
#define PG_reserved                 0       // if this bit=1: the Page is reserved for kernel, cannot be used in alloc/free_pages; otherwise, this bit=0 
#define PG_property                 1       // if this bit=1: the Page is the head page of a free memory block(contains some continuous_addrress pages), and can be used in alloc_pages; if this bit=0: if the Page is the the head page of a free memory block, then this Page and the memory block is alloced. Or this Page isn't the head page.

#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags))
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags))
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))
#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags))
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))

// convert list entry to page
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - maintains a doubly linked list to record free (unused) pages */

//管理连续内存空闲块的双向链表
typedef struct {
    list_entry_t free_list;         // the list header
    unsigned int nr_free;           // 当前空闲页个数
} free_area_t;

#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */
