/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
/**
 * 定义在unistd.h中的内嵌汇编代码信息
 * *.h 头文件所在的默认目录是 incloud,
 * 则在代码中就不用明确指明位置
 * */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */

/**
 * linux 进行子进程进行fork时，需要拷贝父进程的相关堆栈信息
 * 因此不能直接使用函数来创建堆栈，避免复制
 * 这里对关键函数使用预定义的方式进行调用
 * 主要是切换用户态，并执行对应的软中断
 * https://www.cnblogs.com/feng9exe/p/12521350.html
 */

static inline _syscall0(int, fork) static inline _syscall0(int, pause) static inline _syscall1(int, setup, void *, BIOS) static inline _syscall0(int, sync)

/**
 *
 * tty头部，定义了有关tty_io，串行通信方面的参数，常数
 *
 *
 **/
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h> // head 头文件，定义了段描述符的简单结构，和几个选择符常量
#include <asm/system.h> // 系统头文件。以宏的形式定义了许多有关设置或修改 描述符/中断门等的嵌入式汇编子程序。

#include <asm/io.h> // io 头文件。以宏的嵌入汇编程序形式定义对 io 端口操作的函数。

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h> // 文件系统头文件。定义文件表结构（file,buffer_head,m_inode 等）。

	static char printbuf[1024]; // 静态字符串数组，用作内核显示信息的缓存。

extern int vsprintf();							 // 格式化输出到一字符串中（在kernel/vsprintf.c，92 行）。
extern void init(void);							 // 函数原形，初始化（在 168 行）。
extern void blk_dev_init(void);					 // 块设备初始化子程序(kernel/blk_drv/ll_rw_blk.c)
extern void chr_dev_init(void);					 // 字符设备初始化程序（kernel/chr_drv/tty_io.c, 347 行）
extern void hd_init(void);						 // 硬盘初始化程序（kernel/blk_drv/hd.c, 343 行）
extern void floppy_init(void);					 // 软驱初始化程序（kernel/blk_drv/floppy.c, 457 行）
extern void mem_init(long start, long end);		 // 内存管理初始化（mm/memory.c, 399 行）
extern long rd_init(long mem_start, int length); // 虚拟盘初始化(kernel/blk_drv/ramdisk.c,52)
extern long kernel_mktime(struct tm *tm);		 // 计算系统开机启动时间（秒）。
extern long startup_time;						 // 内核启动时间（开机时间）（秒）。

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)	   // 1M后扩展内存大小
#define DRIVE_INFO (*(struct drive_info *)0x90080) // 硬盘参数表，可以见 setup.s
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC) // 根文件系统所在设备号。

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

/**
 * @brief 读取指定地址的时钟信息
 * 指定数据输入端口为0x70
 * 数据输出端口为0x71
 * @note
 * - https://blog.csdn.net/wyyy2088511/article/details/120524407
 * - https://www.cnblogs.com/tlnshuju/p/7276936.html
 * - https://blog.csdn.net/longintchar/article/details/79783305
 */
#define CMOS_READ(addr) ({     \
	outb_p(0x80 | addr, 0x70); \
	inb_p(0x71);               \
})

#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val) >> 4) * 10)

/**
 * @brief 时钟初始化函数
 * 该子程序 CMOS 时钟，并设置开机时间Æstartup_time(秒)。参见后面 CMOS 内存列表。
 */
static void time_init(void)
{
	// 定义时钟
	struct tm time;
	// 循环读取信息到
	do
	{
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0)); // 存在时间差异循环读取，减小时间误差
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	// ：调用函数kernel_mktime()，计算从 1970 年 1 月 1 日 0 时起到现在经过的秒数，作为开机时间，保存到全局变量
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;		   // 机器具有的物理内存容量
static long buffer_memory_end = 0; // 高速缓冲区末端地址
static long main_memory_start = 0; // 主内存(将用于分页)开始的位置

struct drive_info
{
	char dummy[32];
} drive_info; // 存放硬盘参数表信息

