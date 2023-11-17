.text
.globl kernel_thread_entry
kernel_thread_entry:        # void kernel_thread(void)
	move a0, s1 # 把参数传给a0
	jalr s0 # 跳转到s0保存的函数地址中

	jal do_exit # 函数返回回来后, 调用do_exit结束线程
