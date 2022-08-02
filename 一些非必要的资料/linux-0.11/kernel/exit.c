/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

/**
 * @brief  程序暂时挂起，让进程为睡眠状态的系统调用
 * @return int  函数结果
 */
int sys_pause(void);
/**
 * @brief  关闭指定fd，fd 为系统进程号
 * @param  fd               My Param doc
 * @return int 
 */
int sys_close(int fd);
/**
 * @brief  释放指定进程占用的任务槽及其任务数据结构占用的内存
 * @param  p                是任务数据结构的指针。该函数在后面的 sys_kill()和 sys_waitpid()中被调用。
 * 扫描任务指针数组表 task[]以寻找指定的任务。如果找到，则首先清空该任务槽，
 * 然后释放该任务数据结构所占用的内存页面，最后执行调度函数并在返回时立即退出。如果在任务数组
 * 表中没有找到指定任务对应的项，则内核 panic☺
 */
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	// 找到对应任务
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			// 进行重置
			task[i]=NULL;
			free_page((long)p);
			// 重新调度，防止掉链
			schedule();
			return;
		}
	panic("trying to release non-existent task");
}
/**
 * @brief  向进程发送指定新信号
 * @param  sig              信号值
 * @param  p                进程指针
 * @param  priv             强制信号标志，即不需要考虑进程用户属性或级别而能发送信号的权利
 * @return int 
 */
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	// 如果强制发送标志置位，或者当前进程的有效用户标识符(euid)就是指定进程的 euid（也即是自己），    
	// 或者当前进程是超级用户，则在进程位图中添加该信号，否则出错退出。     
	// 其中 suser()定义为(current->euid==0)，用于判断是否是超级用户。
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

/// 终止会话(session)。     
// 进程会话的概念请参见第 4 章中有关进程组和会话的说明。
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1); // 设置挂断进程信号
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	// 获取当前执行任务
	struct task_struct **p = NR_TASKS + task;
	// 
	int err, retval = 0;
	// pid 为0 -- 结束当前进程组中的所有进程
	if (!pid) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pgrp == current->pid) 
			if (err=send_sig(sig,*p,1))  // 强制发送信号
				retval = err;
	// pid > 0 找到并干掉
	} else if (pid>0) while (--p > &FIRST_TASK) {
		if (*p && (*p)->pid == pid) 
			if (err=send_sig(sig,*p,0))
				retval = err;
	// 果 pid=-1，则信号 sig 就会发送给除第一个进程外的所有进程。
	} else if (pid == -1) while (--p > &FIRST_TASK)
		if (err = send_sig(sig,*p,0))
			retval = err;
	// < -1 干掉进程组-pid的所有进程
	else while (--p > &FIRST_TASK)
		if (*p && (*p)->pgrp == -pid)
			if (err = send_sig(sig,*p,0))
				retval = err;
	return retval;
}
/**
 * @brief 通知父进程
 * - 向进程 pid 发送信号 SIGCHLD：默认情况下子进程将停止或终止。
 * 如果没有找到父进程，则自己释放。但根据POSIX.1 要求，若父进程已先行终止
 * 则子进程应该被初始进程 1 收容
 * @param  pid              pid 
 */
static void tell_father(int pid)
{
	int i;
	// 扫描进程数组表
	// 寻找指定进程pid
	// 并向其发送子进程将其停止或终止信号
	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	// 如果没有找到父进程，则进程就自己释放。这样做并不好，必须改成由进程 1 充当其父进程。
	printk("BAD BAD - no father found\n\r");
	// 如果没有找到父进程，就自己释放
	release(current);  
}

/**
 * @brief  程序退出处理程序
 * @param  code             code错误码
 * @return int 
 */
