/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
#include <linux/kernel.h>
#include <linux/sched.h>

void sys_sync(void);	/* it's really int */
// 内核panic 程序
/**
 * @brief 该函数用来显示内核中出现的重大错误信息，并运行文件系统同步函数，
 * 然后进入死循环 -- 死机。
 * 如果当前进程是任务 0 的话，还说明是交换任务出错，并且还没有运行文件系统同步函数。
 * @param  s                My Param doc
 * @return volatile
 */
volatile void panic(const char * s)
{
	// 输出内核panic
	printk("Kernel panic: %s\n\r",s);
	// 0号任务，说明交换错误
	if (current == task[0])
		printk("In swapper task - not syncing\n\r");
	else
		sys_sync(); // 调用底层进行硬盘文件写入
	for(;;);
}