void main(void) /* This really IS void, no error here. */
{				/* The startup routine assumes (well, ...) this */
				/*
				 * Interrupts are still disabled. Do necessary setups, then
				 * enable them
				 */
	/** 此时中断仍被禁止着，做完必要的设置后就将其开启。      */
	// 保存根设备号，在setup.s 中被设置
	ROOT_DEV = ORIG_ROOT_DEV;					// ROOT_DEV 定义在 fs/super.c,29 行。
	drive_info = DRIVE_INFO;					// 复制硬盘参数表
	memory_end = (1 << 20) + (EXT_MEM_K << 10); // 内存大小=1Mb字节 + 扩展内存(K) * 1024 字节
	memory_end &= 0xfffff000;					// 忽略不到4Kb(1页)的内存数
	if (memory_end > 16 * 1024 * 1024)			// 如果内存超过 16Mb，则按 16Mb 计，最大寻址不超过20 位
		memory_end = 16 * 1024 * 1024;
	if (memory_end > 12 * 1024 * 1024) // 如果内存>12Mb，则设置缓冲区末端=4Mb
		buffer_memory_end = 4 * 1024 * 1024;
	else if (memory_end > 6 * 1024 * 1024) // 否则如果内存>6Mb，则设置缓冲区末端=2Mb
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024; // 否则则设置缓冲区末端=1Mb
	main_memory_start = buffer_memory_end;	 // 主内存起始位置 = 缓冲区末端；
#ifdef RAMDISK								 // 定义内存虚拟盘，则初始化虚拟盘，此时主内存将减少。参见 kernel/blk_drv/ramdisk.c
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);
#endif
	mem_init(main_memory_start, memory_end); // 内存初始化
	trap_init();							 // 陷阱门（硬件中断向量）初始化。（kernel/traps.c，181）
	blk_dev_init();							 // 块设备初始化。    （kernel/blk_drv/ll_rw_blk.c，157）
	chr_dev_init();							 // 字符设备初始化。  （kernel/chr_drv/tty_io.c，347）
	tty_init();								 // tty初始化。      （kernel/chr_drv/tty_io.c，105）
	time_init();							 // 设置开机启动时间Îstartup_time（见 76 行）。
	sched_init();							 // 调度程序初始化(加载了任务0 的 tr,ldtr)（kernel/sched.c，385）
	buffer_init(buffer_memory_end);			 // 缓冲管理初始化，建内存链表等。（fs/buffer.c，348）
	hd_init();								 // 硬盘初始化。     （kernel/blk_drv/hd.c，343）
	floppy_init();							 // 软驱初始化。     （kernel/blk_drv/floppy.c，457）
	sti();									 // 所有初始化工作都做完了，开启中断
	move_to_user_mode();					 // 移到用户模式下执行。（include/asm/system.h，第 1 行）
	// 只想fork
	if (!fork())
	{			/* we count on this going ok */
		init(); // 主线程执行初始化，在新建立的子进程(任务1)中执行
	}
	/*
	 *   NOTE!!   For any other task 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0 'pause()' just means we go check if some other
	 * task can run, and if not we return here.
	 */
	// 任务0的身份运行代码
	/**
	 * @brief
	 * 注意!! 对于任何其它的任务，'pause()'将意味着我们必须等待收到一个信号才会返
	 * 回就绪运行态，但任务 0（task0）是唯一的例外情况（参见'schedule()'），因为任务 0 在
	 * 任何空闲时间里都会被激活（当没有其它任务在运行时），因此对于任务0'pause()'仅意味着
	 * 我们返回来查看是否有其它任务可以运行，如果没有的话我们就回到这里，一直循环执行'pause()'。
	 */
	// pause() 系统调用（kernel / sched.c, 144）会把任务 0 转换成可中断等待状态，再执行调度函数。
	// 但是调度函数只要发现系统中没有其它任务可以运行时就会切换到任务 0，而不依赖于任务 0 的状态。
	for (;;)
		pause();
}
/**
 * @brief  产生格式化信息并输出到标准设备stdout
 * @param  fmt              格式化字符串
 * @param  ...              相关参数
 * @return int 				返回结果
 */
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	// 使用vsprintf 将字符串放到printbuf 缓冲区
	// 输出到标准输出1--stdout
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char *argv_rc[] = {"/bin/sh", NULL}; // 调用执行程序时参数的字符串数组。
static char *envp_rc[] = {"HOME=/", NULL};	// 默认环境字符串数组

