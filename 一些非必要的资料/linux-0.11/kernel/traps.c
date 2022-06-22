/*
 *  linux/kernel/traps.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'. Currently mostly a debugging-aid, will be extended
 * to mainly kill the offending process (probably by giving it a signal,
 * but possibly by killing it outright if necessary).
 */
/*
程序用来处理硬件中断和故障，主要用于调试
以后将扩展用来杀死损坏进程(发送系统信号)
*/
#include <string.h>

#include <linux/head.h>  // 定义了一些简单的结构
#include <linux/sched.h> // 调度程序头文件，定义了任务结构的task_struct、初始任务0的数据，嵌入式汇编函数和宏语句
#include <linux/kernel.h> // 内核函数关键头文件
#include <asm/system.h> // 系统头文件，定义了设置或者修改描述符/中断门定义
#include <asm/segment.h> // 段操作头文件。定义了有关段寄存器操作的嵌入式汇编函数
#include <asm/io.h> // 输入/输出头文件。定义硬件端口输入/输出宏汇编语句
// 取段 seg 中地址 addr 处的一个字节。
// 用圆括号括住的组合语句（花括号中的语句）可以作为表达式使用，其中最后的__res 是其输出值。
/**
 * @brief 查询段中指定地址的值--1字节
 */
#define get_seg_byte(seg,addr) ({ \
register char __res; \
__asm__("push %%fs;mov %%ax,%%fs;movb %%fs:%2,%%al;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})
/**
 * @brief 读取段中的值 2字节
 */
#define get_seg_long(seg,addr) ({ \
register unsigned long __res; \
__asm__("push %%fs;mov %%ax,%%fs;movl %%fs:%2,%%eax;pop %%fs" \
	:"=a" (__res):"0" (seg),"m" (*(addr))); \
__res;})
/**
 * @brief 获取当前的段指针指向的地址
 */
#define _fs() ({ \
register unsigned short __res; \
__asm__("mov %%fs,%%ax":"=a" (__res):); \
__res;})
/**
 * @brief 程序退出处理函数
 * @param  code          最终退出码
 * 	- 0 :
 *  - 1 :
 *  - 2 :
 * @return int 
 */
int do_exit(long code);

void page_exception(void);

void divide_error(void);
void debug(void);
void nmi(void);
void int3(void);
void overflow(void);
void bounds(void);
void invalid_op(void);
void device_not_available(void);
void double_fault(void);
void coprocessor_segment_overrun(void);
void invalid_TSS(void);
void segment_not_present(void);
void stack_segment(void);
void general_protection(void);
void page_fault(void);
void coprocessor_error(void);
void reserved(void);
void parallel_interrupt(void);
void irq13(void);

/**
 * @brief 静态函数用来打赢出错中断的名称、错误号、程序的EIP寄存器等参数
 * @param  str              输出字符串
 * @param  esp_ptr          栈顶置针地址
 * @param  nr               错误码信息
 */
static void die(char * str,long esp_ptr,long nr)
{
	// 转换为栈顶部指针
	long * esp = (long *) esp_ptr;
	int i;
	// 打印名称和原始错误码
	printk("%s: %04x\n\r",str,nr&0xffff);
	// 答应EIP信息
	printk("EIP:\t%04x:%p\nEFLAGS:\t%p\nESP:\t%04x:%p\n",
		esp[1],esp[0],esp[2],esp[4],esp[3]);
	// 打印fs段寄存器的值
	printk("fs: %04x\n",_fs());
	// 打印当前段的基地址、段长度
	printk("base: %p, limit: %p\n",get_base(current->ldt[1]),get_limit(0x17));
	if (esp[4] == 0x17) {
		printk("Stack: "); // 打印堆栈
		for (i=0;i<4;i++)
			printk("%p ",get_seg_long(0x17,i+(long *)esp[3]));
		printk("\n");
	}
	str(i);
	// 打印PID、任务号
	printk("Pid: %d, process nr: %d\n\r",current->pid,0xffff & i);
	// 打印10字节指令码
	for(i=0;i<10;i++)
		printk("%02x ",0xff & get_seg_byte(esp[1],(i+(char *)esp[0])));
	printk("\n\r");
	do_exit(11);		/* play segment exception */
}

