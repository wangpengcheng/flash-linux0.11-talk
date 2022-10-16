/*
 *  linux/fs/stat.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/stat.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
/**
 * @brief  复制文件状态信息
 * @param  inode           文件inode节点
 * @param  statbuf         文件状态结构指针
 */
static void cp_stat(struct m_inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	int i;
	// 验证内存分配
	verify_area(statbuf,sizeof (* statbuf));
	// 进行节点数据复制
	tmp.st_dev = inode->i_dev;
	tmp.st_ino = inode->i_num;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlinks;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = inode->i_zone[0];
	tmp.st_size = inode->i_size;
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	// 将信息复制到用户缓冲去
	for (i=0 ; i<sizeof (tmp) ; i++)
		put_fs_byte(((char *) &tmp)[i],&((char *) statbuf)[i]);
}
/**
 * @brief  文件状态系统调用函数 -- 根据文件名获取文件状态信息
 * @param  filename         文件名称
 * @param  statbuf          缓冲区指针
 * @return int 				返回最终结果
 */
int sys_stat(char * filename, struct stat * statbuf)
{
	struct m_inode * inode;
	// 根据文件名找出对应i节点，若出错则返回错误码
	if (!(inode=namei(filename)))
		return -ENOENT;
	// 将数据复制到用户缓冲区，释放inode 节点
	cp_stat(inode,statbuf);
	iput(inode);
	return 0;
}
/**
 * @brief  查询文件状态系统调用 -- 根据文件句柄获取文件状态信息
 * @param  fd               指定文件句柄
 * @param  statbuf          数据缓冲指针
 * @return int 
 */
int sys_fstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct m_inode * inode;
	// 检查文件句柄
	if (fd >= NR_OPEN || !(f=current->filp[fd]) || !(inode=f->f_inode))
		return -EBADF;
	// 进行数据拷贝
	cp_stat(inode,statbuf);
	return 0;
}
