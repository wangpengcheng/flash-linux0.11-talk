/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
/**
 * @brief 下面给出相应信号在信号位图中的对应比特位置
 */
#define ALRMMASK (1<<(SIGALRM-1))  // 警告 信号位屏蔽
#define KILLMASK (1<<(SIGKILL-1))  // kill 信号位屏蔽
#define INTMASK (1<<(SIGINT-1))    // 键盘中断(INT)信号屏蔽
#define QUITMASK (1<<(SIGQUIT-1))  // 键盘退出信号屏蔽
#define TSTPMASK (1<<(SIGTSTP-1))  // tty 发出停止进程(tty stop) 信号屏蔽

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>
/**
 * @brief 获取本地模式标志
 */
#define _L_FLAG(tty,f)	((tty)->termios.c_lflag & f)
/**
 * @brief 获取输入模式标志
 */
#define _I_FLAG(tty,f)	((tty)->termios.c_iflag & f)
/**
 * @brief 获取输出模式标志
 */
#define _O_FLAG(tty,f)	((tty)->termios.c_oflag & f)
/**
 * @brief 获取termios 结构中本地模式标志集中的一个标志位
 */

#define L_CANON(tty) _L_FLAG((tty), ICANON)
#define L_ISIG(tty) _L_FLAG((tty), ISIG)
#define L_ECHO(tty) _L_FLAG((tty), ECHO)
#define L_ECHOE(tty) _L_FLAG((tty), ECHOE)
#define L_ECHOK(tty) _L_FLAG((tty), ECHOK)
#define L_ECHOCTL(tty) _L_FLAG((tty), ECHOCTL)
#define L_ECHOKE(tty) _L_FLAG((tty), ECHOKE)

#define I_UCLC(tty) _I_FLAG((tty), IUCLC)
#define I_NLCR(tty) _I_FLAG((tty), INLCR)
#define I_CRNL(tty) _I_FLAG((tty), ICRNL)
#define I_NOCR(tty) _I_FLAG((tty), IGNCR)
// 获取输出模式标志中的一个标志位
#define O_POST(tty) _O_FLAG((tty), OPOST)  // 输出模式标志集中执行输出处理标志
#define O_NLCR(tty) _O_FLAG((tty), ONLCR)  // 取换行符NL转回车换行符 CR-NL 标志
#define O_CRNL(tty) _O_FLAG((tty), OCRNL)
#define O_NLRET(tty) _O_FLAG((tty), ONLRET)
#define O_LCUC(tty) _O_FLAG((tty), OLCUC)
/**
 * @brief tty 数据结构的tty_table 数组
 * 其中包含三个初始化项数据
 * 分别对应、
 * - 控制台
 * - 串口终端1
 * - 串口终端2
 * 的初始化数据
 */
struct tty_struct tty_table[] = {
	{
		{ICRNL,		/* change incoming CR to NL */
		OPOST|ONLCR,	/* change outgoing NL to CRNL */
		0,
		ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, // 本地模式标志符号
		0,		/* console termio */   // 控制台 termio 
		INIT_C_CC},   // 控制字符数组
		0,			/* initial pgrp */   // 所属初始进程
		0,			/* initial stopped */ // 初始化中断停止标志符号
		con_write,   // 写入控制函数，进行控制台终端写入函数
		{0,0,0,0,""},		/* console read-queue */  // 读取队列初始化
		{0,0,0,0,""},		/* console write-queue */ // 写入队列初始化
		{0,0,0,0,""}		/* console secondary queue */ // 辅助队列初始化
	},{
		{0, /* no translation */ // 输入模式标志，0，无需转换
		0,  /* no translation */ // 输出模式标志，0。无需转换
		B2400 | CS8,   //  控制模式标志。波特率 2400bps，8 位数据位。
		0,    // 本地模式标志 0
		0,    // 行规程 0
		INIT_C_CC}, // 控制字符数组
		0,
		0,
		rs_write,
		{0x3f8,0,0,0,""},		/* rs 1 */
		{0x3f8,0,0,0,""},
		{0,0,0,0,""}
	},{
		{0, /* no translation */
		0,  /* no translation */
		B2400 | CS8,
		0,
		0,
		INIT_C_CC},
		0,
		0,
		rs_write,
		{0x2f8,0,0,0,""},		/* rs 2 */
		{0x2f8,0,0,0,""},
		{0,0,0,0,""}
	}
};

