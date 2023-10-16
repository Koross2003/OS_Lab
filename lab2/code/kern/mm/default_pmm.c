#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>

/* In the first fit algorithm, the allocator keeps a list of free blocks (known as the free list) and,
   on receiving a request for memory, scans along the list for the first block that is large enough to
   satisfy the request. If the chosen block is significantly larger than that requested, then it is 
   usually split, and the remainder added to the list as another free block.
   Please see Page 196~198, Section 8.2 of Yan Wei Min's chinese book "Data Structure -- C programming language"
*/
// you should rewrite functions: default_init,default_init_memmap,default_alloc_pages, default_free_pages.
/*
 * Details of FFMA
 * (1) Prepare: In order to implement the First-Fit Mem Alloc (FFMA), we should manage the free mem block use some list.
 *              The struct free_area_t is used for the management of free mem blocks. At first you should
 *              be familiar to the struct list in list.h. struct list is a simple doubly linked list implementation.
 *              You should know howto USE: list_init, list_add(list_add_after), list_add_before, list_del, list_next, list_prev
 *              Another tricky method is to transform a general list struct to a special struct (such as struct page):
 *              you can find some MACRO: le2page (in memlayout.h), (in future labs: le2vma (in vmm.h), le2proc (in proc.h),etc.)
 * (2) default_init: you can reuse the  demo default_init fun to init the free_list and set nr_free to 0.
 *              free_list is used to record the free mem blocks. nr_free is the total number for free mem blocks.
 * (3) default_init_memmap:  CALL GRAPH: kern_init --> pmm_init-->page_init-->init_memmap--> pmm_manager->init_memmap
 *              This fun is used to init a free block (with parameter: addr_base, page_number).
 *              First you should init each page (in memlayout.h) in this free block, include:
 *                  p->flags should be set bit PG_property (means this page is valid. In pmm_init fun (in pmm.c),
 *                  the bit PG_reserved is setted in p->flags)
 *                  if this page  is free and is not the first page of free block, p->property should be set to 0.
 *                  if this page  is free and is the first page of free block, p->property should be set to total num of block.
 *                  p->ref should be 0, because now p is free and no reference.
 *                  We can use p->page_link to link this page to free_list, (such as: list_add_before(&free_list, &(p->page_link)); )
 *              Finally, we should sum the number of free mem block: nr_free+=n
 * (4) default_alloc_pages: search find a first free block (block size >=n) in free list and reszie the free block, return the addr
 *              of malloced block.
 *              (4.1) So you should search freelist like this:
 *                       list_entry_t le = &free_list;
 *                       while((le=list_next(le)) != &free_list) {
 *                       ....
 *                 (4.1.1) In while loop, get the struct page and check the p->property (record the num of free block) >=n?
 *                       struct Page *p = le2page(le, page_link);
 *                       if(p->property >= n){ ...
 *                 (4.1.2) If we find this p, then it' means we find a free block(block size >=n), and the first n pages can be malloced.
 *                     Some flag bits of this page should be setted: PG_reserved =1, PG_property =0
 *                     unlink the pages from free_list
 *                     (4.1.2.1) If (p->property >n), we should re-caluclate number of the the rest of this free block,
 *                           (such as: le2page(le,page_link))->property = p->property - n;)
 *                 (4.1.3)  re-caluclate nr_free (number of the the rest of all free block)
 *                 (4.1.4)  return p
 *               (4.2) If we can not find a free block (block size >=n), then return NULL
 * (5) default_free_pages: relink the pages into  free list, maybe merge small free blocks into big free blocks.
 *               (5.1) according the base addr of withdrawed blocks, search free list, find the correct position
 *                     (from low to high addr), and insert the pages. (may use list_next, le2page, list_add_before)
 *               (5.2) reset the fields of pages, such as p->ref, p->flags (PageProperty)
 *               (5.3) try to merge low addr or high addr blocks. Notice: should change some pages's p->property correctly.
 */

// 双向链表
free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

//初始化空闲链表
static void default_init(void) {
    list_init(&free_list); //链表初始化,将头节点的前向后向指针都指向自己
    nr_free = 0; //空闲页总数
}


//---------------存疑-----------
// 初始化每一个空闲页,计算空闲页总数
// 初始化每个物理页面记录,然后将全部的可分配物理页视为一大块空闲块加入空闲表
// 初始化n个空闲页块
//-------------------------------

