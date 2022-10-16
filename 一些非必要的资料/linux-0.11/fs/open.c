/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

/**
 * @brief  获取文件系统信息--系统调用
 * @param  dev              设备
 * @param  ubuf             ubuf 
 * @return int 
 */
int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}
/**
 * @brief  查询当前的utc时间
 * @param  filename         文件名称
 * @param  times            时间指针，用于存储当前的时间
 * @return int              最终的返回结果
 */
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime,modtime;
    // 查找文件名对应的inode节点
	if (!(inode=namei(filename)))
		return -ENOENT;
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else
		actime = modtime = CURRENT_TIME;
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
    // 释放对应节点
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */

/**
 * @brief  检查对文件的访问权限
 * @param  filename         对应文件名
 * @param  mode             对应的数据屏蔽码
 * @return int              最终执行错误码 
 */
int sys_access(const char * filename, int mode)
{
	struct m_inode * inode;
	int res, i_mode;
	// 清楚高位数据，只要低比特位
	mode &= 0007;
	// 查询文件的inode节点
	if (!(inode=namei(filename)))
		return -EACCES;
	// 获取文件对应的权限属性码
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	// 如果当前进程是文件的宿主
	if (current->uid == inode->i_uid)
		res >>= 6;  // 直接提取宿主属性
	else if (current->gid == inode->i_gid) // 同用户组，提取文件组属性
		res >>= 6;
	if ((res & 0007 & mode) == mode) // 检查是否和目标权限相同
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	// 超级用户 && 屏蔽码为0，或者文件可被任何人访问，直接返回0
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}
/**
 * @brief  改变当前工作目录，相当于cd 命令
 * @param  filename         文件名称
 * @return int 				最终执行结构
 */
int sys_chdir(const char * filename)
{
	struct m_inode * inode;
	// 获取文件inode
	if (!(inode = namei(filename)))
		return -ENOENT;
	// 是否为文件夹，非文件夹直接返回错误
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 设置当店pwd
	iput(current->pwd);
	// 重置当前pwd 
	current->pwd = inode;
	return (0);
}
/**
 * @brief  改变系统目录，将制定的路径名称修改为'/'
 * @param  filename         文件名称
 * @return int 			操作成功返回0，否则返回错误码
 */
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 修改进程root 目录
	iput(current->root);
	current->root = inode;
	return (0);
}
/**
 * @brief  修改文件属性系统调用函数，对应chmod命令
 * @param  filename         文件名称
 * @param  mode             读写模式
 * @return int 
 */
int sys_chmod(const char * filename,int mode)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	// 检查操作权限
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}
/**
 * @brief  修改文件所属用户组
 * @param  filename         文件名称
 * @param  uid              uid--用户
 * @param  gid              gid--用户组id
 * @return int 
 */
int sys_chown(const char * filename,int uid,int gid)
{
	struct m_inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	inode->i_uid=uid;
	inode->i_gid=gid;
	inode->i_dirt=1;
	iput(inode);
	return 0;
}
/**
 * @brief 打开对应文件
 * @param  filename         文件名称
 * @param  flag             标志位
 * @param  mode             模型
 * @return int 				对应的文件句柄编号
 */
int sys_open(const char * filename,int flag,int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i,fd;

	mode &= 0777 & ~current->umask;
	// 找寻空闲的fd
	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)
		return -EINVAL;
	// 设置执行关闭文件句柄位图，复位对应比特位
	current->close_on_exec &= ~(1<<fd);
	// f指针指向文件表数组开始处。搜索空闲文件结构项(句柄引用计数为0的项目)
	// 如果已经没有空闲文件表结构项目，则返回出错码
	f=0+file_table;
	// 找一个空闲的文件句柄
	for (i=0 ; i<NR_FILE ; i++,f++)
		if (!f->f_count) break;
	// 检查文件句柄数是否越界
	if (i>=NR_FILE)
		return -EINVAL;
	// 更新文件句柄，并增加饮用计数
	(current->filp[fd]=f)->f_count++;
	// 获取文件对应的inode
	if ((i=open_namei(filename,flag,mode,&inode))<0) {
		// 失败进行清空
		current->filp[fd]=NULL;
		f->f_count=0;
		return i;
	}
/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	// 如果为字符涉笔
	if (S_ISCHR(inode->i_mode))
		if (MAJOR(inode->i_zone[0])==4) {
			if (current->leader && current->tty<0) {
				// 设置当前进程的tty号为该i节点的子设备号
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
		} else if (MAJOR(inode->i_zone[0])==5) // 设备为5
			// 没有tty 则为错误
			if (current->tty<0) {
				iput(inode);
				current->filp[fd]=NULL;
				f->f_count=0;
				return -EPERM;
			}
/* Likewise with block-devices: check for floppy_change */
	// 为快设备
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);

	// 修改对应文件信息
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}
/**
 * @brief  关闭文件系统调用函数
 * @param  fd            对应的文件句柄
 * @return int 			成功返回0，否则返回错误码
 */
int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	// 复位进程的执行时关闭文件句柄位图对应位。
	current->close_on_exec &= ~(1 << fd);
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL;
	if (filp->f_count == 0)
		panic("Close: file count is 0");
	if (--filp->f_count)
		return (0);
	iput(filp->f_inode);
	return (0);
}
