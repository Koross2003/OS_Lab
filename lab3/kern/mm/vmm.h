#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>

// 预定义
struct mm_struct;
volatile size_t time_now; // mark 计时，用于lru

// 虚拟连续内存区域(vma)，[vm_start, vm_end)， 
// 如果一个地址属于一个vma，那么这个地址满足 vma.vm_start <= addr < vma.vm_end 

// mark vma_stuct管理虚拟地址对应的页
/*vma_struct(virtual memory address)结构体描述一段连续的虚拟地址，从vm_start到vm_end。 
通过包含一个list_entry_t成员，我们可以把同一个页表对应的多个vma_struct结构体串成一个链表，
在链表里把它们按照区间的起始点进行排序。
*/
/*
// mark mm_struct是一个页表中vma集合
每个页表（每个虚拟地址空间）可能包含多个vma_struct, 也就是多个访问权限可能不同的，不相交的连续地址区间。
我们用mm_struct结构体把一个页表对应的信息组合起来
*/


struct vma_struct {  
    struct mm_struct *vm_mm;  //指向一个比 vma_struct 更高的抽象层次的数据结构 mm_struct 
    uintptr_t vm_start;      //vma 的开始地址
    uintptr_t vm_end;      // vma 的结束地址
    uint_t vm_flags;     // 虚拟内存空间的属性
    list_entry_t list_link;  //双向链表，按照从小到大的顺序把虚拟内存空间链接起来
}; 

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004

// 同一页表下的vma集合
struct mm_struct {  
    list_entry_t mmap_list;  //双向链表头，链接了所有属于同一页目录表的虚拟内存空间
    struct vma_struct *mmap_cache;  //指向当前正在使用的虚拟内存空间
    pde_t *pgdir; //指向的就是 mm_struct数据结构所维护的页表
    int map_count; //记录 mmap_list 里面链接的 vma_struct 的个数
    void *sm_priv; //指向用来链接记录页访问情况的链表头
};  

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);
void reset_time(uintptr_t addr);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);

int do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;

#endif /* !__KERN_MM_VMM_H__ */

