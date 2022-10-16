/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */

/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 */

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <a.out.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/segment.h>

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
/*      
* MAX_ARG_PAGES 定义了新程序分配给参数和环境变量使用的内存最大页数。      
* 32 页内存应该足够了，这使得环境和参数(env+arg)空间的总合达到 128kB!      
*/
#define MAX_ARG_PAGES 32

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */

/**
 * @brief  在新用户堆栈中创建环境和参数变量指针表
 * @param  p                以数据段未起点的参数和环境信息偏移指针
 * @param  argc             参数个数
 * @param  envc             环境变量个数
 * @return unsigned long* 	最终的堆栈指针
 */
static unsigned long * create_tables(char * p,int argc,int envc)
{
	unsigned long *argv,*envp;
	unsigned long * sp;
	// 堆栈指针--基础数据以long * 为4字节
	sp = (unsigned long *) (0xfffffffc & (unsigned long) p);
	// 栈指针直接进行向下增长
	sp -= envc+1;
	// 环境变了指针指向sp
	envp = sp;
	// 设置参数指针
	sp -= argc+1;
	argv = sp;
	// 将环境参数指针envp和命令行参数指针以及命令行参数个数压入堆栈
	// 方便出栈进行记录
	put_fs_long((unsigned long)envp,--sp);
	put_fs_long((unsigned long)argv,--sp);
	put_fs_long((unsigned long)argc,--sp);
	// 将命令行参数依次压入栈中
	while (argc-->0) {
		put_fs_long((unsigned long) p,argv++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	// 放入一个空指针，进行隔离区分
	put_fs_long(0,argv);
	while (envc-->0) {
		put_fs_long((unsigned long) p,envp++);
		while (get_fs_byte(p++)) /* nothing */ ;
	}
	put_fs_long(0,envp);
	// 返回最后的栈顶指针
	return sp;
}

/*
 * count() counts the number of arguments/envelopes
 */

/**
 * @brief 统计参数数目
 * @param  argv             参数数据指针
 * @return int 				最终结果
 */
static int count(char ** argv)
{
	int i=0;
	char ** tmp;

	if (tmp = argv)
		while (get_fs_long((unsigned long *) (tmp++)))
			i++;

	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */

/**
 * @brief  复制指定个数的参数字符串到参数和环境空间。
 * @param  argc             欲添加的参数个数
 * @param  argv             欲添加的参数个数
 * @param  page             参数和环境空间页面指针数组
 * @param  p                在参数表空间中的偏移指针，始终指向已复制串的头部
 * @param  from_kmem        字符串来源标志
 * @return unsigned long    字符串长度
 */
static unsigned long copy_strings(
		int argc,
		char ** argv,
		unsigned long *page,
		unsigned long p, 
		int from_kmem
		)
{
	// 相关的临时指针
	char *tmp, *pag;
	int len, offset = 0;
	unsigned long old_fs, new_fs;

	if (!p)
		return 0;	/* bullet-proofing */  /* 偏移指针验证 */
	// 将ds寄存器值到new_fs，并保存原fs寄存器到old_fs
	new_fs = get_ds();
	old_fs = get_fs();
	// 内核空间，指向内核段
	if (from_kmem==2)
		set_fs(new_fs);
	// 进行参数拷贝
	while (argc-- > 0) {
		// 设置fs 指向内核数据段
		if (from_kmem == 1)
			set_fs(new_fs);
		// 最后一个参数开始逆向操作，取fs段中最后一参数指针到tmp, 如果为空，则出错伺机
		if (!(tmp = (char *)get_fs_long(((unsigned long *)argv)+argc)))
			panic("argc is wrong");
		// 如果字符串在用户空间而字符串数组在内核空间，则恢复 fs 段寄存器原值。
		if (from_kmem == 1)
			set_fs(old_fs);
		len=0;		/* remember zero-padding */
		// 计算该参数字符串长度len,并使tmp指向该参数字符串末端
		do {
			len++;
		} while (get_fs_byte(tmp++));
		// 如果偏移长度小于字符串长度，则恢复fs 段寄存器并返回0
		if ( p-len < 0) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);  /* 不会发生--因为有128KB 段 */
			return 0;
		}
		// 针对 复制fs 段中当前指定的参数字符串，是从该字符串尾部逆向开始复制
		while (len) {
			--p; --tmp; --len;
			//  函数刚开始执行时，偏移变量offset 被初始化为0，因此若 offset-1 < 0，说明时首次复制字符串
			//  则令其等于p指针在页面内的偏移值，并申请空闲页面
			if (--offset < 0) {
				offset = p % PAGE_SIZE;
				// 如果字符串和字符串数组在内核空间，则恢复fs段寄存器原值
				if (from_kmem==2)
					set_fs(old_fs);
				// 如果当前偏移值p所在页面不存在，或者非空闲页，直接取消
				if (!(pag = (char *) page[p/PAGE_SIZE]) &&
				    !(pag = (char *) page[p/PAGE_SIZE] =
				      (unsigned long *) get_free_page())) 
					return 0;
				if (from_kmem==2)
					set_fs(new_fs);

			}
			// 从fs段中复制参数字符串中一子节到pag + offset处
			*(pag + offset) = get_fs_byte(tmp);
		}
	}
	if (from_kmem==2)
		set_fs(old_fs);
	return p;
}
/**
 * @brief  修改局部描述符号表中的描述符基础地址和段限长，并将参数和环境空间页放置在数据段末端
 * @param  text_size        执行文件头部中 a_text 字段给出的代码段长度值
 * @param  page             参数和环境空间页面指针数组
 * @return unsigned long 	数据段限制长度(64MB)
 */
static unsigned long change_ldt(unsigned long text_size,unsigned long * page)
{
	unsigned long code_limit,data_limit,code_base,data_base;
	int i;
	// 获取代码段长度限制地址
	code_limit = text_size+PAGE_SIZE -1;
	code_limit &= 0xFFFFF000;
	data_limit = 0x4000000;
	// 代码段基础地址
	code_base = get_base(current->ldt[1]);
	data_base = code_base;
	// 设置基础地址
	set_base(current->ldt[1],code_base);
	// 设置限制地址
	set_limit(current->ldt[1],code_limit);
	// 设置基础地址
	set_base(current->ldt[2],data_base);
	// 设置限制地址
	set_limit(current->ldt[2],data_limit);
/* make sure fs points to the NEW data segment */
	// fs段寄存器 放入局部表数据段描述符段选择符(0x17)
	__asm__("pushl $0x17\n\tpop %%fs"::);
	// 重新设置data_base 指针
	data_base += data_limit;
	// 遍历所有参数指针表，将非空的参数page 指向data_base 指针
	for (i=MAX_ARG_PAGES-1 ; i>=0 ; i--) {
		data_base -= PAGE_SIZE;
		if (page[i])
			put_page(page[i],data_base);
	}
	return data_limit;  // 返回数据段限制长度(64MB)
}

/*
 * 'do_execve()' executes a new program.
 */

/**
 * @brief  函数执行一个新程序，加载并执行子进程(其它程序)
 * @param  eip              指向堆栈中调用系统中断的程序代码指针 eip 处
 * @param  tmp              系统调用_sys_execve 时段返回地址，无用
 * @param  filename         被执行的程序文件名
 * @param  argv             程序执行的命令参数指针数组
 * @param  envp             环境变量指针数组
 * @return int				调用最终结果
 */
int do_execve(
	unsigned long * eip,
	long tmp,
	char * filename,
	char ** argv, 
	char ** envp
	)
{
	struct m_inode * inode;
	struct buffer_head * bh;
	struct exec ex;
	unsigned long page[MAX_ARG_PAGES];
	int i,argc,envc;
	int e_uid, e_gid;
	int retval;
	int sh_bang = 0;
	unsigned long p=PAGE_SIZE*MAX_ARG_PAGES-4;
	// eip[1]中是原代码段寄存器 cs，其中的选择符不可以是内核段选择符，也即内核不能调用本函数。
	if ((0xffff & eip[1]) != 0x000f)
		panic("execve called from supervisor mode");
	// 初始化参数和环境串空间的页面指针数组(表)
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		page[i]=0;
	// 查询文件inode
	if (!(inode=namei(filename)))		/* get executables inode */
		return -ENOENT;
	// 计算参数变量
	argc = count(argv);
	// 计算环境变量
	envc = count(envp);
// 重新开启中断
restart_interp:
	// 检查文件是否为常规文件 
	if (!S_ISREG(inode->i_mode)) {	/* must be regular file */
		retval = -EACCES;
		goto exec_error2;
	}
	// 以下检查被执行文件的执行权限。根据其属性(对应 i 节点的 uid 和 gid)，看本进程是否有权执行它。
	i = inode->i_mode;
	e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
	e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
	if (current->euid == inode->i_uid)
		i >>= 6;
	else if (current->egid == inode->i_gid)
		i >>= 3;
	if (!(i & 1) &&
	    !((inode->i_mode & 0111) && suser())) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 查询higeht buffer, 读取执行文件第一块数据到高速缓冲区
	if (!(bh = bread(inode->i_dev,inode->i_zone[0]))) {
		retval = -EACCES;
		goto exec_error2;
	}
	// 执行文件的头结构数据进行处理，将ex 指向执行头部分的数据结构
	ex = *((struct exec *) bh->b_data);	/* read exec-header */
	// 如果执行文件开始的两个字节为‘#!’,并且sh_bang 标志没有置位，则处理脚本文件的执行 
	if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang)) {
		/*
		 * This section does the #! interpretation.
		 * Sorta complicated, but hopefully it will work.  -TYT
		 */

		char buf[1023], *cp, *interp, *i_name, *i_arg;
		unsigned long old_fs;

		strncpy(buf, bh->b_data+2, 1022);
		brelse(bh);
		iput(inode);
		buf[1022] = '\0';
		if (cp = strchr(buf, '\n')) {
			*cp = '\0';
			for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++);
		}
		if (!cp || *cp == '\0') {
			retval = -ENOEXEC; /* No interpreter name found */
			goto exec_error1;
		}
		interp = i_name = cp;
		i_arg = 0;
		for ( ; *cp && (*cp != ' ') && (*cp != '\t'); cp++) {
 			if (*cp == '/')
				i_name = cp+1;
		}
		if (*cp) {
			*cp++ = '\0';
			i_arg = cp;
		}
		/*
		 * OK, we've parsed out the interpreter name and
		 * (optional) argument.
		 */
		if (sh_bang++ == 0) {
			p = copy_strings(envc, envp, page, p, 0);
			p = copy_strings(--argc, argv+1, page, p, 0);
		}
		/*
		 * Splice in (1) the interpreter's name for argv[0]
		 *           (2) (optional) argument to interpreter
		 *           (3) filename of shell script
		 *
		 * This is done in reverse order, because of how the
		 * user environment and arguments are stored.
		 */
		p = copy_strings(1, &filename, page, p, 1);
		argc++;
		if (i_arg) {
			p = copy_strings(1, &i_arg, page, p, 2);
			argc++;
		}
		p = copy_strings(1, &i_name, page, p, 2);
		argc++;
		if (!p) {
			retval = -ENOMEM;
			goto exec_error1;
		}
		/*
		 * OK, now restart the process with the interpreter's inode.
		 */
		old_fs = get_fs();
		set_fs(get_ds());
		if (!(inode=namei(interp))) { /* get executables inode */
			set_fs(old_fs);
			retval = -ENOENT;
			goto exec_error1;
		}
		set_fs(old_fs);
		goto restart_interp;
	}
	// 释放该缓冲区
	brelse(bh);
	// 对于下列情况，将不执行程序：如果执行文件不是需求页可执行文件(ZMAGIC)、或者代码重定位部分     
	// 长度 a_trsize 不等于 0、或者数据重定位信息长度不等于 0、或者代码段+数据段+堆段长度超过 50MB、    
	// 或者 i 节点表明的该执行文件长度小于代码段+数据段+符号表长度+执行头部分长度的总和。
	if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
		ex.a_text+ex.a_data+ex.a_bss>0x3000000 ||
		inode->i_size < ex.a_text+ex.a_data+ex.a_syms+N_TXTOFF(ex)) {
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 如果执行文件头部长度不等于一个内存块大小(1024)字节，也不能执行。
	if (N_TXTOFF(ex) != BLOCK_SIZE) {
		printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
		retval = -ENOEXEC;
		goto exec_error2;
	}
	// 如果 sh_bang 标志没有设置，则复制指定个数的环境变量字符串和参数到参数和环境空间中。     
	// 若 sh_bang 标志已经设置，则表明是将运行脚本程序，此时环境变量页面已经复制，无须再复制。
	if (!sh_bang) {
		p = copy_strings(envc,envp,page,p,0);
		p = copy_strings(argc,argv,page,p,0);
		// 如果p =0, 则表示环境变量与参数空间页已经被占满，容纳不下了。转制出错处处理
		if (!p) {
			retval = -ENOMEM;
			goto exec_error2;
		}
	}
/* OK, This is the point of no return */
	// 如果原始程序也是一个执行程序，释放其i节点，并让进程excutable 字段指向新程序i节点
	if (current->executable)
		iput(current->executable);
	current->executable = inode;
	// 复位所有信号处理句柄，但对于SIG_IGN 句柄不能复位，因此在 322 与 323 行之间需添加一条     
	// if 语句：if (current->sa[I].sa_handler != SIG_IGN)。这是源代码中的一个bug。
	for (i=0 ; i<32 ; i++)
		current->sigaction[i].sa_handler = NULL;
	// 根据执行时关闭(close_on_exec) 文件句柄位图标志，关闭指定的打开文件，并复位该标志位
	for (i=0 ; i<NR_OPEN ; i++)
		if ((current->close_on_exec>>i)&1)
			sys_close(i);
	current->close_on_exec = 0;
	// 为进程神奇对应的内存管理页面
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	// 清空数学协处理器
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	current->used_math = 0;
	// 根据text修改局部表中描述符基址和段限长，并将参数和环境空间页面放置在数据段末端
	p += change_ldt(ex.a_text,page)-MAX_ARG_PAGES*PAGE_SIZE;
	p = (unsigned long) create_tables((char *)p,argc,envc);
	// 修改当前进程各个字段为新执行程序的信息。令进程代码段尾值字段 end_code = a_text;
	// 令进程数据段字段end_data = a_data + a_text; 令进程堆结尾字段brk = a_text + a_data + a_bss
	current->brk = ex.a_bss +
		(current->end_data = ex.a_data +
		(current->end_code = ex.a_text));
	// 设置进程开始的堆栈指针所在页面，并重新设置进程的有效用户id和有效组id
	current->start_stack = p & 0xfffff000;
	current->euid = e_uid;
	current->egid = e_gid;
	// 初始化一页bss段数据，全为0
	i = ex.a_text+ex.a_data;
	while (i&0xfff)
		put_fs_byte(0,(char *) (i++));
	// 将原调用系统中断的程序在堆栈上的代码指针替换为指向新执行程序的入口点，并将堆栈指针替换     
	// 为新执行程序的堆栈指针。返回指令将弹出这些堆栈数据并使得 CPU 去执行新的执行程序，因此不会     
	// 返回到原调用系统中断的程序中去了。
	eip[0] = ex.a_entry;		/* eip, magic happens :-) */
	eip[3] = p;			/* stack pointer */
	return 0;
	// 错误处理，释放节点和空闲页
exec_error2:
	iput(inode);
exec_error1:
	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(page[i]);
	return(retval);
}
