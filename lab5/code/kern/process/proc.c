#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  + 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep 
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    //LAB4:EXERCISE1 YOUR CODE
    /*
     * below fields in proc_struct need to be initialized
     *       enum proc_state state;                      // Process state
     *       int pid;                                    // Process ID
     *       int runs;                                   // the running times of Proces
     *       uintptr_t kstack;                           // Process kernel stack
     *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
     *       struct proc_struct *parent;                 // the parent process
     *       struct mm_struct *mm;                       // Process's memory management field
     *       struct context context;                     // Switch here to run process
     *       struct trapframe *tf;                       // Trap frame for current interrupt
     *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
     *       uint32_t flags;                             // Process flag
     *       char name[PROC_NAME_LEN + 1];               // Process name
     */

     //LAB5 YOUR CODE : (update LAB4 steps)
     /*
     * below fields(add in LAB5) in proc_struct need to be initialized  
     *       uint32_t wait_state;                        // waiting state
     *       struct proc_struct *cptr, *yptr, *optr;     // relations between processes
     */

        proc->state = PROC_UNINIT;
    	proc->pid = -1;
    	proc->runs = 0;
    	proc->kstack = NULL;
    	proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->cr3 = boot_cr3;
        proc->flags = 0;
        memset(proc->name, 0, PROC_NAME_LEN);
        proc->wait_state = 0; //PCB新增的条目，初始化进程等待状态
        proc->cptr = proc->optr = proc->yptr = NULL;//设置指针

    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc) {
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process ++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc) {
    list_del(&(proc->list_link));
    if (proc->optr != NULL) {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL) {
        proc->yptr->optr = proc->optr;
    }
    else {
       proc->parent->cptr = proc->optr;
    }
    nr_process --;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 YOUR CODE
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */

        // 禁用中断,避免切换进程被打断
        bool intr_flag;
        local_intr_save(intr_flag);
        // 保存当前进程和下一个进程
        struct proc_struct *prev = current, *next = proc;
        // 将当前进程设置为下一个进程
        current = proc;
        // 切换页表,使用新进城的地址空间
        lcr3(next->cr3);
        // 切换进程
        switch_to(&(prev->context), &(next->context));
        // 恢复中断
        local_intr_restore(intr_flag);
    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void) {
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc) {
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to 
//       proc->tf in do_fork-->copy_thread function
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm) {
    struct Page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
// copy_mm - 处理程序 "proc" 根据 clone_flags 复制或共享进程 "current" 的内存管理结构（mm）
//         - 如果 clone_flags & CLONE_VM，则进行 "共享" 操作；否则进行 "复制" 操作
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL) {
        return 0;
    }
    if (clone_flags & CLONE_VM) {
        mm = oldmm;
        goto good_mm;
    }
    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);

    if (ret != 0) {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->cr3 = PADDR(mm->pgdir);
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    //LAB4:EXERCISE2 YOUR CODE
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

    //    1. call alloc_proc to allocate a proc_struct
    //    2. call setup_kstack to allocate a kernel stack for child process
    //    3. call copy_mm to dup OR share mm according clone_flag
    //    4. call copy_thread to setup tf & context in proc_struct
    //    5. insert proc_struct into hash_list && proc_list
    //    6. call wakeup_proc to make the new child process RUNNABLE
    //    7. set ret vaule using child proc's pid

    //LAB5 YOUR CODE : (update LAB4 steps)
    //TIPS: you should modify your written code in lab4(step1 and step5), not add more code.
   /* Some Functions
    *    set_links:  set the relation links of process.  ALSO SEE: remove_links:  lean the relation links of process 
    *    -------------------
    *    update step 1: set child proc's parent to current process, make sure current process's wait_state is 0
    *    update step 5: insert proc_struct into hash_list && proc_list, set the relation links of process
    */

   if ((proc = alloc_proc()) == NULL)
        goto fork_out;
    proc->parent = current;
    assert(current->wait_state == 0); //确保进程在等待
    if (setup_kstack(proc) == -E_NO_MEM)
        goto bad_fork_cleanup_proc;

    if (copy_mm(clone_flags, proc) != 0)
        goto bad_fork_cleanup_kstack;

    //这里的是父进程的stack pointer（上方注释），也就是栈指针，我们大致是可以把它当作esp使的。
    copy_thread(proc, stack, tf);
    bool interrupt_forbidden;
    local_intr_save(interrupt_forbidden);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        //list_add(&proc_list, &proc->list_link);
        //nr_process++;
        set_links(proc); //设置进程链接
    }
    //local_intr_restore就很明了了，如果原来是关着的那么就重新关着，如果本来是开着的那么就打开
    local_intr_restore(interrupt_forbidden);
    wakeup_proc(proc);
    ret = proc->pid;

 
fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process

// do_exit - 被 sys_exit 调用
//   1. 调用 exit_mmap & put_pgdir & mm_destroy 以释放进程的几乎所有内存空间
//   2. 将进程状态设置为 PROC_ZOMBIE，然后调用 wakeup_proc(parent) 要求父进程回收自身
//   3. 调用调度程序以切换到其他进程

int
do_exit(int error_code) {
    // 检查当前进程是否为idleproc或initproc，如果是，发出panic
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }

    // 获取当前进程的内存管理结构mm
    struct mm_struct *mm = current->mm;

    // 如果mm不为空，说明是用户进程
    if (mm != NULL) {
        // 切换到内核页表，确保接下来的操作在内核空间执行
        lcr3(boot_cr3);

        // 如果mm引用计数减到0，说明没有其他进程共享此mm
        if (mm_count_dec(mm) == 0) {
            // 释放用户虚拟内存空间相关的资源
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        // 将当前进程的mm设置为NULL，表示资源已经释放
        current->mm = NULL;
    }

    // 设置进程状态为PROC_ZOMBIE，表示进程等待回收
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;

    bool intr_flag;
    struct proc_struct *proc;

    // 关中断
    local_intr_save(intr_flag);
    {
        // 获取当前进程的父进程
        proc = current->parent;

        // 如果父进程处于等待子进程状态，则唤醒父进程
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }

        // 遍历当前进程的所有子进程
        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;

            // 设置子进程的父进程为initproc，并加入initproc的子进程链表
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;

            // 如果子进程也处于退出状态，唤醒initproc
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    // 开中断
    local_intr_restore(intr_flag);
    // 调用调度器，选择新的进程执行
    schedule();
    // 如果执行到这里，表示代码执行出现错误，发出panic
    panic("do_exit will not return!! %d.\n", current->pid);
}


/* load_icode - 将二进制程序（ELF 格式）的内容加载为当前进程的新内容
 * @binary: 二进制程序内容的内存地址
 * @size: 二进制程序内容的大小
 */
