/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

volatile void do_exit(long code);

/**
 * @brief 显示内存已用完出错信息，并退出
 * @return volatile 
 */
static inline volatile void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);   // 直接退出
}
/**
 * @brief 刷新页变换高速缓冲宏函数。
 * 为了提高地址转换的效率，CPU 将最近使用的页表数据存放在芯片中高速缓冲中。在修改过页表信息之后，
 * 就需要刷新该缓冲区。这里使用重新加载页目录基址寄存器 cr3 的方法来进行刷新。
 * 下面 eax = 0，是页目录的基址。
 */
#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000  // 内存低端（1MB）
#define PAGING_MEMORY (15*1024*1024)   // 分页内存15MB。
#define PAGING_PAGES (PAGING_MEMORY>>12)  // 分页之后的物理内存页数
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)  // 指定内存地址映射为页号
#define USED 100			// 页面被占用标志

/**
 * @brief  检查当前地址，是否属于当前进程指定的代码段地址中
 */
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0;
/**
 * @brief 复制1页内存(4K字节)
 */
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")
/**
 * @brief 物理内存页
 */
static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */

/**
 * @brief Get the free page object
 * @return unsigned long 
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"
	"jne 1f\n\t"
	"movb $1,1(%%edi)\n\t"
	"sall $12,%%ecx\n\t"
	"addl %2,%%ecx\n\t"
	"movl %%ecx,%%edx\n\t"
	"movl $1024,%%ecx\n\t"
	"leal 4092(%%edx),%%edi\n\t"
	"rep ; stosl\n\t"
	"movl %%edx,%%eax\n"
	"1:"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	:"di","cx","dx");
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	addr -= LOW_MEM;
    // 计算页编号
	addr >>= 12;
    // 操作并返回
	if (mem_map[addr]--) return;
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */

/**
 * @brief  根据指定的线性地址和限制长度(页表个数)，释放对应内存页表所指定的内存块并置表项空闲
 * @param  from             开始物理地址
 * @param  size             地址内存大小
 * @return int 
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
	// 计算所占页目录项数(4M 的进位整数倍)，也即所占页表数。
	size = (size + 0x3fffff) >> 22;
	// 下面一句计算起始目录项。对应的目录项号=from>>22，因每项占 4字节，并且由于页目录是从     
	// 物理地址 0 开始，因此实际的目录项指针=目录项号<<2，也即(from>>20)。与上 0xffc 确保     
	// 目录项指针范围有效。
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))
			continue;
		pg_table = (unsigned long *) (0xfffff000 & *dir);
		for (nr=0 ; nr<1024 ; nr++) {
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;
			pg_table++;
		}
		free_page(0xfffff000 & *dir);
		*dir = 0;
	}
	invalidate();
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */

//// 复制指定线性地址和长度（页表个数）内存对应的页目录项和页表，从而被复制的页目录和 
//// 页表对应的原物理内存区被共享使用。     
// 复制指定地址和长度的内存对应的页目录项和页表项。需申请页面来存放新页表，原内存区被共享；     
// 此后两个进程将共享内存区，直到有一个进程执行写操作时，才分配新的内存页（写时复制机制）。
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;

	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;
	// 对每个占用的页表分别进行复制
	for( ; size-->0 ; from_dir++,to_dir++) {
		// 指定页表已经存在
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		// 此源目录项未被使用，则不复制对应页表，跳火
		if (!(1 & *from_dir))
			continue;
		// 获取当前源目录项中页表地址 --> from_page_table
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		// 找一个空闲页遍
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		// 获取根dir
		*to_dir = ((unsigned long) to_page_table) | 7;
		// 计算需要复制的页面数，如果是在内核空间，则仅需复制头160 页（640KB），     
		// 否则需要复制一个页表中的所有 1024 页面。
		nr = (from==0)?0xA0:1024;
		// 遍历页表进行拷贝
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			// 没有被使用，则不用复制
			if (!(1 & this_page))
				continue;
			// 复位页表项中 R/W 标志(置 0)。(如果 U/S 位是 0，则 R/W 就没有作用。如果 U/S 是 1，而 R/W 是 0，     
			// 那么运行在用户层的代码就只能读页面。如果 U/S 和 R/W 都置位，则就有写的权限。)
			this_page &= ~2;
			// 页表项复制到目的页表中
			*to_page_table = this_page;
			// 增加页表项引用计数
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	// 刷线页变换高速缓冲
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page;
}
/// 取消写保护页面函数。用于页异常中断过程中写保护异常的处理（写时复制）。     
// 输入参数为页表项指针。     
// [ un_wp_page 意思是取消页面的写保护：Un-Write Protected。]
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;
	// 指定页表内物理页面位置
	old_page = 0xfffff000 & *table_entry;
	// 非共享页面，则设置页面的页表项中置 R/W 标志（可写），并刷新页变换高速缓冲，然后返回。
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	// 这里都是必须拷贝的页面
	// 检查是否存在空闲页
	if (!(new_page=get_free_page()))
		oom();
	// 减少引用计数
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	// 更新页表项目
	*table_entry = new_page | 7;
	invalidate();
	// 进行页拷贝
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
//// 页异常中断处理调用的 C 函数。写共享页面处理函数。在page.s 程序中被调用。     
// 参数 error_code 是由 CPU 自动产生，address 是页面线性地址。     
// 写共享页面时，需复制页面（写时复制）。
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */

	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	// 处理取消页面保护。参数指定页面在页表中的页表项指针，其计算方法是：     
	// ((address>>10) & 0xffc)：计算指定地址的页面在页表中的偏移地址；     
	// (0xfffff000 & *((address>>20) &0xffc))：取目录项中页表的地址值，     
	// 其中((address>>20) &0xffc)计算页面所在页表的目录项指针；     
	// 两者相加即得指定地址对应页面的页表项指针。这里对共享的页面进行复制。
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}
/**
 * @brief 确认目标地址可写
 * @param  address          目标逻辑地址
 */
