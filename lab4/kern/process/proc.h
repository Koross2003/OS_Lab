#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>


// process's state in his life cycle
enum proc_state {
    PROC_UNINIT = 0,// 未初始化(刚出生)
    PROC_SLEEPING,// 睡眠
    PROC_RUNNABLE, // 可运行(也许正在运行)
    PROC_ZOMBIE, // 僵尸,等待父进程回收
};

// 进程上下文
// 因为本程序中线程切换是写在一个函数中,所以编译器会帮我们写好保存"调用者保存寄存器"的代码,我们保存的上下文只需要包括"被调用者保存寄存器"即可
struct context {
    uintptr_t ra; // 返回地址
    uintptr_t sp; // 栈顶
    uintptr_t s0; // 临时变量寄存器
    uintptr_t s1;
    uintptr_t s2;
    uintptr_t s3;
    uintptr_t s4;
    uintptr_t s5;
    uintptr_t s6;
    uintptr_t s7;
    uintptr_t s8;
    uintptr_t s9;
    uintptr_t s10;
    uintptr_t s11;
};

#define PROC_NAME_LEN               15
#define MAX_PROCESS                 4096
#define MAX_PID                     (MAX_PROCESS * 2)

// 进程链表
extern list_entry_t proc_list;

struct proc_struct {
    enum proc_state state; // 进程状态,有四种状态：PROC_UNINIT, PROC_SLEEPING, PROC_RUNNABLE, PROC_ZOMBIE
    int pid; // 进程 ID
    int runs; // 进程运行次数
    uintptr_t kstack;// 进程内核栈的栈顶地址
    /*每个进程都有2页的栈,都存在内核的保留空间内
    优点1. 进程(线程)切换时,可以根据kstack正确设置好tss(但是riscv没有这个东西,所以我感觉这个优点应该也不存在) 2. 进程结束时,不用通过mm,直接用kstack就释放了*/
    
    volatile bool need_resched; // bool 值：是否需要重新调度以释放CPU 
    struct proc_struct *parent; // 父进程,只有idle没有爹
    struct mm_struct *mm; // 进程的内存管理,参考之前实验
    struct context context; // 上下文,具体在switch.S中切换
    struct trapframe *tf;  // 保存了进程的中断帧。
                           //当进程从用户空间跳进内核空间的时候，进程的执行状态被保存在了中断帧中（注意这里需要保存的执行状态数量不同于上下文切换）。
                           //系统调用可能会改变用户寄存器的值，我们可以通过调整中断帧来使得系统调用返回特定的值。
    uintptr_t cr3;                              // 页目录表（PDT）的基地址(x86的历史遗留)
    uint32_t flags;                             // 进程标志
    char name[PROC_NAME_LEN + 1];               // 进程名称
    list_entry_t list_link;                     // 进程链接列表
    list_entry_t hash_link;                     // 进程哈希列表
};

#define le2proc(le, member)         \
    to_struct((le), struct proc_struct, member)

// current: 当前进程
// initproc: 理论上应该指向第一个用户态,但是本次实验中指向idle进程
extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);

#endif /* !__KERN_PROCESS_PROC_H__ */

