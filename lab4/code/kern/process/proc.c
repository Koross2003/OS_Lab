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


// mark hash_list和proc_list的区别
/*
hash_list是我们用的list,pid计算出一个hash值,然后每个hash值有一个list_entry_t,连接这个hash值的所有进程
proc_list实际上没啥用,他唯一的作用就是get_pid中.创建一个进程的时候进程加到这个list中,所以他是按照pid的顺序连接的,因此可以按照顺序循环分配pid
*/

// mark context和trapframe
/*
实际上这个实验中,context没啥用
创建(复制)一个进程,他的上下文实际上是这样操作的
1. context中的ra指向forkret,然后switch_to会把原来的context保存到源进程的context中,加载新进程的context
2. switch_to结束后返回,返回地址是ra指向的forkret,这里问题就来了,实际上forkret不需要参数,他做的就是调用forkrets,传入当前进程的trapframe
3. forkrets会把参数a0(也就是当前进程的trapframe)设置为sp,然后调用trap_ret
4. trap_ret把sp中的寄存器恢复到寄存器中(因此之前context恢复的寄存器实际上根本没有用)

所以结论就是:context唯一作用就是帮忙调用了forkret这个函数
我猜测这可能是历史遗留问题
*/

// mark 如何禁用和启用中断
/*
sstatus 寄存器中的 SIE 位用于控制是否允许中断发生。当 SIE 位为 1 时，允许中断发生；当 SIE 位为 0 时，禁用中断。

### 禁用中断
禁用中断经历了以下几个步骤：

1. 检查是否禁止了中断，如果没禁止则禁止中断并返回1，如果已经禁止了则返回0
2. 如果需要禁用中断，调用`intr_disable`函数，将sstatus 寄存器的`SSTATUS_SIE`位置0，禁用中断

### 启用中断
启用中断经历了以下几个步骤：
1. 根据flag保存的状态，判断是否需要启用中断。如果flag为0，说明原本就是禁用的，恢复之后也应该是禁用，所以不需要启用；如果flag为1，说明原本是启用的，则需要启用中断。
2. 如果需要启用中断，调用`intr_enable`函数，将sstatus 寄存器的`SSTATUS_SIE`位置1，启用中断。

*/

// 所有进程控制块的哈希表，proc_struct中的成员变量hash_link将基于pid链接入这个哈希表中
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

// alloc_proc - 分配一个 proc_struct 并初始化 proc_struct 的所有字段
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        // your code
        // proc的变量初始化
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

// get_pid - 为进程分配唯一的pid
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) { // 如果last_pid超过了最大值，就从1开始重新分配
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) { // 如果last_pid超过了next_safe，就重新计算next_safe
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) { // 遍历进程链表
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) { // 如果找到了相同的pid，就将last_pid加1
                if (++ last_pid >= next_safe) { // 如果last_pid超过了next_safe，就重新计算next_safe
                    if (last_pid >= MAX_PID) { // 如果last_pid超过了最大值，就从1开始重新分配
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) { // 如果找到了比last_pid大的pid，就将next_safe设置为该pid
                next_safe = proc->pid;
            }
        }
    }
    return last_pid; // 返回分配的pid
}