void write_verify(unsigned long address)
{
	unsigned long page;
	// 检测页是否越界
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	// 计算页地址
	page &= 0xfffff000;
	// 计算页内偏移
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}
/**
 * @brief  给指定的线性地址一个可用的物理页面
 * @param  address          指定的地址
 */
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */

/**
 * @brief  尝试对进程指定地址处的页面进行共享操作。
 * 同时还验证指定的地址处是否已经申请了页面，若是则出错，死机。
 * @param  address          验证地址
 * @param  p                关键指针数据
 * @return int
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;
	// 原始页
	from_page = to_page = ((address>>20) & 0xffc);
	// from page 指向代码段起始位置
	from_page += ((p->start_code>>20) & 0xffc);
	// 当前进程的代码段--将程序加载到当前进程
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	// 无效目录项，直接返回0
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	// 计算原始页
	from_page = from + ((address>>10) & 0xffc);
	// 获取物理地址
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	// 检查页面对应Dirty 和 Present 标志，表示页面不干净或者无效则返回
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	// 获取页面的地址
	phys_addr &= 0xfffff000;
	// 检查是否为合法地址
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	// 取页目录项内容Îto。如果该目录项无效(P=0)，则取空闲页面，并更新to_page所指的目录项。
	to = *(unsigned long *) to_page;
	if (!(to & 1))
		if (to = get_free_page())
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;
	// 获取对应页表地址 to，对应的页面已经存在，则死机出错
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	// 对 p 进程中页面置写保护标志(置 R / W = 0 只读)。并且当前进程中的对应页表项指向它。
	*(unsigned long *)from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	// 刷新页变换高速缓冲区
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	// 增加引用计数
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */

/**
 * @brief  共享页面地址
 * @param  address          目标地址
 * @return int 
 */
static int share_page(unsigned long address)
{
	// 任务
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	// 搜索所有task ，查询与当前进程可共享页面的进程，并尝试对指定地址的页面进行共享
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))  // 尝试共享页面
			return 1;
	}
	return 0;
}

/**
 * @brief  页面异常中断处理调用的函数，处理缺页异常的情况，在page.s 程序中被调用
 * @param  error_code       进行错误码
 * @param  address          页面线性地址
 */
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;
	// 页面地址
	address &= 0xfffff000;
	// 临时地址长度
	tmp = address - current->start_code;
	// 可执行为空或者超出当前进程长度，直接获取新页面
	if (!current->executable || tmp >= current->end_data) {
		// 申请一页物理内存并映射到指定线性地址处即可。
		get_empty_page(address);
		return;
	}
	// 尝试共享页面成功，则退出
	if (share_page(tmp))
		return;
	// 再获取空闲页
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	// 根据 i 节点信息，取数据块在设备上的对应的逻辑块号。
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	// 读取设备上一个页面的数据(4个逻辑块)到指定物理地址page地址
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	// 进行内存清空
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	// 如果把物理页面映射到指定线性地址的操作成功，就返回。否则就释放内存页，显示内存不够。
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}
/**
 * @brief  物理内存初始化
 * @param  start_mem        可用作分页处理的物理内存起始位置（已去除 RAMDISK所占内存空间等）。
 * @param  end_mem          实际物理内存最大地址
 */
void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;   // 设置内存最高端
	for (i=0 ; i<PAGING_PAGES ; i++)  // 设置页面为已经占用状态
		mem_map[i] = USED;
	i = MAP_NR(start_mem);   // 计算可使用起始内存的页面号
	end_mem -= start_mem;   // 计算可分页处理的内存块大小
	end_mem >>= 12;  // 计算页面数
	while (end_mem-->0)  // 清空页面对应的页面映射数组清零
		mem_map[i++]=0;
}

/**
 * @brief 计算空闲内存页面数柄显示
 */
void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;
	// 扫描内存页面映射数组mem_map[], 获取空闲页面数并显示
	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	// 扫描所有页目录项（除 0，1 项），如果页目录项有效，则统计有效页面数，并显示
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
