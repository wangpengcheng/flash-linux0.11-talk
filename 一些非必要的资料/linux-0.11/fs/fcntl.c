/*
 *  linux/fs/fcntl.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <fcntl.h>
#include <sys/stat.h>

extern int sys_close(int fd);
/**
 * @brief  复制文件句柄(描述符)
 * @param  fd               复制到文件句柄
 * @param  arg              相关指定参数
 * @return int 
 */
static int dupfd(unsigned int fd, unsigned int arg)
{
	// 文件句柄值参数
	if (fd >= NR_OPEN || !current->filp[fd])
		return -EBADF;
	if (arg >= NR_OPEN)
		return -EINVAL;
	while (arg < NR_OPEN)
		if (current->filp[arg])
			arg++;
		else
			break;
	if (arg >= NR_OPEN)
		return -EMFILE;
	// 执行时关闭标志位图中复位句柄
	current->close_on_exec &= ~(1<<arg);
	// 文件结构指针等于原句柄fd的指针，并将文件引用技术+1
	(current->filp[arg] = current->filp[fd])->f_count++;
	return arg;
}

int sys_dup2(unsigned int oldfd, unsigned int newfd)
{
	sys_close(newfd);
	return dupfd(oldfd,newfd);
}

int sys_dup(unsigned int fildes)
{
	return dupfd(fildes,0);
}
/**
 * @brief  文件控制系统调用函数
 * @param  fd              文件句柄
 * @param  cmd             cmd时操作命令
 * @param  arg             相关参数
 * @return int 
 */
int sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{	
	struct file * filp;

	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;
	switch (cmd) {
		case F_DUPFD:   // 复制文件句柄
			return dupfd(fd,arg);
		case F_GETFD:   // 获取文件句柄的执行关闭标志
			return (current->close_on_exec>>fd)&1;
		case F_SETFD:   // 设置句柄执行时关闭标志，arg 位 0 位置是设置，否则关闭
			if (arg&1)
				current->close_on_exec |= (1<<fd);
			else
				current->close_on_exec &= ~(1<<fd);
			return 0;
		case F_GETFL:
			return filp->f_flags;
		case F_SETFL: // 设置文件状态和访问模式(根据 arg 设置添加、非阻塞标志)。
			filp->f_flags &= ~(O_APPEND | O_NONBLOCK);
			filp->f_flags |= arg & (O_APPEND | O_NONBLOCK);
			return 0;
		case F_GETLK:	case F_SETLK:	case F_SETLKW:
			return -1;
		default:
			return -1;
	}
}