// proc_run - 使进程“proc”在CPU上运行
// 注意：在调用switch_to之前，应该加载“proc”的新PDT的基地址
void
proc_run(struct proc_struct *proc) {
    // 如果要切换的进程和当前进程相同则不需要切换
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

// forkret - 新线程/进程的第一个内核入口点
// 注意：forkret 的地址在 copy_thread 函数中设置
// 在 switch_to 之后，当前进程将在此处执行。
static void
forkret(void) {
    // tag 这段代码实际上非常重要
    // 这里把trapframe作为参数传递给了forkret函数
    // forkret会把该参数(trapframe)放入a0,然后跳到trapret,trapret会从该中断帧中恢复所有寄存器
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
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

// kernel_thread - 创建一个内核线程
// 注意：temp trapframe tf 的内容将在 do_fork --> copy_thread 函数中被复制到 proc->tf 中
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf; // 上下文
    memset(&tf, 0, sizeof(struct trapframe));

    // 设置函数和参数
    tf.gpr.s0 = (uintptr_t)fn; // s0寄存器存储函数地址(函数指针)
    tf.gpr.s1 = (uintptr_t)arg;// s1寄存器存储函数参数

    // 设置 trapframe 中的 status 寄存器（SSTATUS）
    // SSTATUS_SPP：Supervisor Previous Privilege（设置为 supervisor 模式，因为这是一个内核线程）
    // SSTATUS_SPIE：Supervisor Previous Interrupt Enable（设置为启用中断，因为这是一个内核线程）
    // SSTATUS_SIE：Supervisor Interrupt Enable（设置为禁用中断，因为我们不希望该线程被中断）
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    // (这句是指导书上的)将入口点（epc）设置为 kernel_thread_entry 函数，作用实际上是将pc指针指向它(entry.S会用到)
    // 进程的入口就是kernel_thread_entry,然后由kernel_thread_entry去让进程执行特定的函数(在entry.S中)
    tf.epc = (uintptr_t)kernel_thread_entry;

    // 使用 do_fork 创建一个新进程（内核线程），这样才真正用设置的tf创建新进程。
    // mark 注意这里的参数,0的位置传入的应该是esp,由于我们本次实验中创建的都是内核线程,所以不需要
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - 为进程内核栈分配大小为 KSTACKPAGE 的页面
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - 释放进程内核栈所占空间
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    assert(current->mm == NULL);
    /* do nothing in this project */
    return 0;
}

// 复制一个进程,我们实际上就是复制stack和trapframe
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    // kstack是栈顶,+KSTACKSIZE是栈底
    // 这里是把trapframe压入栈底(复制一个trapframe到栈底)
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0; // a0寄存器存储返回值,这里设置为0,表示子进程
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp; // 设置栈顶,如果esp=0说明是内核线程,栈顶设置为tf即可;否则要复制父进程的栈顶

    proc->context.ra = (uintptr_t)forkret; // 在switch之后会通过ra进行返回,所以switch_to之后会返回到forkret函数
    // mark 实际上我觉得下面这句没有用,原因看我上面分析的context与trapframe
    proc->context.sp = (uintptr_t)(proc->tf); // trapframe放在上下文栈顶
}


/* do_fork - 创建新子进程的父进程
@clone_flags: 用于指导如何克隆子进程
@stack: 父进程的用户栈指针。如果stack==0，表示要创建一个内核线程。
@tf: trapframe，将被复制到子进程的proc->tf中
*/
// 我们实际需要"fork"的东西就是stack和trapframe
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

    // 调用alloc_proc分配一个proc_struct
    if ((proc = alloc_proc()) == NULL)
        goto fork_out;
    // 调用setup_kstack为子进程分配一个内核栈
    if (setup_kstack(proc) == -E_NO_MEM)
        goto bad_fork_cleanup_proc;
    // 调用copy_mm根据clone_flag复制内存管理信息,但本次实验中只涉及到内核线程,所以不需要复制(copy_mm也没有实现)
    if (copy_mm(clone_flags, proc) != 0)
        goto bad_fork_cleanup_kstack;
    //设置父进程
    proc->parent = current;
    // 调用copy_thread复制trapframe和stack
    copy_thread(proc, stack, tf);

    // 至此,子进程已经创建完毕,接下来就是将子进程加入到进程列表中

    // 禁用中断,防止插入进程列表时被打断
    bool intr_flag;
    local_intr_save(intr_flag);

    proc->pid = get_pid();
    hash_proc(proc);
    list_add(&proc_list, &proc->list_link);
    nr_process++;

    // 恢复中断
    local_intr_restore(intr_flag);

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
int
do_exit(int error_code) {
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg) {
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

// 创建第一个内核线程idle，创建第二个内核线程init_main
// idle就是空闲进程,课上讲的那个cpu空闲时运行的进程
void
proc_init(void) {
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    // 获取一个进程块
    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    // check the proc structure
    int *context_mem = (int*) kmalloc(sizeof(struct context));
    memset(context_mem, 0, sizeof(struct context));
    int context_init_flag = memcmp(&(idleproc->context), context_mem, sizeof(struct context));

    int *proc_name_mem = (int*) kmalloc(PROC_NAME_LEN);
    memset(proc_name_mem, 0, PROC_NAME_LEN);
    int proc_name_flag = memcmp(&(idleproc->name), proc_name_mem, PROC_NAME_LEN);


    // 对新创建的proc进行检查
    if(idleproc->cr3 == boot_cr3 && idleproc->tf == NULL && !context_init_flag
        && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0
        && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL
        && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag
    ){
        cprintf("alloc_proc() correct!\n");

    }
    
    // 对idle进行初始化
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack; // 设置栈起始地址,我们本次实验所有都是内核线程(或者叫进程),所以他的栈就是内核栈bootstack
    idleproc->need_resched = 1; // 设置为1表示需要调度,即当前线程不想执行了,想让别的线程执行
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc; // cur_proc指向idle之后,cup_idle就会识别到need_resched,然后就会调用shcedule()函数,有其他线程向上位,idle就退休了

    int pid = kernel_thread(init_main, "Hello world!!", 0); // 创建一个init_main线程(执行init_main函数)
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
// 一直去调度,如果发现当前进程需要被重新调度(need_ReSched),则调用schedule()函数
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