/*
 * these are the tables used by the machine code handlers.
 * you can implement pseudo-tty's or something by changing
 * them. Currently not done.
 * tty 队列初始化，是3个tty 中的read和write 队列地址
 */
// tty 缓冲队列地址表。rs_io.s 汇编程序使用，用于取得读写缓冲队列地址。
struct tty_queue * table_list[]={
	&tty_table[0].read_q, &tty_table[0].write_q,  // 控制台终端读写
	&tty_table[1].read_q, &tty_table[1].write_q,  // 串口1终端
	&tty_table[2].read_q, &tty_table[2].write_q   // 串口2终端
	};
/**
 * @brief tty终端初始化函数
 * 初始化串口终端和控制台终端
 */
void tty_init(void)
{
	/**
	 * @brief 初始化串行终端程序
	 * 和串口接口1 和2 (serial.c, 37)
	 * 初始化控制台终端(console.c 617) -- 显卡驱动
	 */
	rs_init();
	con_init();
}
/**
 * @brief  进程中断处理函数
 * tty 键盘中断(ctrl + c ) 字符处理函数
 * 向 tty 结构中指明的（前台）进程组中所有的进程发送指定的信号 mask，通常该信号是SIGINT。     
 * 参数：tty - 相应 tty 终端结构指针；mask - 信号屏蔽位。
 * @param  tty              终端
 * @param  mask             信号量
 */
void tty_intr(struct tty_struct * tty, int mask)
{
	int i;
	// 非正常进程，没有控制终端，不会发出中断字符
	// 直接退出，无需进行处理
	if (tty->pgrp <= 0)
		return;
	// 遍历所有队列，修改信号量
	for (i=0;i<NR_TASKS;i++)
		if (task[i] && task[i]->pgrp==tty->pgrp)
			task[i]->signal |= mask;
}
/**
 * @brief 如果队列缓冲区为空
 * 则让进程进入可中断的睡眠状态
 * @param  queue            指定的队列指针
 * 进程在取队列缓冲区中字符时调用此函数
 */
static void sleep_if_empty(struct tty_queue * queue)
{
	cli();
	// 如果队列为空，并且没有要处理的信号
	// 让进程进入可中断睡眠状体啊，并让队列的进程等待指针
	// 指向该进程
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->proc_list);
	sti();
}
/**
 * @brief 若缓冲队列满，则让进程进入可中断的睡眠状态
 * 进程在往缓冲队列中写入时调用此函数
 * @param  queue            指定队列的指针
 */
static void sleep_if_full(struct tty_queue * queue)
{
	// 缓冲队列不满，则返回退出
	if (!FULL(*queue))
		return;
	cli();
	// 如果进程没有信号需要处理并且队列缓冲区中空闲剩余区长度<128，则让进程进入可中断睡眠状态，     
	// 并让该队列的进程等待指针指向该进程。
	while (!current->signal && LEFT(*queue)<128)
		interruptible_sleep_on(&queue->proc_list);
	sti();
}
/**
 * @brief 等待任何按键
 * 如果控制台点读缓冲区空
 * 则进程进入可中断的睡眠状态
 */
void wait_for_keypress(void)
{
	sleep_if_empty(&tty_table[0].secondary);
}
/**
 * @brief  复制成规范模式字符序列
 * 将tty 中的原始字符串，转换成规范字符串并放在辅助队列中
 * @param  tty              指定终端的tty结构
 */