static int
load_icode(unsigned char *binary, size_t size) {
    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;
    // (1) 给当前程序创建一个新的mm
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    //(2) 创建一个新的页目录表（PDT），并将boot_pgdir所指的内容(内核)拷贝到新创建的页目录表中,并将 mm->pgdir 设置为新创建的页目录表的内核虚拟地址
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    //(3) 复制 TEXT/DATA 部分，并将二进制文件中的 BSS 部分构建到进程的内存空间中
    struct Page *page;
    //(3.1) 获取二进制程序（ELF 格式）的文件头
    struct elfhdr *elf = (struct elfhdr *)binary;
    //(3.2) 获取二进制程序（ELF 格式）的程序段头入口
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    //(3.3) 此程序是否有效？
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum;
    for (; ph < ph_end; ph ++) {
    //(3.4) 查找每个程序段头
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            // continue ;
        }
    //(3.5) 调用 mm_map 函数设置新的虚拟内存区域（ph->p_va, ph->p_memsz）
        vm_flags = 0, perm = PTE_U | PTE_V;//PTE_U为用户态
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;//可执行
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;//可写
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;//可读
        // modify the perm bits here for RISC-V
        // 下面是设置risc-v的权限标志位
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {//调用mm_map创建对应大小的vma,并插到mm中
            goto bad_cleanup_mmap;
        }
        unsigned char *from = binary + ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);//准备要分配的地址

        ret = -E_NO_MEM;

        //(3.6) 分配内存，并将每个程序段的内容（从 from 到 from+end）复制到进程的内存中（从 la 到 la+end）
        end = ph->p_va + ph->p_filesz;
    //(3.6.1) 复制二进制程序的 TEXT/DATA 段
        while (start < end) {
            // 分配一个物理页，并将其映射到虚拟地址 la 处
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            // 计算偏移量和复制大小，更新 la 的值
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            // 将数据从二进制程序的 from 复制到新分配的页中
            memcpy(page2kva(page) + off, from, size);
            // 更新 start 和 from 的位置
            start += size, from += size;
        }

    //(3.6.2) 构建二进制程序的 BSS 段
        end = ph->p_va + ph->p_memsz;
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            // 处理 BSS 段的特殊情况，即 p_memsz 等于 p_filesz 的情况
            if (start == end) {
                continue;
            }
            // 计算偏移量和大小，然后使用 memset 将数据初始化为 0
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            // 将数据从二进制程序的 from 复制到新分配的页中
            memset(page2kva(page) + off, 0, size);
            // 更新 start 的位置
            start += size;
            // 确保处理完 BSS 段后，start 和 end 的位置关系符合预期
            assert((end < la && start == end) || (end >= la && start == la));
        }
        // 处理剩余部分，将 BSS 段的数据初始化为 0
        while (start < end) {
            // 分配一个物理页，并将其映射到虚拟地址 la 处
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            // 计算偏移量和大小，然后使用 memset 将数据初始化为 0
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            // 将数据从二进制程序的 from 复制到新分配的页中
            memset(page2kva(page) + off, 0, size);
            // 更新 start 的位置
            start += size;
        }
    }

    // 至此elf的程序码均已经放在了对应的虚拟地址空间中

    //(4) 构建用户栈内存
    vm_flags = VM_READ | VM_WRITE | VM_STACK;// 设置虚拟内存标志为可读、可写、堆栈
    // 将用户栈映射到进程的地址空间
    // 栈顶虚拟地址0x80000000
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    // 先分配几页,上面虚拟地址是开了256个page
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);

    //(5) 设置当前进程的 mm、sr3，并将 CR3 寄存器设置为页目录的物理地址
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);//修改为当前进程的页目录表
    lcr3(PADDR(mm->pgdir));

    //(6) 为用户环境设置trapframe
    struct trapframe *tf = current->tf;
    // Keep sstatus
    uintptr_t sstatus = tf->status;
    memset(tf, 0, sizeof(struct trapframe));//清空进程的trapframe
    /* LAB5:EXERCISE1 YOUR CODE
     * 应该设置 tf->gpr.sp、tf->epc、tf->status
     * 注意：如果我们正确设置 trapframe，那么用户级进程可以从内核返回到用户模式。因此，
     *          tf->gpr.sp 应该是用户栈的顶部（即 sp 的值）
     *          tf->epc 应该是用户程序的入口点（即 sepc 的值）
     *          tf->status 应该适用于用户程序（即 sstatus 的值）
     *          提示：检查 SSTATUS 中 SPP、SPIE 的含义，使用它们通过 SSTATUS_SPP、SSTATUS_SPIE（在 risv.h 中定义）
     */

    tf->gpr.sp = USTACKTOP; // 设置f->gpr.sp为用户栈的顶部地址
    tf->epc = elf->e_entry; // 设置tf->epc为用户程序的入口地址
    tf->status = (read_csr(sstatus) & ~SSTATUS_SPP & ~SSTATUS_SPIE); // 根据需要设置 tf->status 的值，清除 SSTATUS_SPP 和 SSTATUS_SPIE 位

    /*tf->gpr.sp = USTACKTOP;：在用户模式下，栈通常从高地址向低地址增长，而 USTACKTOP 是用户栈的顶部地址，
    因此将 tf->gpr.sp 设置为 USTACKTOP 可以确保用户程序在正确的栈空间中运行。
    tf->epc = elf->e_entry;：elf->e_entry 是可执行文件的入口地址，也就是用户程序的起始地址。
    通过将该地址赋值给 tf->epc，在执行 mret 指令后，处理器将会跳转到用户程序的入口开始执行。
    tf->status = (read_csr(sstatus) & ~SSTATUS_SPP & ~SSTATUS_SPIE);：sstatus 寄存器中的 SPP 位表示当前特权级别，SPIE 位表示之前的特权级别是否启用中断。
    通过清除这两个位，可以确保在切换到用户模式时，特权级别被正确设置为用户模式，并且中断被禁用，以便用户程序可以在预期的环境中执行。
    */

   //mark 这句话还没好好看
   /*
   至此，用户进程的用户环境已经搭建完毕。
   此时initproc将按产生系统调用的函数调用路径原路返回，执行中断返回指令“iret”（位于trapentry.S的最后一句）后，
   将切换到用户进程hello的第一条语句位置_start处（位于user/libs/initcode.S的第三句）开始执行。
   */

    ret = 0;
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.

