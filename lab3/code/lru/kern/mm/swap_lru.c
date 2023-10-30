#include "defs.h"
#include "swap_lru.h"
#include "list.h"
#include "kdebug.h"
#include "pmm.h"
#include "vmm.h"
#include <stdio.h>

list_entry_t pra_list_head, *curr_ptr;;


static int
_lru_init_mm(struct mm_struct *mm)
{     
    list_init(&pra_list_head);
    mm->sm_priv = &pra_list_head;
    return 0;
}


static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && head != NULL);
    //record the page access situlation

    //(1)link the most recent arrival page at the back of the pra_list_head qeueue.
    list_add(head, entry); // 插到head的后面
    return 0;
}


static int
_lru_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    assert(head != NULL);
    assert(in_tick==0);



    struct Page *ptr_victim = NULL;
    list_entry_t *list_ptr_victim = NULL;
    size_t temp_time = time_now;

    curr_ptr = head;
    //cprintf("\n\n\n woccccccc \n\n\n");
    while((curr_ptr = list_next(curr_ptr)) != head) {

        //cprintf("\n\n\n %d\n\n\n", temp_time);

        struct Page *ptr = le2page(curr_ptr, pra_page_link);
        if(ptr->last_visited_time <= temp_time){
            temp_time = ptr->last_visited_time;
            ptr_victim = ptr;
            list_ptr_victim = curr_ptr;
        }
    }
    *ptr_page = ptr_victim;
    list_del(list_ptr_victim);
    return 0;
}

static int
_lru_check_swap(void) {
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    reset_time((unsigned char*)0x3000);
    assert(pgfault_num==4);

    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    reset_time((unsigned char*)0x1000);
    assert(pgfault_num==4);

    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    reset_time((unsigned char*)0x4000);
    assert(pgfault_num==4);

    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    reset_time((unsigned char*)0x2000);
    assert(pgfault_num==4);

    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    reset_time((unsigned char*)0x5000);
    assert(pgfault_num==5);

    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    reset_time((unsigned char*)0x2000);
    assert(pgfault_num==5);

    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    reset_time((unsigned char*)0x1000);
    assert(pgfault_num==5);

    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    reset_time((unsigned char*)0x2000);
    assert(pgfault_num==5);

    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c; // 这里lru已经把d踢出去了,如果是fifo就是踢a
    reset_time((unsigned char*)0x3000);
    assert(pgfault_num==6);

    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;// 访问d没命中
    reset_time((unsigned char*)0x4000);
    assert(pgfault_num==7);

    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    reset_time((unsigned char*)0x5000);
    assert(pgfault_num==8);

    cprintf("write Virt Page a in lru_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    reset_time((unsigned char*)0x1000);
    assert(pgfault_num==9);
    return 0;
}

static int
_lru_init(void)
{
    return 0;
}

static int
_lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_lru_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_lru =
{
     .name            = "lru swap manager",
     .init            = &_lru_init,
     .init_mm         = &_lru_init_mm,
     .tick_event      = &_lru_tick_event,
     .map_swappable   = &_lru_map_swappable,
     .set_unswappable = &_lru_set_unswappable,
     .swap_out_victim = &_lru_swap_out_victim,
     .check_swap      = &_lru_check_swap,
};


