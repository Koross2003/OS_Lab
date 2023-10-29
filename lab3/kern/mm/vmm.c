#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <riscv.h>
#include <swap.h>
#include <clock.h>

/* 
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm is the memory manager for the set of continuous virtual memory  
  area which have the same PDT. vma is a continuous virtual memory area.
  There a linear link list for vma & a redblack link list for vma in mm.
---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

// szx func : print_vma and print_mm
void print_vma(char *name, struct vma_struct *vma){
	cprintf("-- %s print_vma --\n", name);
	cprintf("   mm_struct: %p\n",vma->vm_mm);
	cprintf("   vm_start,vm_end: %x,%x\n",vma->vm_start,vma->vm_end);
	cprintf("   vm_flags: %x\n",vma->vm_flags);
	cprintf("   list_entry_t: %p\n",&vma->list_link);
}

void print_mm(char *name, struct mm_struct *mm){
	cprintf("-- %s print_mm --\n",name);
	cprintf("   mmap_list: %p\n",&mm->mmap_list);
	cprintf("   map_count: %d\n",mm->map_count);
	list_entry_t *list = &mm->mmap_list;
	for(int i=0;i<mm->map_count;i++){
		list = list_next(list);
		print_vma(name, le2vma(list,list_link));
	}
}

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

// mark mm和vma的初始化,注意他们需要的内存空间均由kmalloc分配
// mm_create -  alloc a mm_struct & initialize it.
// 
struct mm_struct *
mm_create(void) {
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL) {
        list_init(&(mm->mmap_list));
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;

        if (swap_init_ok) swap_init_mm(mm); //页面置换初始化
        else mm->sm_priv = NULL;
    }
    return mm;
}

// vma_create - alloc a vma_struct & initialize it. (addr range: vm_start~vm_end)
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint_t vm_flags) {
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL) {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    return vma;
}


// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr) {
    struct vma_struct *vma = NULL;
    if (mm != NULL) {
        vma = mm->mmap_cache;
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr)) {
                bool found = 0;
                list_entry_t *list = &(mm->mmap_list), *le = list;
                while ((le = list_next(le)) != list) {
                    vma = le2vma(le, list_link);
                    if (vma->vm_start<=addr && addr < vma->vm_end) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    vma = NULL;
                }
        }
        if (vma != NULL) {
            mm->mmap_cache = vma;
            // //cprintf("\n\n\n whi \n\n\n ");
            // // mark 实现LRU,把这个vma所在的页的访问时间更新
            // list_entry_t *head = (list_entry_t*)mm->sm_priv;
            // //cprintf("\n\n\n whi \n\n\n ");
            // list_entry_t *ple = head;
            // //cprintf("\n\n\n whi \n\n\n ");
            // struct Page *target_page;
            // //cprintf("\n\n\n whi \n\n\n ");
            // while((ple = list_next(ple)) != head){
            //     cprintf("\n\n\n whi \n\n\n ");
            //     target_page = le2page(ple, page_link);
            //     cprintf("\n\n\n whi \n\n\n ");
            //     if(target_page->pra_vaddr == vma->vm_start){//找到了该需要替换的物理页
            //         target_page->last_used_time = ++time_now;
            //     }
            // }
        }
    }
    return vma;
}


// check_vma_overlap - check if vma1 overlaps vma2 ?
// mark 插入vma之前,保证他和原有的区间都不重合
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next) {
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end); // next是我们想插入的区间, 这里顺便检验了start < end
}


// insert_vma_struct -insert vma in mm's list link
// mark 插入一个新的vma
void
insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma) {
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

        list_entry_t *le = list;
        while ((le = list_next(le)) != list) {
            struct vma_struct *mmap_prev = le2vma(le, list_link);
            if (mmap_prev->vm_start > vma->vm_start) {
                break;
            }
            le_prev = le;
        }

    le_next = list_next(le_prev); // tag 保证插入后所有vma按照区间左端点有序排列,这很关键!检查的时候也方便检查是否有重合

    /* check overlap */
    if (le_prev != list) {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list) {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count ++; // 计数器
}

// mm_destroy - free mm and mm internal fields
// mark 这个好像不是这个函数的.查找某个虚拟地址对应的vma_stuct是否存在
//如果返回NULL，说明查询的虚拟地址不存在/不合法，既不对应内存里的某个页，也不对应硬盘里某个可以换进来的页
void
mm_destroy(struct mm_struct *mm) {

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list) {
        list_del(le);
        kfree(le2vma(le, list_link),sizeof(struct vma_struct));  //kfree vma        
    }
    kfree(mm, sizeof(struct mm_struct)); //kfree mm
    mm=NULL;
}

// vmm_init - initialize virtual memory management
//          - now just call check_vmm to check correctness of vmm
void
vmm_init(void) {
    check_vmm();
}