// do_execve - 调用 exit_mmap(mm) 和 put_pgdir(mm) 以回收当前进程的内存空间
//           - 调用 load_icode 以根据二进制程序设置新的内存空间

// 主要目的在于清理原来进程的内存空间，为新进程执行准备好空间和资源
int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size) {//程序名 名长 程序码地址 程序码长
    struct mm_struct *mm = current->mm;

    // 检查传入的进程名是否合法
    if (!user_mem_check(mm, (uintptr_t)name, len, 0)) {
        return -E_INVAL;
    }
    // 若进程名长度超过最大长度，则截断为最大长度
    if (len > PROC_NAME_LEN) {
        len = PROC_NAME_LEN;
    }

    // 将进程名复制到本地字符数组local_name中
    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    // 首先要为加载执行码清空用户态内存空间
    if (mm != NULL) {// 如果当前进程具有内存管理结构体mm，则进行清理操作
        cputs("mm != NULL");
        lcr3(boot_cr3);//设置页表为内核空间页表,切换到内核态
        if (mm_count_dec(mm) == 0) {//看mm引用计数-1后是否为零,如果为0，则表明没有进程再需要此进程所占用的内存空间
            exit_mmap(mm);//释放mm
            put_pgdir(mm);//释放页表
            mm_destroy(mm);//清空内存
        }
        current->mm = NULL;// 将当前进程的内存管理结构指针设为NULL，表示没有有效的内存管理结构
    }
    int ret;
    /*
    接下来的一步是加载应用程序执行码到当前进程的新创建的用户态虚拟空间中。
    这里涉及到读ELF格式的文件，申请内存空间，建立用户态虚存空间，加载应用程序执行码等。
    load_icode函数完成了整个复杂的工作。
    */
    if ((ret = load_icode(binary, size)) != 0) {
        goto execve_exit;
    }
    // 给新进程设置进程名
    set_proc_name(current, local_name);
    return 0;

execve_exit:
    do_exit(ret);// 执行出错，退出当前进程，并传递错误码ret
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int
do_yield(void) {
    current->need_resched = 1;
    return 0;
}

// do_wait - 等待一个或任何具有 PROC_ZOMBIE 状态的子进程，并释放内核栈的内存空间以及该子进程的 proc 结构。
// 注意：仅在执行完 do_wait 函数后，子进程的所有资源才会被释放。
int
do_wait(int pid, int *code_store) {//code_store是退出码，保存
    struct mm_struct *mm = current->mm;

    //确保code_store指向的内存空间可以访问
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL; 
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;

    // 指定了pid，寻找特定的子进程
    if (pid != 0) {
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current) {
            haskid = 1;//表示存在子进程

            // 如果子进程已经是僵尸进程，跳转到found标签
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    // 如果pid为0，遍历所有子进程
    else {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;

            // 如果子进程已经是僵尸进程，跳转到found标签
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }

    // 如果存在子进程但它们还未退出，则将当前进程状态设置为PROC_SLEEPING
    if (haskid) {//前面没有goto，说明子进程不是僵尸
        current->state = PROC_SLEEPING;// 当前进程状态设置为SLEEP
        current->wait_state = WT_CHILD;// 等待状态设置为WT_CHILD
        schedule();// 调用调度器选择新的进程执行

        // 如果当前进程正在退出，调用do_exit函数
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }

        // 重新尝试等待
        goto repeat;
    }

    // 没有符合条件的子进程
    return -E_BAD_PROC; // 无效的子进程

found:
    // 如果找到符合条件的子进程
    if (proc == idleproc || proc == initproc) {// 要是子进程是idleproc或initproc，就报错
        panic("wait idleproc or initproc.\n");
    }

    // 如果指定了存储退出码的内存地址，则将退出码写入该地址
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }

    local_intr_save(intr_flag);
    {
        unhash_proc(proc); // 从进程哈希表中移除
        remove_links(proc); // 移除与其他进程的链接
    }
    local_intr_restore(intr_flag);

    put_kstack(proc); // 释放内核栈
    kfree(proc); // 释放进程结构体内存

    return 0; // 成功返回
}


