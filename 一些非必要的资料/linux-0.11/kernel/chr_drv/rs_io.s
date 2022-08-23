/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	rs_io.s
 *
 * This module implements the rs232 io interrupts.
 */

  /*       
  * 该程序模块实现 rs232 输入输出中断处理程序。      
  */ 

.text
.globl _rs1_interrupt,_rs2_interrupt
/* 读写缓冲队列的长度 */
size	= 1024				/* must be power of two !
					   and must match the value
					   in tty_io.c!!! */

/* these are the offsets into the read/write buffer structures */
/* 读写缓冲结构的偏移量对应tty_queue */
rs_addr = 0
head = 4  // head
tail = 8 // 尾部
proc_list = 12 // 等待缓冲队列指针
buf = 16  // 队列缓冲区
                    /* 写队列可写剩余字符空间长度 */
startup	= 256		/* chars left in write queue when we restart it */

/*
 * These are the actual interrupt routines. They look where
 * the interrupt is coming from, and take appropriate action.
 * 实际中断程序，用于进行相关中断处理
 */
.align 2
_rs1_interrupt:
	pushl $_table_list+8 // tty 表中对应串口1的读写缓冲指针的地址入栈
	jmp rs_int  // 调用rs_int
.align 2
_rs2_interrupt:
	pushl $_table_list+16  // tty 表中对应串口2的读写缓冲指针的地址入栈
rs_int:  // 中断处理函数
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds		/* as this is an interrupt, we cannot */
	pushl $0x10		/* know that bs is ok. Load it */
	pop %ds   /* 检查ds是否加载正确，加载ds、es指向内核数据段 */
	pushl $0x10
	pop %es
	movl 24(%esp),%edx  // 将缓冲队列指针地址存入edx寄存器
	movl (%edx),%edx 
	movl rs_addr(%edx),%edx // 取出对应串口的端口号放入edx
	addl $2,%edx		/* interrupt ident. reg */  /* edx 指向中断标识寄存器 */
rep_int:  // 中断处理核心函数
	xorl %eax,%eax // 清空eax寄存器
	inb %dx,%al  // 取出中断标识字节，用以判断中断来源
	testb $1,%al  // 判断有待处理的中断(位 =1 无中断，=0 有中断)
	jne end  // 无中断直接结束
	cmpb $6,%al		/* this shouldn't happen, but ... 检查al值大于6 */
	ja end  // 直接跳出
	movl 24(%esp),%ecx  // 将缓冲队列地址放入ecx
	pushl %edx  // 将edx--中断标识寄存器端口号 0x3fa(0x2fa) 放入栈中
	subl $2,%edx  // 0x3f8(0x2f8)
	call jmp_table(,%eax,2)		/* NOTE! not *4, bit0 is 0 already */
/*
上面语句是指，当有待处理中断时，al 中位 0=0，位 2-1 是中断类型，因此相当于已经将中断类型乘了 2，
这里再乘 2，获得跳转表（第 79 行）对应各中断类型地址，并跳转到那里去作相应处理。     
中断来源有 4 种：
modem 状态发生变化；
要写（发送）字符；
要读（接收）字符；
线路状态发生变化。    
要发送字符中断是通过设置发送保持寄存器标志实现的。在 serial.c 程序中的 rs_write()函数中，
当写缓冲队列中有数据时，就会修改中断允许寄存器内容，添加上发送保持寄存器中断允许标志，    
从而在系统需要发送字符时引起串行中断发生。
*/
	popl %edx  // 弹出中断标识寄存器端口号0x3fa(或0x2fa)
	jmp rep_int
end:	movb $0x20,%al
	outb %al,$0x20		/* EOI */
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4,%esp		# jump over _table_list entry //丢弃缓冲队列指针地址。
	iret  // 结束程序
// 各种中断类型处理程序地址跳转表
// 4种来源
// modem 状态变化中断
// 写字符中断
// 读字符中断
// 线路状态有问题中断
jmp_table:
	.long modem_status,write_char,read_char,line_status
// 由于 modem 状态发生变化而引发此次中断。通过读 modem状态寄存器对其进行复位操作。
.align 2
modem_status:
	addl $6,%edx		/* clear intr by reading modem status reg */
	inb %dx,%al         /* 通过读 modem 状态寄存器进行复位(0x3fe) */
	ret
// 由于线路状态发生变化而引起这次串行中断。通过读线路状态寄存器对其进行复位操作。
.align 2
line_status:
	addl $5,%edx		/* clear intr by reading line status reg. */
	inb %dx,%al
	ret
