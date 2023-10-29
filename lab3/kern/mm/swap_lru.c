#include "defs.h"
#include "swap_lru.h"
#include "list.h"
#include "kdebug.h"
#include "pmm.h"
#include "vmm.h"
#include <stdio.h>

struct pra_list_manager{
    list_entry_t free_list;
    int32_t free_count;
    list_entry_t busy_list;
    uint32_t busy_count;
}_pra_list_manager;

#define LEN 10

static void
init_pra_list_manager(struct pra_list_manager *p)
{
     p->busy_count = p->free_count = 0;
     list_init(&p->free_list);
     list_init(&p->busy_list);
}



static int
_lru_init_mm(struct mm_struct *mm)
{     
    init_pra_list_manager(&_pra_list_manager);
     mm->sm_priv = &_pra_list_manager;

     return 0;
}

// _lru_swap_cleanup函数用于清理过期页面，返回最早的未被访问的页面
static list_entry_t*
_lru_swap_cleanup(struct mm_struct *mm, int32_t cleanup_len)
{

    // 初始化值
    struct pra_list_manager* p = (struct pra_list_manager*)mm->sm_priv;
    list_entry_t *free_head = &p->free_list;
    list_entry_t *busy_head = &p->busy_list;

    // 计算需要清理的页面数量
    int32_t free_i = cleanup_len < p->free_count ? cleanup_len : p->free_count;
    int32_t busy_i = cleanup_len < p->busy_count ? cleanup_len : p->busy_count;

    list_entry_t *found = NULL;
    int32_t i,delt;

    // 清理busy_list中的过期页面

    list_entry_t *le = busy_head->next;
    for(i = 0, delt = 0 ; i < busy_i ; i ++,le = le->next)
    {
        // 获取页面
        struct Page *page = le2page(le, pra_page_link);


        if(page->visited == 0)
        {
            // 如果页面未被访问过，则将其从busy_list中删除，添加到free_list中
            list_del(le);
            list_add_before(free_head, le);
            delt ++;
        }
    }
    p->free_count += delt;
    p->busy_count -= delt;

    // 清理free_list中的过期页面
    le = free_head->next;
    for(i = 0, delt = 0 ; i < free_i ; i ++,le = le->next)
    {
        // 获取页面
        struct Page *page = le2page(le, pra_page_link);

        // 获取页面对应的页表项
        if(page->visited == 1)
        {
            // 如果页面被访问过，则将其从free_list中删除，添加到busy_list中
            list_del(le);
            list_add_before(busy_head, le);
            delt ++;
        }else{
            // 如果页面未被访问过，则将其作为最早未被访问的页面返回
            if(found == NULL)
                found = le; 
        }
    }
    p->free_count -= delt;
    p->busy_count += delt;

    return found;
    
}


static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    // 获取进程的页面替换算法管理器
    struct pra_list_manager* p = (struct pra_list_manager*)mm->sm_priv;
    // 获取链表头
    list_entry_t *free_head = &p->free_list;
    list_entry_t *busy_head = &p->busy_list;
    // 获取当前页面的链表节点
    list_entry_t *entry = &(page->pra_page_link);
    // 清理过期页面
    _lru_swap_cleanup(mm, LEN);
    // 将当前页面添加到链表头之前
    page->visited = 0;
    list_add_before(free_head, entry);
    // 页面计数加1
    p->free_count ++;
    return 0;
}


static int
_lru_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    struct pra_list_manager* p = (struct pra_list_manager*)mm->sm_priv;
    list_entry_t *free_head = &p->free_list;
    list_entry_t *busy_head = &p->busy_list;
    list_entry_t *found;

    assert(in_tick == 0);
    assert( free_head != NULL && busy_head != NULL);

    found = _lru_swap_cleanup(mm, LEN);

    //point the first one
    if(found == NULL) 
    {
        if(p->free_count != 0)
        {
            found = free_head->next;
            p->free_count --;
        }
        else{
           if(p->busy_count == 0) return false;
           found = busy_head->next; 
           p->busy_count --;
        }
    }else{
        p->free_count --;
    }
    
    list_del(found); 
    *ptr_page = le2page(found, pra_page_link);


    return 0;
}

static int
_lru_check_swap(void) {
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==7);
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==8);
    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==9);
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==10);
    cprintf("write Virt Page a in lru_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==11);
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


