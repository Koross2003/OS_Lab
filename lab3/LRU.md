### LRU
***

#### 基本思路

1. page结构体中增加一个变量`last_visited_time`，用于记录最近一次访问时间
2. 添加一个global变量`time_now`，模拟当前时间
3. 每次访问某个页时，`last_visited_time = ++time_now`
4. 每次选取替换页时，遍历list，选取`last_visited_time`最小的页

***

#### 仍然存在的问题

这个lru只能作为一个玩具，因为我没想到怎么实现在访问一个地址时自动更新对应物理page的last_visited_time。

**作废的方案**
一开始我以为每次访问都会调用find_vma，所以我当时选择在vma上添加last_visited_time，每次选择替换页时遍历每个物理页对应的vma，将最近的vma->last_visited_time作为page的last_visited_time。
但是后来通过调试发现，除了前期check的部分，本次实验的中只有发生page_fault时调用do_page_fault函数，才会调用find_vma，因此这个方案作废了

**新的方案**
所以我只能手动模拟这个更新过程，在check_lru中，每访问一个地址后要手动调用reset_time(uint_t addresss)，这个函数接收一个虚拟地址，通过查找页表找到页表项进而找到对应的Page，然后更新Page的last_visited_time。

***

#### 具体的实现过程

lru的init和map_swappable函数与fifo相同，不再赘述

**lru_swap_out_victim()**

```c
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

    //遍历list，选取last_visited_time最小的页
    while((curr_ptr = list_next(curr_ptr)) != head) {

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
```


**void reset_time()**

```c
void reset_time(uintptr_t addr){
    
    addr = ROUNDDOWN(addr, PGSIZE); 
    pte_t* temp_ptep = NULL; 
    temp_ptep = get_pte(boot_pgdir, addr, 1); // 获取页表项
    struct Page* page = pte2page(*temp_ptep); // 找到对应物理页
    page -> last_visited_time = ++time_now; // 更新last_visited_time
}
```

**check_swap()**

```c
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
    *(unsigned char *)0x3000 = 0x0c;
    reset_time((unsigned char*)0x3000);
    assert(pgfault_num==6);

    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
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
```