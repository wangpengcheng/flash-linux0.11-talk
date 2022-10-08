/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>
/**
 * @brief  进行管道文件的读写操作
 * @param  inode            目标inode节点
 * @param  buf              对应的目标缓冲buffer
 * @param  count            数据长度描述
 * @return int              最终读取的字节长度
 */
int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	while (count>0) {
        // 管道长度为0
		while (!(size=PIPE_SIZE(*inode))) {
            // 唤醒等待中的写入进程
			wake_up(&inode->i_wait);
			if (inode->i_count != 2) /* are there any writers? */
				return read;
			sleep_on(&inode->i_wait);
		}
        // 检查是否需要分页
		chars = PAGE_SIZE-PIPE_TAIL(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		read += chars;
		size = PIPE_TAIL(*inode);
		PIPE_TAIL(*inode) += chars;
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		while (chars-->0){
            // 将对应文件放入到用户空间buffer中
            // 对应管道i节点，其i_size 字段中是管道缓冲块指针
            put_fs_byte(((char *)inode->i_size)[size++], buf++);
        }
	}
    // 唤醒等待中的进程
	wake_up(&inode->i_wait);
	return read;
}
/**
 * @brief  管道写入函数
 * 基本和读取函数一致，不过转换为了写入
 * @param  inode            对应的inode 数据节点
 * @param  buf              目标缓冲区buf
 * @param  count            目标数据长度
 * @return int              最终返回结果
 */
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(&inode->i_wait);
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			sleep_on(&inode->i_wait);
		}
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(&inode->i_wait);
	return written;
}
/**
 * @brief  系统调用函数，创建pipe
 * 在 fildes所指的数组中创建一对文件句柄(描述符)。这对文件句柄指向一管道 i节点。
 * fildes[0] 用于读管道中数据，fildes[1]用于向管道中写入数据。
 * @param  fildes   对应的管道数组
 * @return int      功时返回 0，出错时返回-1
 */
int sys_pipe(unsigned long * fildes)
{
    // 相关数据指针
	struct m_inode * inode;
	struct file * f[2];
	int fd[2];
    // 文件表遍历数据指针
	int i,j;

	j=0;
    // 从系统文件中取两个空闲项，并分别设置引用计数为1
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;
    // 只有一个空闲项，释放该项(引用计数复位)
	if (j==1)
		f[0]->f_count=0;
	// 没有空闲项，直接返回-1
    if (j<2)
		return -1;
	j=0;
    // 针对上面取得的两个文件结构项，分别配置一文件句柄
    // 并使进程的文件结构之战分别指向这两个文件结构
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)
		current->filp[fd[0]]=NULL;
    // 没有空闲句柄，则释放上面获取的两个文件结构项(复位引用计数值)，并返回-1
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
    // 查询对应的inode 节点
	if (!(inode=get_pipe_inode())) {
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);
	put_fs_long(fd[1],1+fildes);
	return 0;
}
