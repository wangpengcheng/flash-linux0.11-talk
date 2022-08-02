/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

/**
 * @brief  退出
 * @param  error_code       相关退出码
 * @return volatile 
 */
volatile void do_exit(int error_code);
/**
 * @brief  获取当前任务信号屏蔽位图(屏蔽码)
 * @return int 
 */
int sys_sgetmask()
{
	return current->blocked;
}
/**
 * @brief  设置新的信号屏蔽码
 * @param  newmask          目标信号屏蔽码
 * @return int  原始的信号屏蔽码
 */
int sys_ssetmask(int newmask)
{
	int old=current->blocked;
	// 这里使用位运算，不允许屏蔽SIGKILL 信号
	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}
/**
 * @brief  复制信号处理具柄，fs数据段到to处
 * @param  from             原始的fs数据段地址
 * @param  to              	目标数据段地址
 */
static inline void save_old(char * from,char * to)
{
	int i;
	// 检查目标位置容量是否足够
	verify_area(to, sizeof(struct sigaction));
	// 进行内存拷贝
	// 这里应该直接使用memcpoy
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}
/**
 * @brief  信号处理系统调用
 * 
 * @param  signum           指定信号
 * @param  handler          对应处理函数
 * @param  restorer         恢复函数指针--libc库提供
 * @return int   原始函数处理具柄
 */
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;
	// 进行目标信号校验
	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	// 设置函数
	tmp.sa_handler = (void (*)(int)) handler;
	// 执行时信号掩码
	tmp.sa_mask = 0;
	// 具柄使用一次后恢复到默认值，允许信号在自己的处理具柄中收到
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	// 设置信号堆栈恢复函数
	tmp.sa_restorer = (void (*)(void)) restorer;
	// 重新设置handler地址
	handler = (long) current->sigaction[signum-1].sa_handler;
	// 设置信号描述符号
	current->sigaction[signum-1] = tmp;
	// 返回处理具柄
	return handler;
}

/**
 * @brief  系统调用，改变进程在收到一个信号时的操作
 * @param  signum           信号
 * @param  action           新操作
 * @param  oldaction        旧操作
 * @return int    最终设置结果，成功返回0，否则为-1
 */
int sys_sigaction(
	int signum, 
	const struct sigaction * action,
	struct sigaction * oldaction
)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	// 获取当前信号行为
	tmp = current->sigaction[signum-1];
	// 将处理具柄，设置到新的地址
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	// 如果存在oldaction指针不为空
	// 将其保存到oldaction所指的位置
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	// 如果允许信号在自己的信号具柄中收到
	// 则令屏蔽码为0，否则设置屏蔽本信息
	// 执行过程中，不接受新的信号具柄
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}
/**
 * @brief  系统调用中断处理程序中真正的信号处理程序
 * 该代码的主要作用是将信号的处理具柄插入到用户程序堆栈中
 * 并在本系统调用结束返回后立刻执行具柄程序
 * 然后继续执行用户的程序
 * @param  signr            My Param doc
 * @param  eax              My Param doc
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
 */
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	// 旧eip地址
	long old_eip=eip;
	// 获取当前描述符
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;
	// 获取当前信号处理具柄
	sa_handler = (unsigned long) sa->sa_handler;
	// 信号忽略，直接返回
	if (sa_handler==1)
		return;
	// 如果是默认处理具柄
	if (!sa_handler) {
		// 直接返回
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));  // 执行退出
	}
	// 如果函数只执行一次
	if (sa->sa_flags & SA_ONESHOT){
		// 设置处理具柄为null
		sa->sa_handler = NULL;
	}
	/**
	 * @brief 将设置的信号处理具柄插入到用户堆栈中
	 *  sa_restorer,signr,进程屏蔽码寄存器作为参数以及原调用系统调用
	 * 到程序返回指针及标志寄存器值继续压入堆栈
	 * 本次系统调用中断返回用户程序时会首先执行用户的信号具柄程序
	 * 然后再继续执行用户程序
	 */
	// 将用户调用的系统调用的代码指针eip指向该信号处理具柄
	*(&eip) = sa_handler;
	// 如果允许信号自己的处理具柄收到信号自己
	// 需要将进程的阻塞码压入堆栈
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	// 将原调用程序的用户堆栈指针向下扩展 7（或 8）个长字（用来存放调用信号句柄的参数等）
	// 并检查内存使用情况（例如如果内存超界则分配新页等）。
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	// 在用户堆栈中从下到上存放
	// - sa_restorer
	// - signr 信号
	// - blocked(如果 SA_NOMASK位置)
	// 相关寄存器指针 
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	// 进程阻塞码(屏蔽码)添加上sa_mask中的码位
	current->blocked |= sa->sa_mask;
}
