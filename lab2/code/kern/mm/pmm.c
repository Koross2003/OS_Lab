#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
#include <riscv.h>
#include <buddy_pmm.h>

// virtual address of physical page array
// 物理页面数组的虚拟地址
struct Page *pages;
// amount of physical memory (in pages)
// 物理内存量（以页面为单位）(到0x8800的页数)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
// 内核映像在 VA=KERNBASE 和 PA=info.base 处进行映射。
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000

// DRAM_BASE是内存起始的物理地址,nbase是其按照页来算的起始地址(0x8000)
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
// 启动时页目录的虚拟地址
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
// 启动时页目录的物理地址
uintptr_t satp_physical;

// physical memory management
// 物理内存管理器
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
// 初始化一个pmm_manager
static void init_pmm_manager(void) {
    //pmm_manager = &best_fit_pmm_manager;
    pmm_manager = &buddy_pmm_manager;
    //pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    // 调用pmm_init
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL;
    bool intr_flag;
    // 保存中断状态并禁用中断
    local_intr_save(intr_flag);
    {
        // 调用pmm_alloc分配内存
        page = pmm_manager->alloc_pages(n);
    }
    // 恢复中断状态
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        //调用pmm_free释放内存
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        // 查看当前链表中剩余多少空闲页
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

// 初始化page
static void page_init(void) {
    va_pa_offset = PHYSICAL_MEMORY_OFFSET; // 0xFFFFFFFF40000000


    // 获取内存范围的 起始地址 大小 结束地址,都是用物理地址计算的
    uint64_t mem_begin = KERNEL_BEGIN_PADDR;// 0x80200000
    uint64_t mem_size = PHYSICAL_MEMORY_END - KERNEL_BEGIN_PADDR;
    uint64_t mem_end = PHYSICAL_MEMORY_END; //0x88000000

    //打印物理内存的映射信息
    cprintf("physcial memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n", mem_size, mem_begin,
            mem_end - 1);

//----------------这里没看懂-----------------------
    // 计算可用物理内存上限(maxpa),如果超过了内核的顶部地址(KERNTOP),则将其限制为KERNTOP
    uint64_t maxpa = mem_end;

    if (maxpa > KERNTOP) {
        maxpa = KERNTOP;
    }
    // kerntop是内存结束地址(物理地址为0x8800)对应的虚拟地址,但是maxpa保存的是0x8800
    // 为什么如果物理地址更大,就把物理内存上限置为虚拟地址
    // 按照这个实验设计,物理地址一定没有虚拟地址高
    // 但是如果物理地址更低,就要把物理内存上限设置为一个虚拟地址,这么做的目的是什么???
//-------------------------------------------------------



    // 内核所占空间结束的位置
    extern char end[];

    // 物理内存一共有多少页(他这个是从0x00开始算的)
    npage = maxpa / PGSIZE;
    
    // pages指向内核所占空间结束后的第一页
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE); // pages也包含nbase~npage这段的页

    // 一开始把所有页面都设置为保留给内核使用的,之后在设置哪些可以分配给其他程序
    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i);
    }

    // kernel_end到freemem的部分一堆Page Struct
    // 我们可以自由使用的物理内存起始位置
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));

    // 按页对齐
    mem_begin = ROUNDUP(freemem, PGSIZE);
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (freemem < mem_end) {
        // 初始化我们可以自由使用的物理内存
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    // 物理内存管理器
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    // 检测物理内存空间，保留已使用的内存，然后使用pmm->init_memmap创建空闲页面列表
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    // 使用pmm->check来验证pmm中分配/释放函数的正确性
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