// do_kill - kill process with pid by set this process's flags with PF_EXITING
int
do_kill(int pid) {
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL) {
        if (!(proc->flags & PF_EXITING)) {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED) {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}


// kernel_execve - 执行 SYS_exec 系统调用以执行由 user_main 内核线程调用的用户程序
static int
kernel_execve(const char *name, unsigned char *binary, size_t size) {
    int64_t ret=0, len = strlen(name);

    // mark 调用过程,最终调用do_execve来完成用户进程的创建
    /*
    vector128(vectors.S)--\>
    \_\_alltraps(trapentry.S)--\>trap(trap.c)--\>trap\_dispatch(trap.c)--
    --\>syscall(syscall.c)--\>sys\_exec（syscall.c）--\>do\_execve(proc.c)
    */
    asm volatile(
        "li a0, %1\n"//SYS_exec,宏定义为4,li的i是立即数
        "lw a1, %2\n"//一直到a4就是后面那四个参数,name表示程序名称(是个指针)
        "lw a2, %3\n"//程序名称的长度
        "lw a3, %4\n"//程序二进制代码的起始地址
        "lw a4, %5\n"//程序二进制代码的长度
    	"li a7, 10\n"//系统调用号,10是execve
        "ebreak\n"//触发一个中断
        "sw a0, %0\n"//将返回值写入ret
        : "=m"(ret)
        : "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size)
        : "memory");//这句memory表示汇编代码可能会影响内存的内容，因此编译器应该在执行这段代码之前刷新内存
    cprintf("ret = %d\n", ret);
    return ret;
}

#define __KERNEL_EXECVE(name, binary, size) ({                          \
            cprintf("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_obj___user_##x##_out_start[],  \
                _binary_obj___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,     \
                            _binary_obj___user_##x##_out_size);         \
        })

#define __KERNEL_EXECVE2(x, xstart, xsize) ({                           \
            extern unsigned char xstart[], xsize[];                     \
            __KERNEL_EXECVE(#x, xstart, (size_t)xsize);                 \
        })

#define KERNEL_EXECVE2(x, xstart, xsize)        __KERNEL_EXECVE2(x, xstart, xsize)

// user-main: 执行用户程序的内核线程
static int
user_main(void *arg) {
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg) {
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }

    while (do_wait(0, NULL) == 0) {
        schedule();
    }

    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));

    cprintf("init check memory pass.\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
void
proc_init(void) {
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

