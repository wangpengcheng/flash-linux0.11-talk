/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

 /*
 * asm.s 程序中包括大部分的硬件故障（或出错）处理的底层次代码。页异常是由内存管理程序      
 * mm 处理的，所以不在这里。此程序还处理（希望是这样）由于 TS-位而造成的 fpu 异常，      
 * 因为 fpu 必须正确地进行保存/恢复处理，这些还没有测试过。
 */
// 本代码文件主要涉及对Intel 保留的中断 int0--int16 的处理（int17-int31 留作今后使用）。     
// 以下是一些全局函数名的声明，其原形在traps.c 中说明。

.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

// int0 的系统信号中断
_divide_error:
	pushl $_do_divide_error // 将目标调用函数地址入栈，出错号为0
no_error_code:  // 无错误处理的入口处
	xchgl %eax,(%esp) // 栈顶位置do_divide_error 的地址 存入eax
	pushl %ebx  // 将相关寄存器依次入栈
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds // 段寄存器入栈
	push %es
	push %fs
	pushl $0		# "error code" // 将错误码入栈--初始值为0
	lea 44(%esp),%edx // 11*4 获取原调用返回地址处的堆栈指针位置--中断返回地址
	pushl %edx  // 将基础地址压入栈
	movl $0x10,%edx //  内核代码数据段选择符。
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs // 下行上的'*'号表示是绝对调用操作数，与程序指针 PC 无关。
	call *%eax // 调用do_divide_error 函数，之后进行寄存器栈恢复
	addl $8,%esp // 移动8字节，指向寄存器fs入栈处
	pop %fs // 取出fs
	pop %es // 取出es
	pop %ds // 
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax  // 取出原来eax中的内容
	iret
// int1--bug 调试中断入口点。处理过程同上。 
_debug:
	pushl $_do_int3		// _do_debug c函数入口，主要对相关堆栈进行保存
	jmp no_error_code
// int2 -- 非屏蔽中断调用入口点
_nmi:
	pushl $_do_nmi
	jmp no_error_code

_int3:
	pushl $_do_int3
	jmp no_error_code
// int4 -- 溢出出错处理中断入口点
_overflow:
	pushl $_do_overflow
	jmp no_error_code
// int5 -- 边界检查出错中断入口点
_bounds:
	pushl $_do_bounds
	jmp no_error_code
// int6 -- 无效操作指令出出错中断入口点
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code
// int9 -- 协处理器段超出出错中断入口点
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code
// int15 -- 保留，相关保留系统中断
_reserved:
	pushl $_do_reserved
	jmp no_error_code
// int45 -- (= 0x20 + 13) 数学协处理器(Coprocessor)发出中断
// 当协处理器执行完一个操作时会发出IRQ13 中断信号，以通知CPU操作完成
_irq13:
	pushl %eax // 将eax的值压入堆栈
	xorb %al,%al // 执行亦或操作，清空al
	outb %al,$0xF0 // 将al输出到0xF0端口--消除CPU的BUSY延续信号
	movb $0x20,%al // 指定al值为0x20
	outb %al,$0x20  // 发送结束信号
	jmp 1f  // 跳转指令起延时作用
1:	jmp 1f 
1:	outb %al,$0xA0  // 发送EOI(中断结束)信号
	popl %eax
	jmp _coprocessor_error  // 跳转到处理器错误

// 以下中断在调用时会在中断返回地址之后将出错号压入堆栈，因此返回时也需要将出错号弹出。
// int8 -- 双出错故障。（下面这段代码的含义参见图 5.3(b)）
_double_fault:
	pushl $_do_double_fault  // 将函数地址压入栈中
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax， 将寄存器的值进行依次入栈
	xchgl %ebx,(%esp)		# &function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code  错误号入栈
	lea 44(%esp),%eax		# offset  程序返回地址处堆栈指针位置入栈
	pushl %eax
	movl $0x10,%eax  # 设置内核数据段选择符号
	mov %ax,%ds   
	mov %ax,%es
	mov %ax,%fs
	call *%ebx  # 调用相应的C函数，其参数
	addl $8,%esp # 栈顶指针上移8字节，放置fs内容位置
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret  # 返回CPU中断继续执行
// int10--无效的任务状态段(TSS)
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code
// int11 -- 段不存在
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code
// int12 -- 队栈段错误
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code
// int13 -- 一般保护性出错
_general_protection:
	pushl $_do_general_protection
	jmp error_code

