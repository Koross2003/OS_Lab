
# lab1的entry
# #include <mmu.h>
# #include <memlayout.h>
#     
#     # 声明代码段
#     .section .text,"ax",%progbits
#     # 声明全局标签
#     .globl kern_entry
# kern_entry:
#     # bootstacktop是内核栈的栈顶,这里做的就是把原来在opensbi中的sp指向ucore的栈顶
#     la sp, bootstacktop
#     # 调用kern_init函数执行初始化
#     tail kern_init

# .section .data
#     # .align 2^12
#     # 按页对齐(PGSHIFT的值是12)
#     .align PGSHIFT
#     .global bootstack
# bootstack:
#     # kstacksize是内核栈大小(多少字节)
#     .space KSTACKSIZE
#     .global bootstacktop #栈顶后声明,是高地址
# bootstacktop:

# 在lab1中我们的链接脚本中写的内核起始地址是0x8020,和实际的地址是一致的,所以我们在entey不需要进行地址映射,直接分配栈空间然后跳转到kern_init就好
# 但是在lab2中我们的内核起始地址规定为一个虚拟地址,内核的各个部分都会以这个起始地址开始去排布,且此时内核处于S mode,寄存器satp的MODE被设置为Bare,
# 会把所有的地址作为物理地址,由于我们规定的起始地址很大,所以肯定会超界,所以这一次entry要进行映射,即让(地址-偏移量),恢复为物理地址
#include <mmu.h>
#include <memlayout.h>
    # 声明代码段
    .section .text,"ax",%progbits
    # 声明entry为全局标签
    .globl kern_entry
kern_entry:
    # t0 := 三级页表的虚拟地址
    # boot_page_table_sv39为三级页表,由于我们之后只需要他的物理页号,所以只取他高20位就行,低12位可以不取
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12

    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39(值为1000),这之后再访问内存,cpu就会去查页表而不是直接使用物理地址了
    li      t1, 8 << 60
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
    # 将算出的 t0(即新的MODE|页表基址物理页号) 覆盖到 satp 中
    csrw    satp, t0
    # 使用 sfence.vma 指令刷新 TLB
    # 刷新TLB的原因: 由于我们更新了页表,所以TLB中存储的虚拟物理地址映射肯定不对了,所以要重置刷新
    sfence.vma
    # 从此，我们给内核搭建出了一个完美的虚拟内存空间！
    #nop # 可能映射的位置有些bug。。插入一个nop
    
    # 我们在虚拟内存空间中：随意将 sp 设置为虚拟地址！
    lui sp, %hi(bootstacktop)

    # 我们在虚拟内存空间中：随意跳转到虚拟地址！
    # 跳转到 kern_init
    lui t0, %hi(kern_init)
    addi t0, t0, %lo(kern_init)
    jr t0

# 初始化栈的操作和第一次实验一致
.section .data
    # .align 2^12
    # 12位对齐,我们分配内核栈也是按页分配的
    .align PGSHIFT
    # 栈底
    .global bootstack
bootstack:
    # 开辟栈空间,开辟出2页的空间
    .space KSTACKSIZE
    # 栈顶,后声明,在高地址
    .global bootstacktop
bootstacktop:

.section .data
    # 由于我们要把这个页表放到一个页里面，因此必须 12 位对齐
    .align PGSHIFT
    .global boot_page_table_sv39
# 分配 4KiB 内存给预设的三级页表
boot_page_table_sv39:
    # 0xffffffff_c0000000 map to 0x80000000 (1G)
    # 前 511 个页表项均设置为 0 ，因此 V=0 ，意味着是空的(unmapped)
    .zero 8 * 511
    # 设置最后一个页表项，PPN=0x80000，标志位 VRWXAD 均为 1
    # 三级页表每一个页表项都代表最大1GB的物理空间,我们在entry中就初始化出三级页表,然后将最后一个页表项作为内核入口的页表项
    # (即将内核起始虚拟地址(0xFFFFFFFFC0200000)-> 0x80000000这个我们希望的物理地址
    # 每个页表项低10位是标志位,中间44位是页表项(也可以理解为0x80000 000的前5个数是页表索引项,后面3个数是12位的页内偏移
    .quad (0x80000 << 10) | 0xcf # VRWXAD