// check_vmm - check correctness of vmm
static void
check_vmm(void) {
    size_t nr_free_pages_store = nr_free_pages();
    check_vma_struct();
    check_pgfault();

    nr_free_pages_store--;	// szx : Sv39三级页表多占一个内存页，所以执行此操作
    assert(nr_free_pages_store == nr_free_pages());

    cprintf("check_vmm() succeeded.\n");
}

static void
check_vma_struct(void) {
    size_t nr_free_pages_store = nr_free_pages();

    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;

    int i;
    for (i = step1; i >= 1; i --) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i ++) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i ++) {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i +=5) {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i+1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i+2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i+3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i+4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i  && vma1->vm_end == i  + 2);
        assert(vma2->vm_start == i  && vma2->vm_end == i  + 2);
    }

    for (i =4; i>=0; i--) {
        struct vma_struct *vma_below_5= find_vma(mm,i);
        if (vma_below_5 != NULL ) {
           cprintf("vma_below_5: i %x, start %x, end %x\n",i, vma_below_5->vm_start, vma_below_5->vm_end); 
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

    assert(nr_free_pages_store == nr_free_pages());

    cprintf("check_vma_struct() succeeded!\n");
}

struct mm_struct *check_mm_struct;

// check_pgfault - check correctness of pgfault handler
static void
check_pgfault(void) {
	// char *name = "check_pgfault";
    size_t nr_free_pages_store = nr_free_pages();

    check_mm_struct = mm_create();

    assert(check_mm_struct != NULL);
    struct mm_struct *mm = check_mm_struct;
    pde_t *pgdir = mm->pgdir = boot_pgdir;
    assert(pgdir[0] == 0);

    struct vma_struct *vma = vma_create(0, PTSIZE, VM_WRITE);

    assert(vma != NULL);

    insert_vma_struct(mm, vma);

    uintptr_t addr = 0x100;
    assert(find_vma(mm, addr) == vma);

    int i, sum = 0;
    for (i = 0; i < 100; i ++) {
        *(char *)(addr + i) = i;
        sum += i;
    }
    for (i = 0; i < 100; i ++) {
        sum -= *(char *)(addr + i);
    }
    assert(sum == 0);

    page_remove(pgdir, ROUNDDOWN(addr, PGSIZE));

    free_page(pde2page(pgdir[0]));

    pgdir[0] = 0;

    mm->pgdir = NULL;
    mm_destroy(mm);

    check_mm_struct = NULL;
    nr_free_pages_store--;	// szx : Sv39第二级页表多占了一个内存页，所以执行此操作

    assert(nr_free_pages_store == nr_free_pages());

    cprintf("check_pgfault() succeeded!\n");
}
//page fault number
volatile unsigned int pgfault_num=0;

/* do_pgfault - 处理page fault的处理程序
 * @mm         : mm struct(一组在相同页表中的vma)
 * @error_code : 由x86硬件设置的记录在trapframe->tf_err中的错误代码
 * @addr       : 导致page fault的虚拟地址（CR2寄存器的内容）
 *
 * 调用图：trap--> trap_dispatch-->pgfault_handler-->do_pgfault
 * 处理器向ucore的do_pgfault函数提供了两个信息以帮助诊断异常并从中恢复。
 *   (1) CR2寄存器的内容。处理器使用32位线性地址加载CR2寄存器，该地址生成异常。
 *       do_pgfault函数可以使用此地址来定位相应的页目录和页表条目。
 *   (2) 内核堆栈上的错误代码。页面故障的错误代码格式与其他异常不同。
 *       错误代码告诉异常处理程序三件事：
 *         -- P标志（位0）指示异常是由于未出现的页面（0）还是由于访问权限违规或使用保留位（1）引起的。
 *         -- W/R标志（位1）指示导致异常的内存访问是读取（0）还是写入（1）。
 *         -- U/S标志（位2）指示处理器在异常发生时是否以用户模式（1）或监管模式（0）执行。
 */
int
do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr) {
    //addr: 访问出错的虚拟地址
    int ret = -E_INVAL;
    //找到一个包含出错地址的vma,由于vma是连续的，所以顺序找就行了
    struct vma_struct *vma = find_vma(mm, addr);
    //我们首先要做的就是在mm_struct里判断这个虚拟地址是否可用
    pgfault_num++;
    // 检查这个虚拟地址是否在vma范围内(如果不在这个范围内说明这个虚拟地址不在这个进程允许的虚拟空间内)
    if (vma == NULL || vma->vm_start > addr) { 
        cprintf("not valid addr %x, and  can not find it in vma\n", addr);
        goto failed;
    }

    /* IF (write an existed addr ) OR
     *    (write an non_existed addr && addr is writable) OR
     *    (read  an non_existed addr && addr is readable)
     * THEN
     *    continue process
     */

    // perm用于创建页表项时作为标志位, 如果这个vma是可写的,则将他赋值为可读可写
    uint32_t perm = PTE_U; // 初始化用户可访问
    if (vma->vm_flags & VM_WRITE) {
        perm |= (PTE_R | PTE_W); // 添加可读可写权限
    }

    addr = ROUNDDOWN(addr, PGSIZE); // 按照页面大小向下对齐地址(因为一个页的页首地址是低地址)

    ret = -E_NO_MEM;

    pte_t *ptep=NULL;
/*
* 宏定义或函数：
*   get_pte：获取一个PTE并返回此PTE的虚拟地址，针对线性地址la
*             如果包含此PTE的页表不存在，为页表分配一页（注意第三个参数'1'）
*   pgdir_alloc_page：调用alloc_page和page_insert函数分配一个页面大小的内存并设置
*             地址映射pa<--->la，使用线性地址la和PDT pgdir
* 常量定义：
*   VM_WRITE  ：如果vma->vm_flags＆VM_WRITE == 1/0，则vma是可写/不可写的
*   PTE_W           0x002                   // 页表/目录项标志位：可写
*   PTE_U           0x004                   // 页表/目录项标志位：用户可以访问
* 变量：
*   mm->pgdir ：这些vma的页目录表
*
*/

    // 找到对应的页表项,如果对应的页表项不存在就创建一个对应的页表
    // pgdir是三级页表的物理地址,
    // 注意这个1,表示如果不存在就一级一级创建对应的页表项
    // ptep存储的是最后一级页表中addr对应的页表项的虚拟地址

    // mark
    /** 设计思路：
    首先检查页表中是否有相应的表项，如果表项为空，那么说明没有映射过；
    然后使用 pgdir_alloc_page 获取一个物理页，同时进行错误检查即可。*/

    ptep = get_pte(mm->pgdir, addr, 1);  
    //如果页表不存在，尝试分配一空闲页，匹配物理地址与逻辑地址，建立对应关系
    if (*ptep == 0) { // mark 说实话这里我觉得压根没用,上面那种分配方式不应该出现0,因为我们设置的就是1,这里问问助教()
        if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) { // 分配空间
            cprintf("pgdir_alloc_page in do_pgfault failed\n");
            goto failed;
        }
    } else {
        /*LAB3 EXERCISE 3: YOUR CODE 2113644
        * 请你根据以下信息提示，补充函数
        * 现在我们认为pte是一个交换条目，那我们应该从磁盘加载数据并放到带有phy addr的页面，
        * 并将phy addr与逻辑addr映射，触发交换管理器记录该页面的访问情况
        *
        *  一些有用的宏和定义，可能会对你接下来代码的编写产生帮助(显然是有帮助的)
        *  宏或函数:
        *    swap_in(mm, addr, &page) : 分配一个内存页，然后根据
        *    PTE中的swap条目的addr，找到磁盘页的地址，将磁盘页的内容读入这个内存页
        *    page_insert ： 建立一个Page的phy addr与线性addr la的映射
        *    swap_map_swappable ： 设置页面可交换
        */
       // mark
       /*如果 PTE 存在，那么说明这一页已经映射过了但是被保存在磁盘中，需要将这一页内存交换出来：
      1.调用 swap_in 将内存页从磁盘中载入内存；
      2.调用 page_insert 建立物理地址与线性地址之间的映射；
      3.设置页对应的虚拟地址，方便交换出内存时将正确的内存数据保存在正确的磁盘位置；
      4.调用 swap_map_swappable 将物理页框加入 FIFO。*/
        if (swap_init_ok) {
            struct Page *page = NULL;
            // 你要编写的内容在这里，请基于上文说明以及下文的英文注释完成代码编写
            //(1）According to the mm AND addr, try
            //to load the content of right disk page
            //into the memory which page managed.
            // 根据 mm 和 addr，将适当的磁盘页的内容加载到由 page 管理的内存中
            //(2) According to the mm,
            //addr AND page, setup the
            //map of phy addr <--->
            //logical addr            
            //(3) make the page swappable.

            //在swap_in()函数执行完之后，page保存换入的物理页面。
            //swap_in()函数里面可能把内存里原有的页面换出去
            if (swap_in(mm, addr, &page) != 0) {
                cprintf("swap_in failed\n");
                goto failed;
            }
            // 更新页表, 插入新的页表项
            if (page_insert(mm->pgdir, page, addr, perm) != 0) {
                cprintf("page_insert failed\n");
                goto failed;
            }
            swap_map_swappable(mm, addr, page, 1); // 标志这个页面将来是可再换出的
            page->pra_vaddr = addr;
        } else {
            cprintf("no swap_init_ok but ptep is %x, failed\n", *ptep);
            goto failed;
        }
   }

   ret = 0;
failed:
    return ret;
}