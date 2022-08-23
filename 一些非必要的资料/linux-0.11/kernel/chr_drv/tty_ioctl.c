/*
 *  linux/kernel/chr_drv/tty_ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

/**
 * @file tty_ioctl.c
 * @brief 字符设备控制相关操作实现，主要实现了系统调用tty_ioctl()
 * 修改终端结构中的设置标志信息
 * @author wangpengcheng  (wangpengcheng2018@gmail.com)
 * @version 1.0
 * @date 2022-08-14 21:01:34
 * @copyright Copyright (c) 2022  IRLSCU
 * 
 * @par 修改日志:
 * <table>
 * <tr>
 *    <th> Commit date</th>
 *    <th> Version </th> 
 *    <th> Author </th>  
 *    <th> Description </th>
 * </tr>
 * <tr>
 *    <td> 2022-08-14 21:01:34 </td>
 *    <td> 1.0 </td>
 *    <td> wangpengcheng </td>
 *    <td> 增加文档日志 </td>
 * </tr>
 * </table>
 */
#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/system.h>
//  这是波特率因子数组（或称为除数数组）。波特率与波特率因子的对应关系参见列表后的说明。
static unsigned short quotient[] = {
	0, 2304, 1536, 1047, 857,
	768, 576, 384, 192, 96,
	64, 48, 24, 12, 6, 3
};
/**
 * @brief 修改终端设置传送速度
 * @param  tty              指定的终端
 */
static void change_speed(struct tty_struct * tty)
{
	unsigned short port,quot;

	if (!(port = tty->read_q.data))
		return;
	// 从 tty 的 termios 结构控制模式标志集中取得设置的波特率索引号，据此从波特率因子数组中取得     
	// 对应的波特率因子值。CBAUD是控制模式标志集中波特率位屏蔽码。
	quot = quotient[tty->termios.c_cflag & CBAUD];
	cli();
	outb_p(0x80,port+3);		/* set DLAB */
	outb_p(quot & 0xff,port);	/* LS of divisor */
	outb_p(quot >> 8,port+1);	/* MS of divisor */
	outb(0x03,port+3);		/* reset DLAB */
	sti();
}
/**
 * @brief  刷新 tty 缓冲队列。
 * 令缓冲队列的头指针等于尾指针，从而达到清空缓冲区(零字符)的目的。
 * @param  queue           指定缓冲队列指针
 */
static void flush(struct tty_queue * queue)
{
	cli();
	queue->head = queue->tail;
	sti();
}

static void wait_until_sent(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}

static void send_break(struct tty_struct * tty)
{
	/* do nothing - not implemented */
}
/**
 * @brief 取终端 termios 结构信息。
 * @param  tty              指定终端的tty 结构指针
 * @param  termios          用户数据区，缓冲指针
 * @return int    执行结果
 */
static int get_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;
	// 校验内存地址
	verify_area(termios, sizeof (*termios));
	// 复制指定 tty 结构中的 termios 结构信息到用户 termios 结构缓冲区。
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		put_fs_byte( ((char *)&tty->termios)[i] , i+(char *)termios );
	return 0;
}
/**
 * @brief Set the termios object
 * 设置终端termios 结构信息
 * @param  tty             指定终端的tty结构指针
 * @param  termios         用户数据区 termios结构脂针
 * @return int 
 */
static int set_termios(struct tty_struct * tty, struct termios * termios)
{
	int i;
	// 复制用户数据区中的termios 结构信息到指定的tty结构中
	for (i=0 ; i< (sizeof (*termios)) ; i++)
		((char *)&tty->termios)[i]=get_fs_byte(i+(char *)termios);
	// 修改终端相关设置
	change_speed(tty);
	return 0;
}
/**
 * @brief 查询tty对应的termio
 * @param  tty              目标终端
 * @param  termio           相关信息拷贝
 * @return int 
 */
static int get_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;
	// 检查内存是否充足
	verify_area(termio, sizeof (*termio));
	tmp_termio.c_iflag = tty->termios.c_iflag;
	tmp_termio.c_oflag = tty->termios.c_oflag;
	tmp_termio.c_cflag = tty->termios.c_cflag;
	tmp_termio.c_lflag = tty->termios.c_lflag;
	tmp_termio.c_line = tty->termios.c_line;
	// 进行数据拷贝
	for(i=0 ; i < NCC ; i++)
		tmp_termio.c_cc[i] = tty->termios.c_cc[i];
	// 进行内存值拷贝
	for (i=0 ; i< (sizeof (*termio)) ; i++)
		put_fs_byte( ((char *)&tmp_termio)[i] , i+(char *)termio );
	return 0;
}