void copy_to_cooked(struct tty_struct * tty)
{
	signed char c;
   // 辅助队列未满 && 读队列非空
	while (!EMPTY(tty->read_q) && !FULL(tty->secondary)) {
		// 读取一个字符
		GETCH(tty->read_q,c);
		// 回车标志符处理
		if (c==13) 
			if (I_CRNL(tty))
				c=10;
			else if (I_NOCR(tty))
				continue;
			else ;
		// 如果该字符是换行符 NL(10)并且换行转回车标志 NLCR置位，则将其转换为回车符CR(13)。
		else if (c==10 && I_NLCR(tty))
			c=13;
		// 如果大写转小写标志位UCLC设置，进行大写转小写
		if (I_UCLC(tty))
			c=tolower(c);
		if (L_CANON(tty)) {
			if (c==KILL_CHAR(tty)) { // 键盘终止字符
				/* deal with killing the input line */
				// 清空辅助队列： 将数据输出到写队列--屏幕显示输出
				while(!(EMPTY(tty->secondary) ||
				        (c=LAST(tty->secondary))==10 ||
				        c==EOF_CHAR(tty))) {
					if (L_ECHO(tty)) {
						if (c<32)
							PUTCH(127,tty->write_q);
						PUTCH(127,tty->write_q);
						tty->write(tty);
					}
					DEC(tty->secondary.head);
				}
				continue;
			}
			// 删除控制字符
			if (c==ERASE_CHAR(tty)) {
				if (EMPTY(tty->secondary) ||
				   (c=LAST(tty->secondary))==10 ||
				   c==EOF_CHAR(tty))
					continue;
				if (L_ECHO(tty)) {
					// 控制字符，放入一个擦除字符
					if (c<32)
						PUTCH(127,tty->write_q);
					PUTCH(127,tty->write_q);
					// 调用写入函数
					tty->write(tty);
				}
				DEC(tty->secondary.head);
				continue;
			}
			// 停止字符
			if (c==STOP_CHAR(tty)) {
				tty->stopped=1;
				continue;
			}
			// 开始字符
			if (c==START_CHAR(tty)) {
				tty->stopped=0;
				continue;
			}
		}
		// 若输入模式标志集中 ISIG 标志置位，表示终端键盘可以产生信号，则在收到 INTR、QUIT、SUSP     
		// 或 DSUSP 字符时，需要为进程产生相应的信号。
		if (L_ISIG(tty)) {
			// 如果该字符是键盘中断符(^C)，则向当前进程发送键盘中断信号，并继续处理下一字符。
			if (c==INTR_CHAR(tty)) {
				tty_intr(tty,INTMASK);
				continue;
			}
			// 如果该字符是退出符号，则向当前进程发送键盘退出信号，并继续处理下一个字符
			if (c==QUIT_CHAR(tty)) {
				tty_intr(tty,QUITMASK);
				continue;
			}
		}
		// 如果该字符是NL(10),或者是文件结束符号
		// 表示一行字符已经处理完，把辅助缓冲队列中含有读字符行数值 + 1
		// 在tty_read()中若取走一行字符，就会将其值 -1 
		if (c==10 || c==EOF_CHAR(tty))
			tty->secondary.data++;
		// 如果设置了本地回显，需要进行显卡输出
		if (L_ECHO(tty)) {
			if (c==10) { // 结束字符需要单独进行输出
				PUTCH(10,tty->write_q);
				PUTCH(13,tty->write_q);
			} else if (c<32) {  // 控制字符，单独进行输出
				if (L_ECHOCTL(tty)) {
					PUTCH('^',tty->write_q);
					PUTCH(c+64,tty->write_q);
				}
			} else {
				PUTCH(c, tty->write_q);
			}
			tty->write(tty);
		}
		// 将该字符放入辅助队列中
		PUTCH(c,tty->secondary);
	}
	// 唤醒等待的辅助缓冲队列进程
	wake_up(&tty->secondary.proc_list);
}
/**
 * @brief  ty 读函数，从终端辅助缓冲队列中读取指定数量的字符，放到用户指定的缓冲区中。
 * @param  channel          子设备号
 * @param  buf              用户缓冲区指针
 * @param  nr               需要读取的子节数目
 * @return int 				已经返回已经读取的子节数
 */