void do_double_fault(long esp, long error_code)
{
	die("double fault",esp,error_code);
}

void do_general_protection(long esp, long error_code)
{
	die("general protection",esp,error_code);
}

void do_divide_error(long esp, long error_code)
{
	die("divide error",esp,error_code);
}

void do_int3(long * esp, long error_code,
		long fs,long es,long ds,
		long ebp,long esi,long edi,
		long edx,long ecx,long ebx,long eax)
{
	int tr;

	__asm__("str %%ax":"=a" (tr):"0" (0));
	printk("eax\t\tebx\t\tecx\t\tedx\n\r%8x\t%8x\t%8x\t%8x\n\r",
		eax,ebx,ecx,edx);
	printk("esi\t\tedi\t\tebp\t\tesp\n\r%8x\t%8x\t%8x\t%8x\n\r",
		esi,edi,ebp,(long) esp);
	printk("\n\rds\tes\tfs\ttr\n\r%4x\t%4x\t%4x\t%4x\n\r",
		ds,es,fs,tr);
	printk("EIP: %8x   CS: %4x  EFLAGS: %8x\n\r",esp[0],esp[1],esp[2]);
}

void do_nmi(long esp, long error_code)
{
	die("nmi",esp,error_code);
}

void do_debug(long esp, long error_code)
{
	die("debug",esp,error_code);
}

void do_overflow(long esp, long error_code)
{
	die("overflow",esp,error_code);
}

void do_bounds(long esp, long error_code)
{
	die("bounds",esp,error_code);
}

void do_invalid_op(long esp, long error_code)
{
	die("invalid operand",esp,error_code);
}

void do_device_not_available(long esp, long error_code)
{
	die("device not available",esp,error_code);
}

void do_coprocessor_segment_overrun(long esp, long error_code)
{
	die("coprocessor segment overrun",esp,error_code);
}

void do_invalid_TSS(long esp,long error_code)
{
	die("invalid TSS",esp,error_code);
}

void do_segment_not_present(long esp,long error_code)
{
	die("segment not present",esp,error_code);
}

void do_stack_segment(long esp,long error_code)
{
	die("stack segment",esp,error_code);
}

void do_coprocessor_error(long esp, long error_code)
{
	if (last_task_used_math != current)
		return;
	die("coprocessor error",esp,error_code);
}

void do_reserved(long esp, long error_code)
{
	die("reserved (15,17-47) error",esp,error_code);
}
// 下面是异常（陷阱）中断程序初始化子程序。设置它们的中断调用门（中断向量）。     
// set_trap_gate()与 set_system_gate()的主要区别在于前者设置的特权级为 0，后者是 3。因此     
// 断点陷阱中断 int3、溢出中断 overflow 和边界出错中断 bounds 可以由任何程序产生。     // 这两个函数均是嵌入式汇编宏程序(include/asm/system.h,第 36 行、39 行)。
void trap_init(void)
{
	int i;
	// 设置除操作出错的中断向量值。以下雷同。
	set_trap_gate(0, &divide_error);
	set_trap_gate(1,&debug);
	set_trap_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(8,&double_fault);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_trap_gate(14,&page_fault);
	set_trap_gate(15,&reserved);
	set_trap_gate(16,&coprocessor_error);
	// 面将 int17-48 的陷阱门先均设置为reserved，以后每个硬件初始化时会重新设置自己的陷阱门。

	for (i=17;i<48;i++)
		set_trap_gate(i,&reserved);
	set_trap_gate(45,&irq13);  // 设置协处理器的陷阱门
	outb_p(inb_p(0x21)&0xfb,0x21); // 允许主 8259A 芯片的IRQ2中断请求
	outb(inb_p(0xA1) & 0xdf, 0xA1); // 允许从8259A 芯片的IRQ2中断请求
	set_trap_gate(39,&parallel_interrupt); // 设置并行口的陷阱门
}
