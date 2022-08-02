/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 *
 * NOTE! Don't touch this without checking that nothing in rs_io.s or
 * con_io.s breaks. Some constants are hardwired into the system (mainly
 * offsets into 'tty_queue'
 */

#ifndef _TTY_H
#define _TTY_H

#include <termios.h>

#define TTY_BUF_SIZE 1024

/**
 * @brief 终端处理数据保存结构
 * 被保存在3个tty_queue结构的字符串
 * 缓冲队列中
 */
struct tty_queue
{
	unsigned long data;			   //< 等待队列缓冲区中当前数据统计值，对于串口终端，存放串口端口地址。
	unsigned long head;			   //< 缓冲区中数据头指针
	unsigned long tail;			   //< 缓冲区中数据尾部指针
	struct task_struct *proc_list; //< 等待本缓冲队列的进程列表
	char buf[TTY_BUF_SIZE];		   //< 队列缓冲区，长度为1页
};

#define INC(a) ((a) = ((a)+1) & (TTY_BUF_SIZE-1))
#define DEC(a) ((a) = ((a)-1) & (TTY_BUF_SIZE-1))
#define EMPTY(a) ((a).head == (a).tail)
#define LEFT(a) (((a).tail-(a).head-1)&(TTY_BUF_SIZE-1))
#define LAST(a) ((a).buf[(TTY_BUF_SIZE-1)&((a).head-1)])
#define FULL(a) (!LEFT(a))
#define CHARS(a) (((a).head-(a).tail)&(TTY_BUF_SIZE-1))
#define GETCH(queue,c) \
(void)({c=(queue).buf[(queue).tail];INC((queue).tail);})
#define PUTCH(c,queue) \
(void)({(queue).buf[(queue).head]=(c);INC((queue).head);})

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSPEND_CHAR(tty) ((tty)->termios.c_cc[VSUSP])
/**
 * @brief 终端设备描述数据结构体
 */
struct tty_struct
{
	struct termios termios;				   //< 终端IO 属性和控制字符数据结构，相关IO属性
	int pgrp;							   //< 所属进程组，指明前台进程组。即当前拥有该终端设备的进程组
	int stopped;						   //< 终端停止标志，表明终端是否停用
	void (*write)(struct tty_struct *tty); //< 写操作函数，控制台终端，它负责驱动显示硬件，在屏幕上显示字符串等信息
	struct tty_queue read_q;			   //< 读数据队列
	struct tty_queue write_q;			   //< 写数据队列
	struct tty_queue secondary;			   //< tty 辅助队列(存放规范模式字符序列)，
};
/**
 * @brief 终端结构数组
 * 保存每个串口设备终端信息
 */
extern struct tty_struct tty_table[];

/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

void rs_init(void);
void con_init(void);
void tty_init(void);

int tty_read(unsigned c, char * buf, int n);
int tty_write(unsigned c, char * buf, int n);

void rs_write(struct tty_struct * tty);
void con_write(struct tty_struct * tty);

void copy_to_cooked(struct tty_struct * tty);

#endif
