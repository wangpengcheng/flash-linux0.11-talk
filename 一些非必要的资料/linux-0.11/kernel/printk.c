/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * When in kernel-mode, we cannot use printf, as fs is liable to
 * point to 'interesting' things. Make a printf with fs-saving, and
 * all is well.
 */
#include <stdarg.h>
#include <stddef.h>

#include <linux/kernel.h>

static char buf[1024];

extern int vsprintf(char * buf, const char * fmt, va_list args);
/**
 * @brief  进行内核参数输出
 * @param  fmt              格式化字符串
 * @param  ...              相关参数
 * @return int 				最终执行结果
 */
int printk(const char *fmt, ...)
{
	// 可变参数列表 -- 字符指针类型
	va_list args;
	int i;
	// 开始处理参数
	va_start(args, fmt);
	// 进行参数组合
	// 返回字符串长度
	i=vsprintf(buf,fmt,args);
	// 处理结束函数
	va_end(args);
	// 将其输送到内核，进行打印
	__asm__("push %%fs\n\t" // 保存fs
			"push %%ds\n\t"
			"pop %%fs\n\t"		  // 取出ds: fs=ds
			"pushl %0\n\t"		  // 将字符串长度压入栈
			"pushl $_buf\n\t"	  // 将buf地址压入栈
			"pushl $0\n\t"		  // 0压入栈
			"call _tty_write\n\t" // 进行系统调用，输出到tty 用 tty_write 函数。(kernel/chr_drv/tty_io.c,290)
			"addl $8,%%esp\n\t"  // 跳过两个入栈参数
			"popl %0\n\t"  // 弹出字符串长度，作为返回值
			"pop %%fs" ::"r"(i)  // 回复fs寄存器
			: "ax", "cx", "dx");
	return i;
}
