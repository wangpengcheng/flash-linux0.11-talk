/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
/**
 * @brief 唤醒字符界限
 * 当写入队列中含有 WAKEUP_CHARS 个字符串时
 * 就开始进行发送
 */
#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

/**
 * @brief 
 * 串行口1的中断处理程序(rs_io.s, 34)
 */
extern void rs1_interrupt(void);
/**
 * @brief 串行口2的中断处理程序(rs_io.s, 38)
 */
extern void rs2_interrupt(void);

/**
 * @brief  初始化串行端口
 * 
 * @param  port     串行端口编号 0x3F8 和 0x2f8
 */
static void init(int port)
{
    // 设置线路控制寄存器 DLAB位
	outb_p(0x80,port+3);	/* set DLAB of line control reg */
	outb_p(0x30,port);	    /* LS of divisor (48 -> 2400 bps */
	outb_p(0x00,port+1);	/* MS of divisor */
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0d,port+1);	/* enable all intrs but writes */
	(void)inb(port);	    /* read data port to reset things (?) */
}
/**
 * @brief 串口端口初始化
 */
void rs_init(void)
{
    // 设置中断门向量，处理函数
	set_intr_gate(0x24,rs1_interrupt);
	set_intr_gate(0x23,rs2_interrupt);
    init(tty_table[1].read_q.data); // 初始化串口 1(.data 是端口号)
    init(tty_table[2].read_q.data); // 初始化串口 2 
	outb(inb_p(0x21)&0xE7,0x21);    // 允许主8259A 芯片的IRQ3，IRQ4 中断请求
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */

/**
 * @brief   tty_write()已将数据放入输出(写)队列时会调用下面的子程序。
 * 必须首先查写队列是否为空，并相应设置中断寄存器。
 * 串行数据发送输出。     
 * 实际上只是开启串行发送保持寄存器已空中断标志，在 UART 将数据发送出去后允许发中断信号。
 * @param  tty              对应的串口终端
 */
void rs_write(struct tty_struct * tty)
{
	cli(); // 关闭中断
	// 检查写入队列是否为空
	if (!EMPTY(tty->write_q))
		// 不为空修改中断允许标志位，让串口设备允许字符写入产生的中断
		// 用来刷新屏幕
		// 1. 读起中断允许寄存器内容
		// 2. 添加发送保持寄存器中断允许标志位
		// 3. 重新进行写入
		outb(inb_p(tty->write_q.data+1)|0x02,tty->write_q.data+1);
	sti(); // 允许中断
}