int tty_read(unsigned channel, char * buf, int nr)
{
	struct tty_struct * tty;
	char c, * b=buf;
	int minimum,time,flag=0;
	long oldalarm;
	// 检查设备号和目标长度
	if (channel>2 || nr<0) return -1;
	// 获取设备缓冲区表
	tty = &tty_table[channel];
	oldalarm = current->alarm;
	// 读写超时定时值time和需要最少读取的字符个数minimum
	time = 10L*tty->termios.c_cc[VTIME];
	minimum = tty->termios.c_cc[VMIN];
	// 设置超时时间
	if (time && !minimum) {
		minimum=1;
		if (flag=(!oldalarm || time+jiffies<oldalarm))
			current->alarm = time+jiffies;
	}
	if (minimum>nr)
		minimum=nr;
	// 进行数据读取
	while (nr>0) {
		if (flag && (current->signal & ALRMMASK)) {
			current->signal &= ~ALRMMASK;
			break;
		}
		// flag没有置位，或者已置位但当前进程有其它信号要处理
		// 退出，返回0
		if (current->signal)
			break;
		// 如果辅助缓冲队列(规范模式队列)为空，或者设置了规范模式标志并且辅助队列中字符数为 0 以及     
		// 辅助模式缓冲队列空闲空间>20，则进入可中断睡眠状态，返回后继续处理。
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
		!tty->secondary.data && LEFT(tty->secondary)>20)) {
			sleep_if_empty(&tty->secondary);
			continue;
		}
		// 辅助缓冲队列数据读取
		do {
			GETCH(tty->secondary,c);
			// 
			if (c==EOF_CHAR(tty) || c==10)
				tty->secondary.data--;
			if (c==EOF_CHAR(tty) && L_CANON(tty))
				return (b-buf);
			// 否则说明是原始模式（非规范模式）操作，于是将该字符直接放入用户数据段缓冲区 buf 中，并把     
			// 欲读字符数减 1。此时如果欲读字符数已为 0，则中断循环。
			else {
				put_fs_byte(c,b++);
				if (!--nr)
					break;
			}
		} while (nr>0 && !EMPTY(tty->secondary));
		// 如果超时定时值 time 不为 0 并且规范模式标志没有置位(非规范模式)，那么：
		if (time && !L_CANON(tty))
			if (flag=(!oldalarm || time+jiffies<oldalarm))
				current->alarm = time+jiffies;
			else
				current->alarm = oldalarm;
		// 如果规范模式标志置位，那么若已读到起码一个字符则中断循环。否则若已读取数大于或等于最少要     
		// 求读取的字符数，则也中断循环。
		if (L_CANON(tty)) {
			if (b-buf)
				break;
		} else if (b-buf >= minimum)
			break;
	}
	current->alarm = oldalarm;
	if (current->signal && !(b-buf))
		return -EINTR;
	return (b-buf);
}
/**
 * @brief  tty 写函数。把用户缓冲区中的字符写入 tty 的写队列中。
 * @param  channel          子设备号
 * @param  buf              缓冲区指针
 * @param  nr               写子节数目
 * @return int
 */
int tty_write(unsigned channel, char * buf, int nr)
{
	static cr_flag=0;
	struct tty_struct * tty;
	char c, *b=buf;

	if (channel>2 || nr<0) return -1;
	tty = channel + tty_table;
	while (nr>0) {
		sleep_if_full(&tty->write_q);
		if (current->signal)
			break;
		while (nr>0 && !FULL(tty->write_q)) {
			c=get_fs_byte(b);
			if (O_POST(tty)) {
				if (c=='\r' && O_CRNL(tty))
					c='\n';
				else if (c=='\n' && O_NLRET(tty))
					c='\r';
				if (c=='\n' && !cr_flag && O_NLCR(tty)) {
					cr_flag = 1;
					PUTCH(13,tty->write_q);
					continue;
				}
				if (O_LCUC(tty))
					c=toupper(c);
			}
			b++; nr--;
			cr_flag = 0;
			PUTCH(c,tty->write_q);
		}
		tty->write(tty);
		if (nr>0)
			schedule();
	}
	return (b-buf);
}

/*
 * Jeh, sometimes I really like the 386.
 * This routine is called from an interrupt,
 * and there should be absolutely no problem
 * with sleeping even in an interrupt (I hope).
 * Of course, if somebody proves me wrong, I'll
 * hate intel for all time :-). We'll have to
 * be careful and see to reinstating the interrupt
 * chips before calling this, though.
 *
 * I don't think we sleep here under normal circumstances
 * anyway, which is good, as the task sleeping might be
 * totally innocent.
 */
//// tty 中断处理调用函数 - 执行 tty 中断处理。     
// 参数：tty - 指定的 tty 终端号（0，1 或 2）。     
// 将指定 tty 终端队列缓冲区中的字符复制成规范(熟)模式字符并存放在辅助队列(规范模式队列)中。     
// 在串口读字符中断(rs_io.s, 109)和键盘中断(kerboard.S, 69)中调用。
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table+tty);
}

void chr_dev_init(void)
{
}