static char *argv[] = {"-/bin/sh", NULL};		// 参数字符串，"-" 标志登陆shell执行，其执行过程与shell 下执行sh 不同，主要是增加了登录验证
static char *envp[] = {"HOME=/usr/root", NULL}; // 环境字符串
/**
 * @brief
 * 在 main()中已经进行了系统初始化，包括内存管理、各种硬件设备和驱动程序。
 * init()函数运行在任务 0 第 1 次创建的子进程（任务 1）中。
 * 它首先对第一个将要执行的程序（shell）的环境进行初始化，然后加载该程序并执行之。
 */
void init(void)
{
	int pid, i;
	// setup 系统调用，用于读取硬盘参数包括分区表信息并加载虚拟硬盘和根文件系统，
	// 该函数是用 25 行上的宏定义的，对应函数是 sys_setup()，在kernel/blk_drv/hd.c 71
	setup((void *)&drive_info);
	// 下面以读写访问方式打开设备“/dev/tty0”，它对应终端控制台。
	// 由于这是第一次打开文件操作，因此产生的文件句柄号（文件描述符）肯定是 0。该句柄是 UNIX 类
	// 操作系统默认的控制台标准输入句柄 stdin。这里把它以读和写的方式打开是为了复制产生标准
	// 输出（写）句柄 stdout和标准出错输出句柄stderr。
	// 注意这里如果/etc/目录中配置文件的设置信息，会为每一个配置创建对应的子进程
	(void)open("/dev/tty0", O_RDWR, 0);
	(void)dup(0); // 复制句柄，产生句柄1 号 -- stdout 标准输出设备。
	(void)dup(0); // 复制句柄，产生句柄 2 号 -- stderr标准出错输出设备。
	printf("%d buffers = %d bytes buffer space\n\r", NR_BUFFERS,
		   NR_BUFFERS * BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r", memory_end - main_memory_start);
	// 执行fork 创建子进程(任务2)
	if (!(pid = fork())) // 子进程执行操作
	{
		close(0);							 // 关闭复制的标准具柄--守护进程
		if (open("/etc/rc", O_RDONLY, 0))	 // 只读方式打开/etc/rc 
			_exit(1);						 // 如果打开文件失败，则退出(lib/_exit.c,10)。
		execve("/bin/sh", argv_rc, envp_rc); // 使用/bin/sh 程序执行
		_exit(2);							 // 若 execve() 执行失败则退出。
	}
	// 主线程等待对应子进程进行执行
	if (pid > 0)
		while (pid != wait(&i)) // 等待对应pid结束
			/* nothing */;
	// 子进程执行结束，循环执行
	while (1)
	{
		// 再次fork
		if ((pid = fork()) < 0)
		{
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) // pid=0 -- 子进程
		{
			close(0);							// 关闭标准输入
			close(1);							// 关闭标准输出
			close(2);							// 关闭错误输出
			setsid();							// 创建新的会话层--
			(void)open("/dev/tty0", O_RDWR, 0); // 读写方式打来tty0
			(void)dup(0);						//
			(void)dup(0);
			_exit(execve("/bin/sh", argv, envp)); // 使用/bin/sh 程序执行
		}
		// 主进程继续执行
		while (1)
			if (pid == wait(&i)) // 子进程结束
				break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync(); // 同步操作，刷新缓冲区
	}
	_exit(0); /* NOTE! _exit, not exit() */
	/*
	注意！是_exit()，不是 exit()，_exit()和 exit()都用于正常终止一个函数。
	但_exit()直接是一个 sys_exit 系统调用，
	而 exit()则 通常是普通函数库中的一个函数。
	它会先执行一些清除操作，例如调用执行各终止处理程序、关闭所/有标准 IO等，然后调用 sys_exit。
 	*/
}
