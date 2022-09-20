/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>
/**
 * @brief 内存中的i节点表
 */
struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);
/**
 * @brief 等待指定的inode 节点被释放
 * 如果i节点已经被锁定，则将当前任务设置为不可中断
 * 状态，直到该i节点解锁
 * @param  inode            目标inode指针
 */
static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
    // 目标线程进行等待
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}
/**
 * @brief  对指定的i节点上锁(锁定指定的i节点)
 * @param  inode            目标i节点
 */
static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}
/**
 * @brief inode 节点解锁
 * @param  inode           目标inode指针
 */
static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}
/**
 * @brief 释放内存设备dev的所有i节点
 * 扫描内存中的i节点表数组，查询到指定设备使用的
 * i节点就进行释放
 * @param  dev              内存dev
 */
void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;
    // inode 指向表首位
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}
/**
 * @brief 同步inode节点
 * 遍历所有inode 表进行写入
 */
void sync_inodes(void)
{
	int i;
	struct m_inode * inode;
    // 
	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dirt && !inode->i_pipe)
			write_inode(inode);
	}
}
/**
 * @brief  文件数据块映射到盘的处理操作(block 位图处理函数 )
 * 如果创建标志位，则对应逻辑块不存在时就申请新的磁盘块
 * [磁盘块链接](https://blog.csdn.net/u014426028/article/details/105809411?spm=1001.2101.3001.6661.1&utm_medium=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7ERate-1-105809411-blog-83593584.pc_relevant_multi_platform_whitelistv4&depth_1-utm_source=distribute.pc_relevant_t0.none-task-blog-2%7Edefault%7ECTRLIST%7ERate-1-105809411-blog-83593584.pc_relevant_multi_platform_whitelistv4&utm_relevant_index=1)
 * @param  inode            文件inode--节点
 * @param  block            文件中的数据块号
 * @param  create           创建标志
 * @return int              数据块对应设备上的逻辑块号(盘块号)
 */
static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;
    // 如果块小于0,死机
	if (block<0)
		panic("_bmap: block<0");
    // 检查是否大于最大块号
	if (block >= 7+512+512*512)
		panic("_bmap: block>big");
    // 块号小于7 
    // 已经预选初始化了直接创建即可
	if (block<7) {
        // 创建 && 对应块逻辑块字段为0
		if (create && !inode->i_zone[block]) {
            // 进行新块的申请
            if (inode->i_zone[block] = new_block(inode->i_dev))
            {
                // 修改逻辑块时间
                inode->i_ctime = CURRENT_TIME;
                // 修改脏标志位--请求进行同步
                inode->i_dirt = 1;
            }
        }
        // 更新文件系统占用的块号
		return inode->i_zone[block];
	}
    // 先进行求余
    // 块号大于7
	block -= 7;
    // 小于512 为间接转换
	if (block<512) {
        // 是创建，并且对应标志位不存在，进行块创建
		if (create && !inode->i_zone[7])
			if (inode->i_zone[7]=new_block(inode->i_dev)) {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
        // m没有找到磁盘块，直接退出
		if (!inode->i_zone[7])
			return 0;
        // 读取对应磁盘块失败，直接退出
		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;
        // 查询对应磁盘的逻辑块号
		i = ((unsigned short *) (bh->b_data))[block];
		// 不存在，申请对应的设备块
        if (create && !i)
			if (i=new_block(inode->i_dev)) {
                // 更新高速缓冲块的设备编号
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1; 
			}
        // 最后释放该间接块，返回磁盘上新申请的对应block的逻辑块号
		brelse(bh);
		return i;
	}
    // 逻辑块大于512 -- 三级高速缓存
    // 需要进行两次映射
	block -= 512;
    // 寻找二级页表
    // 二级页表不存在进行创建
	if (create && !inode->i_zone[8])
		if (inode->i_zone[8]=new_block(inode->i_dev)) {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
	if (!inode->i_zone[8])
		return 0;
    // 读取二级块的一级页表
	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;
    // 查询对应的磁盘块号
	i = ((unsigned short *)bh->b_data)[block>>9];
    // 如果是创建并且二次间接块的一级块上第(block/512)项中的逻辑块号为 0 的话，则需申请一磁盘     
    // 块（逻辑块）作为二次间接块的二级块，并让二次间接块的一级块中第(block/512)项等于该二级     
    // 块的块号。然后置位二次间接块的一级块已修改标志。并释放二次间接块的一级块。
    if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block>>9]=i;
			bh->b_dirt=1;
		}
    // 清空临时块
	brelse(bh);
    // 二次间接块的二级块号为0，标志失败
	if (!i)
		return 0;
    // 检查读写
	if (!(bh=bread(inode->i_dev,i)))
		return 0;
    // 二级块对应的逻辑块号
	i = ((unsigned short *)bh->b_data)[block&511];
    // 为0，表示三级块不存在，需要进行创建
	if (create && !i)
		if (i=new_block(inode->i_dev)) {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
    // 释放临时指针
	brelse(bh);
    // 返回逻辑块号
	return i;
}
/**
 * @brief  根据i节点信息获取文件数据块block 
 * 在设备上对应的逻辑块号
 * @param  inode            对应的文件系统inode
 * @param  block            磁盘块编号
 * @return int              对应的逻辑块号
 */
int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}
/**
 * @brief Create a block object 
 * @param  inode            inode 指针
 * @param  block            文件数据块
 * @return int              对应的逻辑块号
 */
