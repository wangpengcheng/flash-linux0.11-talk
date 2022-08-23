/*
 * linux/kernel/math/math_emulate.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This directory should contain the math-emulation code.
 * Currently only results in a signal.
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
/**
 * @brief  协处理器仿真函数。
 * 中断处理程序调用的 C 函数，参见(kernel/math/system_call.s，169 行)。
 * @param  edi              My Param doc
 * @param  esi              My Param doc
 * @param  ebp              My Param doc
 * @param  sys_call_ret     My Param doc
 * @param  eax              My Param doc
 * @param  ebx              My Param doc
 * @param  ecx              My Param doc
 * @param  edx              My Param doc
 * @param  fs               My Param doc
 * @param  es               My Param doc
 * @param  ds               My Param doc
 * @param  eip              My Param doc
 * @param  cs               My Param doc
 * @param  eflags           My Param doc
 * @param  ss               My Param doc
 * @param  esp              My Param doc
 */
void math_emulate(long edi, long esi, long ebp, long sys_call_ret,
	long eax,long ebx,long ecx,long edx,
	unsigned short fs,unsigned short es,unsigned short ds,
	unsigned long eip,unsigned short cs,unsigned long eflags,
	unsigned short ss, unsigned long esp)
{
	unsigned char first, second;
	// 选择符 0x000F 表示在局部描述符表中描述符索引值=1，即代码空间。如果段寄存器 cs 不等于 0x000F     
	// 则表示 cs 一定是内核代码选择符，是在内核代码空间，则出错，显示此时的 cs:eip 值，并显示信息    
	// “内核中需要数学仿真”，然后进入死机状态。

	/* 0x0007 means user code space */
	if (cs != 0x000F) {
		printk("math_emulate: %04x:%08x\n\r",cs,eip);
		panic("Math emulation needed in kernel");
	}
	first = get_fs_byte((char *)((*&eip)++));
	second = get_fs_byte((char *)((*&eip)++));
	printk("%04x:%08x %02x %02x\n\r",cs,eip-2,first,second);
	current->signal |= 1<<(SIGFPE-1);
}
/// 协处理器出错处理函数。     
// 中断处理程序调用的 C 函数，参见(kernel/math/system_call.s，145 行)
void math_error(void)
{
	// 协处理器指令。(以非等待形式)清除所有异常标志、忙标志和状态字位 7。
	__asm__("fnclex");
	// 如果上个任务使用过协处理器，则向上个任务发送协处理器异常信号。
	if (last_task_used_math)
		last_task_used_math->signal |= 1<<(SIGFPE-1);
}