// 初始化链表中的空闲页块
// 从base开始连续分配n个页
static void default_init_memmap(struct Page *base, size_t n) {
    //检查n是否大于0
    assert(n > 0);
    struct Page *p = base;
    // 要分配的页置位
    for (; p != base + n; p ++) {
        assert(PageReserved(p));//确认本页是否为保留页
        //设置标志位
        p->flags = p->property = 0;
        //清空映射
        set_page_ref(p, 0);
    }
    // 第一个页设置,该页块有n个页
    base->property = n;
    //base可分配
    SetPageProperty(base);
    // 总的空闲页+n
    nr_free += n;
    //将base插入双向链表
    if (list_empty(&free_list)) {
        //链表为空直接插入
        list_add(&free_list, &(base->page_link));
    } else {
        //le从链表头开始
        list_entry_t* le = &free_list;
        //遍历链表(le = list_next(le),直到尾部(le的next指向链表头)
        while ((le = list_next(le)) != &free_list) {
            //每次把le转换为Page指针,这一步会减去page_link在Page中的偏移量,取到Page的地址
            struct Page* page = le2page(le, page_link);
            //按照地址排序,低地址在前面
            if (base < page) {
                //page更大,则将base插在前面
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                //如果没有到最后,继续迭代
                //到最后了,说明base比所有都大,加到最后
                list_add(le, &(base->page_link));
            }
        }
    }
}

//遍历链表,找到第一块大于n的块,分配出来并从链表中除去,剩余部分重新加入链表
//重新插入时,会插入到原位置,因为是按照地址递增排序的链表
static struct Page * default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        //减去page_link偏移,取到page地址
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        //多余的插回去
        if (page->property > n) {
            //找到新页块的起始位置
            struct Page *p = page + n;
            //新页块中页的数量
            p->property = page->property - n;
            //可分配
            SetPageProperty(p);
            //加到page前一个的后面
            list_add(prev, &(p->page_link));
        }
        //更新链表中页块数量
        nr_free -= n;
        //page设置为已分配
        ClearPageProperty(page);
    }
    return page;
}

// 释放页面base
// 将释放页面标记为可分配
// 找到合适位置,并且如果和前驱或者后继节点空间上邻接,要合并

/* 合并流程
1.寻找插入位置(插入到地址 > base的空闲块链表节点前)
		
2.进行地址比对，已确定插入位置及处理方式
分析: 循环结束情况及处理方式分为如下3种
	(1).空闲链表为空(只有头结点)，直接添加到头结点后面就可以
	(2).查到链表尾均未发现比即将插入到空闲连表地址大的空闲块。
		a.先插入到链表尾部
		b.尝试与前一个节点进行合并
	(3).找到比需要插入空闲块地址大的空闲块节点而跳出循环
		a.先插入到找到的节点前面
		b.尝试与后一个节点进行合并
		c.如果前一个节点不为头结点，则尝试与前一个节点进行合并
3.更新空闲链表可用空闲块数量
	nr_free += n;
*/
static void default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        //既不是被保留的,也已经被分配了
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0; //修改标志位
        set_page_ref(p, 0);//引用数置0
    }
    base->property = n; //设置连续大小为n
    SetPageProperty(base);//可分配
    nr_free += n;

    //执行插入操作,这部分和初始化空闲页方式一致
    if (list_empty(&free_list)) {
        //如果链表为空,直接加在链表头
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        //遍历链表
        while ((le = list_next(le)) != &free_list) {
            //取page
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                //找到比base大的page,插到page前面
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                //如果没找到,插到最后
                list_add(le, &(base->page_link));
            }
        }
    }

    //前驱合并
    //取base的前驱节点
    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        //前驱节点的page
        p = le2page(le, page_link);
        //如果前驱节点的尾部的下一个是base的话则合并
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);//分配置0
            list_del(&(base->page_link));//从链表中删除
            base = p;//base变为前驱节点
        }
    }

    //后继合并,和前驱类似
    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

static size_t default_nr_free_pages(void) {
    return nr_free;
}

static void basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
default_check(void) {
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count ++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 2) && p0[2].property == 3);
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL);
    assert(p0 + 2 == p1);

    p2 = p0 + 1;
    free_page(p0);
    free_pages(p1, 3);
    assert(PageProperty(p0) && p0->property == 1);
    assert(PageProperty(p1) && p1->property == 3);

    assert((p0 = alloc_page()) == p2 - 1);
    free_page(p0);
    assert((p0 = alloc_pages(2)) == p2 + 1);

    free_pages(p0, 2);
    free_page(p2);

    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        count --, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
}
//这个结构体在
const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = default_init,
    .init_memmap = default_init_memmap,
    .alloc_pages = default_alloc_pages,
    .free_pages = default_free_pages,
    .nr_free_pages = default_nr_free_pages,
    .check = default_check,
};