int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}
/**
 * @brief  释放一个inode节点(回写入设备)
 * @param  inode            需要释放的inode指针
 */
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count)
		panic("iput: trying to free free inode");
	// 存在管道使用
    if (inode->i_pipe) {
		// 唤醒等待中的进程
        wake_up(&inode->i_wait);
        // 任然存在引用，直接返回
		if (--inode->i_count)
			return;
        // 释放对应的内存分页
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}
    // 设备为0，减少引用
	if (!inode->i_dev) {
		inode->i_count--;
		return;
	}
    // 块设备，先刷新对应设备
    // 再进行释放
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
    // 存在软链接引用
	if (!inode->i_nlinks) {
        // 释放对应的i节点逻辑块
		truncate(inode);
		free_inode(inode);
		return;
	}
    // 节点已经修改，更新对应节点
    // 等待对应节点解锁
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
    // 节点引用计数-1
	inode->i_count--;
	return;
}
/**
 * @brief 从i节点表(inode_table)中获取一个空闲i节点项
 * 寻找引用计数count 为 0 的 i 节点，并将其写盘后清零
 * 返回其指针
 * @return struct m_inode* 对应的inode节点指针 
 */
struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;
	int i;
    // 循环遍历进行处理
	do {
		inode = NULL;
        // 遍历表节点
		for (i = NR_INODE; i ; i--) {
			if (++last_inode >= inode_table + NR_INODE)
				last_inode = inode_table;
			// 没有被引用
            if (!last_inode->i_count) {
				inode = last_inode;
                // 没有被污染，没有被锁
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}
        // node未找到
        // 直接报空错误
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}
        // 等待inode对应的锁释放
		wait_on_inode(inode);
        // 如果数据存在变更
        // 进行相关写入
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count);
    // 重置inode
	memset(inode,0,sizeof(*inode));
    // 设置引用计数++
	inode->i_count = 1;
    // 返回数据指针
	return inode;
}
/**
 * @brief 创建管道inode
 * @return struct m_inode* 获取的管道inode 指针 
 */
struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;
    // 查询空闲inode
	if (!(inode = get_empty_inode()))
		return NULL;
    // 查询空闲分页
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
    // 创建引用计数
	inode->i_count = 2;	/* sum of readers/writers */
    // 初始化指针
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	// pip 初始化为1
    inode->i_pipe = 1;
	return inode;
}
/**
 * @brief  从设备上读取指定节点号的i节点
 * @param  dev              目标设备名称
 * @param  nr               i节点号
 * @return struct m_inode*  最终的inode 指针
 */
struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");
    // 获取一个空闲i 节点
	empty = get_empty_inode();
	inode = inode_table;
    // 遍历i 节点列表，找寻目标设备上的节点
	while (inode < NR_INODE+inode_table) {
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
		wait_on_inode(inode);
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table;
			continue;
		}
		inode->i_count++;
        // 存在挂载点，该i节点是其它文件系统的安装点
        // 在超级块中搜寻安装在此i节点超级块，释放空闲节点，返回i节点指针
		if (inode->i_mount) {
			int i;
            // 找寻对应超级块挂载点
			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;
            // 如果大于了索引，新增inode缓冲区数量
			if (i >= NR_SUPER) {
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
            // 将该节点写盘，从安装在此i节点文件系统的超级块上
            // 读取设备号，并令i节点号为1，重新扫描，查询对应根节点
			iput(inode);
            // 查询设备号
			dev = super_block[i].s_dev;
            // 设备节点编号
			nr = ROOT_INO;
            // 重新设置inode节点
			inode = inode_table;
			continue;
		}
        // 空闲链表存在，直接放入
		if (empty)
			iput(empty);
		return inode;
	}
    // 没有找到指定的i节点，则进行创建
	if (!empty)
		return (NULL);
	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;
    // 进行读写测试
	read_inode(inode);
    // 返回文件i节点
	return inode;
}
/**
 * @brief   从设备上读取指定i节点的信息到内存(缓冲区)中
 * @param  inode            目标inode节点
 */
static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
    // 查询对应的文件系统
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");
    // 计算对应的逻辑块编号= (启动块 + 超级块) + i 节点位图占用块数 + 逻辑块位图占用块数 + (i 节点号 - 1) / 每块含有的i节点数目
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
    // 尝试进行数据读取
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
	// 将node指针指向对应i节点信息
    *(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];
    // 释放bh指针
	brelse(bh);
    // 解锁inode 信息
	unlock_inode(inode);
}
/**
 * @brief  指定i节点信息写入设备
 * 写入缓冲区对应的缓冲块中，待缓冲区刷新时会写入盘中
 * @param  inode            对应的数据节点
 */
static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
    // 节点没有被修改过，直接解锁并释放
	if (!inode->i_dirt || !inode->i_dev) {
		unlock_inode(inode);
		return;
	}
    // 查询设备对应文件系统
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");
    // 计算对应的逻辑块编号
	block = 2 + sb->s_imap_blocks + sb->s_zmap_blocks +
		(inode->i_num-1)/INODES_PER_BLOCK;
    // 读取节点对应的逻辑块
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
    // 将节点信息复制到对应的i节点项中
	((struct d_inode *)bh->b_data)
		[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;
	// 重置缓冲区标志位，解锁该缓冲区
    bh->b_dirt=1;
	inode->i_dirt=0;
	brelse(bh);
	unlock_inode(inode);
}