/*
 * This only works as the 386 is low-byt-first
 */

/**
 * @brief 设置终端termio结构信息
 * @param  tty              指定终端的tty结构指针
 * @param  termio           目标termio
 * @return int 
 */
static int set_termio(struct tty_struct * tty, struct termio * termio)
{
	int i;
	struct termio tmp_termio;

	for (i=0 ; i< (sizeof (*termio)) ; i++)
		((char *)&tmp_termio)[i]=get_fs_byte(i+(char *)termio);
	*(unsigned short *)&tty->termios.c_iflag = tmp_termio.c_iflag;
	*(unsigned short *)&tty->termios.c_oflag = tmp_termio.c_oflag;
	*(unsigned short *)&tty->termios.c_cflag = tmp_termio.c_cflag;
	*(unsigned short *)&tty->termios.c_lflag = tmp_termio.c_lflag;
	tty->termios.c_line = tmp_termio.c_line;
	for(i=0 ; i < NCC ; i++)
		tty->termios.c_cc[i] = tmp_termio.c_cc[i];
	change_speed(tty);
	return 0;
}
/**
 * @brief  输入字符设备指令
 * @param  dev              设备
 * @param  cmd              指令
 * @param  arg              参数
 * @return int 				相关结果
 */
int tty_ioctl(int dev, int cmd, int arg)
{
	struct tty_struct * tty;
	if (MAJOR(dev) == 5) {
		dev=current->tty;
		if (dev<0)
			panic("tty_ioctl: dev<0");
	} else
		dev=MINOR(dev);
	// 查询对应的设备表
	tty = dev + tty_table;
	switch (cmd) {
		case TCGETS:
			return get_termios(tty,(struct termios *) arg);
		case TCSETSF:
			flush(&tty->read_q); /* fallthrough 重置设备缓冲区 */
		case TCSETSW:
			// 在设置终端 termios 的信息之前，需要先等待输出队列中所有数据处理完(耗尽)。对于修改参数     
			// 会影响输出的情况，就需要使用这种形式。
			wait_until_sent(tty); /* fallthrough */
		case TCSETS:
			// 取对应终端结构中的信息
			return set_termios(tty,(struct termios *) arg);
		case TCGETA:
			return get_termio(tty,(struct termio *) arg);
		case TCSETAF:
			flush(&tty->read_q); /* fallthrough */
		case TCSETAW:
			wait_until_sent(tty); /* fallthrough */
		case TCSETA:
			return set_termio(tty,(struct termio *) arg);
		case TCSBRK:
			if (!arg) {
				wait_until_sent(tty);
				send_break(tty);
			}
			return 0;
		case TCXONC:
			// 重新开启挂起的输入
			return -EINVAL; /* not implemented */
		case TCFLSH:
			if (arg==0)
				flush(&tty->read_q);
			else if (arg==1)
				flush(&tty->write_q);
			else if (arg==2) {
				flush(&tty->read_q);
				flush(&tty->write_q);
			} else
				return -EINVAL;
			return 0;
		case TIOCEXCL:
			return -EINVAL; /* not implemented */
		case TIOCNXCL:
			return -EINVAL; /* not implemented */
		case TIOCSCTTY:
			return -EINVAL; /* set controlling term NI */
		case TIOCGPGRP:
			verify_area((void *) arg,4);
			put_fs_long(tty->pgrp,(unsigned long *) arg);
			return 0;
		case TIOCSPGRP:
			tty->pgrp=get_fs_long((unsigned long *) arg);
			return 0;
		case TIOCOUTQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->write_q),(unsigned long *) arg);
			return 0;
		case TIOCINQ:
			verify_area((void *) arg,4);
			put_fs_long(CHARS(tty->secondary),
				(unsigned long *) arg);
			return 0;
		case TIOCSTI:
			return -EINVAL; /* not implemented */
		case TIOCGWINSZ:
			return -EINVAL; /* not implemented */
		case TIOCSWINSZ:
			return -EINVAL; /* not implemented */
		case TIOCMGET:
			return -EINVAL; /* not implemented */
		case TIOCMBIS:
			return -EINVAL; /* not implemented */
		case TIOCMBIC:
			return -EINVAL; /* not implemented */
		case TIOCMSET:
			return -EINVAL; /* not implemented */
		case TIOCGSOFTCAR:
			return -EINVAL; /* not implemented */
		case TIOCSSOFTCAR:
			return -EINVAL; /* not implemented */
		default:
			return -EINVAL;
	}
}
