/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

 /*      
  * system_call.s文件包含系统调用(system-call)底层处理子程序。由于有些代码比较类似，所以      
  * 同时也包括时钟中断处理(timer-interrupt)句柄。硬盘和软盘的中断处理程序也在这里。      
  *      
  * 注意：这段代码处理信号(signal)识别，在每次时钟中断和系统调用之后都会进行识别。一般      
  * 中断信号并不处理信号识别，因为会给系统造成混乱。      
  *      
  * 从系统调用返回（'ret_from_system_call'）时堆栈的内容见上面 19-30 行。      
  */


SIG_CHLD	= 17  /* 定义SIG_CHLD 信号(子进程停止或者结束) */

EAX		= 0x00  /* 定义堆栈中各个寄存器点偏移位置，用于存储数据 */
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28  /* 当有特权等级变化时，调用此接口 */
OLDSS		= 0x2C
/* 下面是任务结构(task_struct)中变量的偏移值，参见 include/linux/sched.h */
state	= 0		# these are offsets into the task-struct. # 进程状态码
counter	= 4   /* 进程运行时间片计数器，递减 */
priority = 8  /* 运行优先等级，任务开始时counter=priority,越大则运行时间越长 */
signal	= 12 /* 对应信号值 */
sigaction = 16		# MUST be 16 (=len of sigaction) /* 段页符号长度 */
blocked = (33*16)  /* 受阻塞信号位图的偏移量 */

# offsets within sigaction 
/* 定义在 sigaction 结构中的偏移量，参见 include/signal.h，第 48 行开始。 */
sa_handler = 0  /* 信号处理函数具柄指针 */
sa_mask = 4  /* 信号处理掩码 */
sa_flags = 8  /* 对应信号集合 */
sa_restorer = 12 /* 恢复函数指针，参见 kernel/signal.c */

nr_system_calls = 72  /* linux 0.11 版内核中的系统调用总数 */

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
 /* 对应函数链接定位符号 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error
/* 错误系统调用号 */
.align 2   /* 内存 4字节对齐 */
bad_sys_call:
	movl $-1,%eax  /* 将eax 寄存器设置为-1,作为返回值 */
	iret   /* 设置中断 */
.align 2
reschedule:
	pushl $ret_from_sys_call  /* 设置系统回调 */
	jmp _schedule  /* 进入调度代码 */
.align 2
_system_call:
	cmpl $nr_system_calls-1,%eax  /* 对系统调用号-1， */
	ja bad_sys_call /* 超出返回在eax中置为-1并退出 */
	push %ds  /* 保存堆栈寄存器 */
	push %es
	push %fs
	pushl %edx
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds   /* 将es，ds指向内核数据段 */
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space /* fs指向局部数据段 */
	mov %dx,%fs
	call _sys_call_table(,%eax,4)  /* 查询系统调用表对应函数,进行调用 调用地址 = _sys_call_table + %eax *4 */
	pushl %eax    /* 将函数返回值入栈 */
	movl _current,%eax  /* 将当前任务(进程)数据地址放入 eax */
	cmpl $0,state(%eax)		# state /* 检查当前状态是否为0 */
	jne reschedule  /* 不为0，进行调用 */
	cmpl $0,counter(%eax)		# counter  /* 检查时间片是否为0 */  
	je reschedule  /* 进行重新调用 */
/* 系统调用结束后，对信号量进行处理 */
ret_from_sys_call:
	movl _current,%eax		# task[0] cannot have signals
	cmpl _task,%eax /* 判断不是为0号任务，直接返回 */
	je 3f  /* 跳转到代码3直接结束 */
/* 
通过对原调用程序代码选择符的检查来判断调用程序是否是内核任务（例如任务 1）。
如果是则直接退出中断。否则对于普通进程则需进行信号量的处理。
这里比较选择符是否为普通用户代码段的选择符 0x000f (RPL=3，局部表，第 1 个段(代码段))，
如果不是则跳转退出中断程序。
*/
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?/* 如果原堆栈段选择符号不在用户数据段中，进行推出 */
	jne 3f
/*
    查询当前任务结构的信号位图
    使用任务结构中的信号阻塞(屏蔽)码，阻塞不允许的信号位，取得数值最小的信号值
    把原信号图中
*/
	movl signal(%eax),%ebx /* 将信号位图 传递给ebx,每一位代表一种信号，一共32个信号 */ 
	movl blocked(%eax),%ecx /* 取阻塞(屏蔽)信号位图 -> ecx */
	notl %ecx  /* 每位进行取反 */
	andl %ebx,%ecx  /* 获取许可的信号位图 */
	bsfl %ecx,%ecx  /* 位（位 0）开始扫描位图，看是否有 1 的位，有，则 ecx 保留该位的偏移值（即第几位 0-31） */
	je 3f  /* 没有信号位，保留偏移值进行退出 */
	btrl %ecx,%ebx  /* 包含信号位进行复位 */
	movl %ebx,signal(%eax)  /* 重新保存signal 位图信息 -> current-> signal */
	incl %ecx /* 将信号调整为从1开始的数(1-32) */
	pushl %ecx  /* 信号值入栈，作为_do_signal 的参数 */
	call _do_signal  /* 调用信号处理函数 */
	popl %eax /* 弹出信号值 */
3:	popl %eax    /* 将保存的寄存器进行恢复  */
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret
/* int16 -- 下面这段代码处理协处理器发出的出错信号。跳转执行 C 函数 math_error()    
(kernel/math/math_emulate.c,82)，返回后将跳转到ret_from_sys_call 处继续执行。*/
.align 2
_coprocessor_error:
	push %ds   /* 保存相关堆栈 */
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax   /* 将ds、es重新指向内核数据段 */
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax  /* 将fs指向用户 局部数据段 */
	mov %ax,%fs
	pushl $ret_from_sys_call   /* 调用执行恢复 */
	jmp _math_error  /* 跳转到错误处理: math_error()(kernel/math/math_emulate.c,37*/


/* 
#### int7 -- 设备不存在或协处理器不存在(Coprocessor not available)。     
# 如果控制寄存器 CR0 的 EM 标志置位，则当 CPU 执行一个 ESC 转义指令时就会引发该中断，这样就     
# 可以有机会让这个中断处理程序模拟 ESC 转义指令（169 行）。     
# CR0 的 TS 标志是在 CPU 执行任务转换时设置的。TS 可以用来确定什么时候协处理器中的内容（上下文）    
# 与 CPU 正在执行的任务不匹配了。当 CPU 在运行一个转义指令时发现 TS 置位了，就会引发该中断。     
# 此时就应该恢复新任务的协处理器执行状态（165 行）。参见(kernel/sched.c,77)中的说明。     
# 该中断最后将转移到标号 ret_from_sys_call 处执行下去（检测并处理信号）。
*/

.align 2
_device_not_available:
	push %ds 
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax /* 切换到内核中断 */
	mov %ax,%ds 
	mov %ax,%es
	movl $0x17,%eax  /* 指向用户对应数据段 */ 
	mov %ax,%fs
	pushl $ret_from_sys_call  /* 将下面跳转或调用的返回地址入栈 */
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit) /* 非EM中断，使用新任务协处理器状态 */
	je _math_state_restore  /* 执行函数 math_state_restore()(kernel/sched.c,77)。 */
	pushl %ebp
	pushl %esi
	pushl %edi
	call _math_emulate  /* 用 C 函数 math_emulate(kernel/math/math_emulate.c,18)。 */
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl _jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

.align 2
_sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call _do_execve
	addl $4,%esp
	ret

.align 2
_sys_fork:
	call _find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process
	addl $20,%esp
1:	ret

_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