// 由于串行设备（芯片）接收到字符而引起这次中断。将接收到的字符放到读缓冲队列 read_q 头     
// 指针（head）处，并且让该指针前移一个字符位置。若 head 指针已经到达缓冲区末端，则让其     
// 折返到缓冲区开始处。最后调用 C 函数do_tty_interrupt()（也即 copy_to_cooked()），把读     
// 入的字符经过一定处理放入规范模式缓冲队列（辅助缓冲队列secondary）中。
.align 2
read_char:
	inb %dx,%al   // 读取字符 -> al
	movl %ecx,%edx  // 当前串口缓冲队列指针地址 -> edx
	subl $_table_list,%edx  // 缓冲队列指针表首地址 - 当前串口队列指针地址 -> edx
	shrl $3,%edx   // 差值/8
	movl (%ecx),%ecx		# read-queue // 读取缓冲队列第一个值
	movl head(%ecx),%ebx  // ebx 指向缓冲区头部地址
	movb %al,buf(%ecx,%ebx)  // 将读取到的字符，放在缓冲区头指针的位置
	incl %ebx   // 将指针前移动一个字节
	andl $size-1,%ebx   // 与运算，防止溢出 等价取余
	cmpl tail(%ecx),%ebx  // 检查是否已经指向尾部
	je 1f  // 是，直接结束，调用软中断函数
	movl %ebx,head(%ecx) // 否，重新修改头部指针
1:	pushl %edx  // 将串口号压入堆栈(1 - 串口1，2 - 串口2)，作为参数
	call _do_tty_interrupt  // 调用tty 中断处理C 函数()
	addl $4,%esp  // 丢弃入栈参数，并返回
	ret
// 写入函数，主要用来进行显卡驱动的写入
// 将读取的字符写入显卡缓冲区，由显卡进行输出
// 由于串行设备(芯片)接收到字符而引起的这次中断
// 将接收到的字符放到读缓冲队列read_q 头指针(head)处
// 并且让该指针前移一个字符位置，若head指针已经达到缓冲区末端
// 则让其折返到缓冲区开始处。最后调用C函数do_tty_interrupt()
// 把读入的字符经过一定处理放入规范模式缓冲队列(辅助缓冲队列secondary)中
.align 2
write_char:
	movl 4(%ecx),%ecx		# write-queue // 读取写缓冲队列地址 -> ecx
	movl head(%ecx),%ebx   // 写队列头指针到ebx
	subl tail(%ecx),%ebx  // 计算队列中字符数量 = 头指针 - 尾指针
	andl $size-1,%ebx		# nr chars in queue // 检查是否存在字符  
	je write_buffer_empty  // 无字符，进入空字符串处理
	cmpl $startup,%ebx    // 检查字符数量 > 256
	ja 1f   // 超过，跳转进行处理，否则直接唤醒等待中的进程进行处理
	movl proc_list(%ecx),%ebx	# wake up sleeping process  // 唤醒等待进程进行处理
	testl %ebx,%ebx			# is there any?  // 检测是否有剩余
	je 1f  // 空，跳转1 处进行执行
	movl $0,(%ebx)   // 将进程设置为可运行状态
1:	movl tail(%ecx),%ebx  // 获取尾部指针 -> ebx
	movb buf(%ecx,%ebx),%al  // 从缓冲中尾部指针取一字符 -> al
	outb %al,%dx  // 输出到保持寄存器中
	incl %ebx   // 尾部指针前移
	andl $size-1,%ebx // 检查是否已经越界-- 到末尾
	movl %ebx,tail(%ecx)  // 设置新的头部指针数据
	cmpl head(%ecx),%ebx  // 到达头部，表示已经清空，进行跳转
	je write_buffer_empty // 跳转至写入缓冲队列位空的情况
	ret
// 处理写缓冲队列write_q已空的情况。若有等待写该串行终端的进程则唤醒之，然后屏蔽发送     
// 保持寄存器空中断，不让发送保持寄存器空时产生中断。
.align 2
write_buffer_empty:
	movl proc_list(%ecx),%ebx	# wake up sleeping process // 获取等待队列进程
	testl %ebx,%ebx			# is there any?  // 检查是否位空
	je 1f  // 无，跳转到1
	movl $0,(%ebx) // 有：将进程设置为可运行状态
1:	incl %edx  // 指向端口 0x3f9(0x2f9)。
	inb %dx,%al // 读取中断允许寄存器
	jmp 1f  // 稍作延迟
1:	jmp 1f
1:	andb $0xd,%al		/* disable transmit interrupt */ /* 蔽发送保持寄存器空中断（位 1） */ 
	outb %al,%dx
	ret
