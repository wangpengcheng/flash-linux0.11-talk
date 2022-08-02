/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
/**
 * @brief  写入块
 * @param  address   目标地址可写
 */
extern void write_verify(unsigned long address);
/**
 * @brief 最新进程号
 * 其值由 get_empty_process()生成。
 */
long last_pid=0;
/**
 * @brief 进程空间区域写前验证函数
 * @param  addr       目标地址空间      
 * @param  size       内存大小，单位为字节
 */
void verify_area(void* addr,int size)
{
	unsigned long start;
	// 其实地址
	start = (unsigned long) addr;
	// 设置size为最终地址 = 页内地址 + size 
	size += start & 0xfff;
	// 设置为页起始地值
	start &= 0xfffff000;
	// 获取基础线性地址
	start += get_base(current->ldt[2]);
	// 大小超过一页
	while (size>0) {
		size -= 4096;
		// 进行写页面验证
		// 确认下一页可写
		write_verify(start);
		// 重新设置start
		start += 4096;
	}
}
/**
 * @brief  设置新任务的代码和数据段基地址、限制长度并复制业表
 * @param  nr               任务号--pid
 * @param  p                My Param doc
 * @return int 
 */
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;
	// 取局部描述符表中代码段描述父项中段限长度
	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	// 获取对应基础地址
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	// 创建中新进程在线性地址空间中的基地址等于 64MB * 其任务号。
	new_data_base = new_code_base = nr * 0x4000000; //  新基址=任务号*64Mb(任务大小)。
	p->start_code = new_code_base;
	// 设置新进程局部描述符表中段描述符中的基地址。
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	// 设置新进程的页目录表项和页表项。即把新进程的线性地址内存页对应到实际物理地址内存页面上。
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		// 如果出错则释放申请的内存。
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */


/**
 * @brief  拷贝参数进程
 * @param  nr               调用find_empty 分配的任务数组项目号
 * @param  ebp              My Param doc
 * @param  edi              My Param doc
 * @param  esi              My Param doc
 * @param  gs               My Param doc
 * @param  none             system_call.s 中调用sys_call_table时压入堆栈的返回地址
 * @param  ebx              My Param doc
 * @param  ecx              My Param doc
 * @param  edx              My Param doc
 * @param  fs               My Param doc
 * @param  es               My Param doc
 * @param  ds               My Param doc
 * @param  eip              My Param doc
 * @param  cs               My Param doc
 * @param  eflags           My Param doc
 * @param  esp              My Param doc
 * @param  ss               My Param doc
 * @return int 
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;
	// 分配内存结构
	p = (struct task_struct *) get_free_page();
	if (!p)
		return -EAGAIN;
	// 设置进程号数组
	task[nr] = p;
	// /* 注意！这样做不会复制超级用户的堆栈 */（只复制当前进程内容）*/
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack */
	// 设置任务状态为不可中断状态
	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
	p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;  // 设置时钟
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p; // 内核堆栈指针
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;  // 注意：这里时fork 新进程会返回0 的原因
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp; // 复制父进程堆栈内容
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);  // 设置新任务的局部描述符表的选择
	p->tss.trace_bitmap = 0x80000000;
	/*
		// 如果当前任务使用了协处理器，就保存其上下文。汇编指令 clts 用于清除控制寄存器 CR0 中的任务 
		// 已交换（TS）标志。每当发生任务切换，CPU 都会设置该标志。该标志用于管理数学协处理器：如果     
		// 该标志置位，那么每个 ESC 指令都会被捕获。如果协处理器存在标志也同时置位的话那么就会捕获     
		// WAIT 指令。因此，如果任务切换发生在一个 ESC 指令开始执行之后，则协处理器中的内容就可能需     
		// 要在执行新的 ESC 指令之前保存起来。错误处理句柄会保存协处理器的内容并复位 TS 标志。     
		// 指令 fnsave 用于把协处理器的所有状态保存到目的操作数指定的内存区域中（tss.i387）。
	*/
	if (last_task_used_math == current) 
		__asm__("clts ; fnsave %0" ::"m"(p->tss.i387));
	// 设置新任务的代码
	if (copy_mem(nr,p)) {
		// 失败释放内存
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	// 遍历文件数组
	for (i=0; i<NR_OPEN;i++)
		// 打开文件的引用计数++
		if (f=p->filp[i])
			f->f_count++;
	// 将当前进程（父进程）的pwd, root 和 executable 引用次数均增 1。
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	// 设置GDT中的新任务TSS和LDT描述符
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	// 将其设置为RUNNING
	// 准备运行
	p->state = TASK_RUNNING;	/* do this last, just in case */
	// 返回新的pid
	return last_pid;
}
// 查找空闲进程号
int find_empty_process(void)
{
	int i;
// 如果 last_pid 增 1 后超出其正数表示范围，则重新从 1 开始使用 pid 号。
repeat:
	if ((++last_pid) < 0)
		last_pid = 1;
	// / 在任务数组中搜索刚设置的 pid 号是否已经被任何任务使用。如果是则重新获得一个 pid 号。
	for (i = 0; i < NR_TASKS; i++)
		if (task[i] && task[i]->pid == last_pid)
			goto repeat;
	// task 为空，直接返回
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}
