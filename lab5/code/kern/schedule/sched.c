#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

// 调度函数
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;
    // 关闭中断
    local_intr_save(intr_flag);
    {
        // 当前进程不需要重新调度,所以当前进程的need_resched设置为0
        current->need_resched = 0;
        // 如果当前进程是idle，则从进程列表头开始遍历
        // 否则从当前进程的下一个进程开始遍历
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        // 遍历进程列表，找到下一个处于就绪态的进程
        do {
            if ((le = list_next(le)) != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
            }
        } while (le != last);
        // 如果没有找到就绪进程，则运行idle
        if (next == NULL || next->state != PROC_RUNNABLE) {
            next = idleproc;
        }
        // 更新下一个进程的运行次数
        next->runs ++;
        // 如果下一个进程不是当前进程，则运行下一个进程
        if (next != current) {
            proc_run(next);
        }
    }
    // 恢复中断
    local_intr_restore(intr_flag);
}