int do_exit(long code)
{
	int i;
	// 释放当前进程代码段和数据段所占用的内存页
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	// 发送所有子进程终止信号
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
	// 关闭当前进程打开所有文件
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			// 进行文件关闭
			sys_close(i);
	// 对当前进程的工作目录 pwd、根目录 root 以及程序的 i 节点进行同步操作，并分别置空（释放）
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	// 释放终端
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	// 如果当前进程上次使用过协处理器，则将last_task_used_math 置空
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	// 如果当前进程是leader进程，则终止该会话的所有相关进程
	if (current->leader)
		kill_session();
	// 将当前进程设置为僵死状态，表明当前进程已经释放了资源
	// 并保存将由父进程读取的退出码
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	// 告知父进程子进程已经释放
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}
/**
 * @brief 系统调用waitpid() 挂起当前进程
 * 直到 pid 指定的子进程退出（终止）或者收到要求终止
 * 该进程的信号，或者是需要调用一个信号句柄（信号处理程序）。
 * 如果 pid 所指的子进程早已退出（已成所谓的僵死进程），则本调用将立刻返回。子进程使用的所有资源将释放。
 * 如果 pid > 0, 表示等待进程号等于 pid 的子进程。
 * 如果 pid = 0, 表示等待进程组号等于当前进程组号的任何子进程。
 * 如果 pid < -1, 表示等待进程组号等于 pid 绝对值的任何子进程。
 * 如果 pid = -1, 表示等待任何子进程。]
 * 如果options = WUNTRACED，表示如果子进程是停止的，也马上返回（无须跟踪）。
 * 若 options = WNOHANG，表示如果没有子进程退出或终止就马上返回。
 * 如果返回状态指针 stat_addr 不为空，则就将状态信息保存到那里。
 * @param  pid              进程pid
 * @param  stat_addr        状态地址指针
 * @param  options          相关参数选项
 * @return int  函数指针
 */
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	// 从任务数组末端开始扫描所有任务，跳过空项、本进程项以及非当前进程的子进程项。
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current) // 跳过空项和本进程项
			continue;
		if ((*p)->father != current->pid)  // 如果不是当前进程的子进程则跳过
			continue;
		/**
		 * @brief 此时扫描选择到的进程 p 肯定是当前进程的子进程。
		 *  如果等待的子进程号 pid>0，但与被扫描子进程 p 的 pid不相等，说明它是当前进程另外的子进程，
		 *  于是跳过该进程，接着扫描下一个进程。
		 */
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
		/**
		 * @brief 否则，如果指定等待进程的 pid=0,表示正在等待进程组号
		 * 等于当前进程组号的任何子进程
		 * 如果此时被扫描进程p的进程组号与当前进程的组号不等，则跳过。  
		 */
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
			/**
			 * @brief 否则，如果指定的 pid<-1，表示正在等待进程组号等于 pid 绝对值的任何子进程。如果此时被扫描
			 * 进程 p 的组号与 pid 的绝对值不等，则跳过。
			 */
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
		/**
		 * @brief 如果前 3 个对 pid 的判断都不符合，则表示当前进程正在等待其任何子进程，也即pid=-1 的情况。
		 * 此时所选择到的进程 p 正是所等待的子进程。接下来根据这个子进程 p 所处的状态来处理。
		 */
		switch ((*p)->state) {
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
		/**
		 * @brief 如果子进行程p处于僵死状态，则首先把它在用户态和内核态运行的时间
		 * 分别累计到当前进程(父进程)中，然后取出子进程的pid和退出码
		 * 并释放该子进程。最后返回子进程的退出码和pid
		 */
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;  // 临时保存子进程pid
				code = (*p)->exit_code;
				release(*p); // 释放该子进程
				put_fs_long(code,stat_addr); // 设置状态信息为退出码值
				return flag;  // 退出，返回子进程的pid
			// 如果这个子进程 p 的状态既不是停止也不是僵死，那么就置flag=1。表示找到过一个符合要求的     
			// 子进程，但是它处于运行态或睡眠态。
			default:
				flag=1;
				continue;
		}
	}
	// 在上面对任务数组扫描结束后，如果 flag 被置位，说明有符合等待要求的子进程并没有处于退出 
	// 或僵死状态。如果此时已设置WNOHANG 选项（表示若没有子进程处于退出或终止态就立刻返回），     
	// 就立刻返回 0，退出。否则把当前进程置为可中断等待状态并重新执行调度。
		if (flag)
	{
		if (options & WNOHANG) // 如果 options = NOHANG，则立刻返回
			return 0;
		current->state = TASK_INTERRUPTIBLE; // 置当前进程为可中断等待状态。
		schedule(); // 重新调度
		// 当又开始执行本进程时，如果本进程没有收到除 SIGCHLD以外的信号，则还是重复处理。
		// 否则返回出错码并退出。
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;  // 退出，返回出错码
	}
	// 如果没有找到符合要求的子进程，则返回出错码
	return -ECHILD;
}


