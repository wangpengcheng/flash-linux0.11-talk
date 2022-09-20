/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })
/**
 * @brief 超级块结构数组
 */
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
/**
 * @brief ROOT_DEV 已经在init/main.c 中被初始化
 */
int ROOT_DEV = 0;
/**
 * @brief  锁定指定的超级块
 * @param  sb               超级块指针
 */
static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}
/**
 * @brief  解锁超级块
 * @param  sb               超级块指针
 */
static void free_super(struct super_block * sb)
{
	cli();  // 关闭中断
	sb->s_lock = 0; // 复位锁定标志
	wake_up(&(sb->s_wait)); // 唤醒等待该超级块的进程
	sti(); // 开启中断
}
/**
 * @brief  等待超级块解锁
 * @param  sb               超级块指针
 */
static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}
/**
 * @brief  获取指定设备的超级块，返回该超级块结构指针
 * @param  dev              设备编号
 * @return struct super_block*  对应的超级块结构指针
 */
struct super_block * get_super(int dev)
{
	struct super_block * s;
	// 没有指定设备，返回空指针
	if (!dev)
		return NULL;
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;
		} else
			s++;
	return NULL;
}

void put_super(int dev)
{
	struct super_block * sb;
	struct m_inode * inode;
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}
/**
 * @brief  从设备上读取超级块到缓冲区中
 * 如果该设备的超级块已经在高速缓冲中并且有效，则直接返回该超级块的指针。
 * @param  dev              对应的设备超级块
 * @return struct super_block* 返回超级块指针
 */
static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;
	// 如果没有指明设备，返回空指针
	if (!dev)
		return NULL;
	// 检查设备是否更换过盘片(也即是否为软盘设备)，如果更换过盘
	// 则高速缓冲区有关该设备的所有缓冲块均失效，需要进行失效处理(释放原来加载的文件系统)
	check_disk_change(dev);
	// 查找对应超级块
	if (s = get_super(dev))
		return s;
	for (s = 0+super_block ;; s++) {
		if (s >= NR_SUPER+super_block)
			return NULL;
		if (!s->s_dev)
			break;
	}
	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;
	lock_super(s);
	if (!(bh = bread(dev,1))) {
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
    // 复制磁盘对应的超级块信息
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	// 释放对应缓冲块
    brelse(bh);
    // 检查超级块对应的文件系统模数字段是否正确，不正确直接进行释放并退出
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}
    // 初始化内存超级块结构中的位图空间
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL;
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL;
    // 从设备上读取i节点位图和逻辑位图信息，放在超级块对应字段中
	block=2;
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if (s->s_imap[i]=bread(dev,block))
			block++;
		else
			break;
	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if (s->s_zmap[i]=bread(dev,block))
			block++;
		else
			break;
    // 检查最终数目是否正确，不正确表示位图信息有问题，需要进行释放
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
    // 将0号节点设置为1 防止文件系统分配节点
	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;
	// 解锁超级块
    free_super(s);
	return s;
}
/**
 * @brief  卸载对应的设备
 * @param  dev_name         设备名称
 * @return int              最终操作结果
 */
int sys_umount(char * dev_name)
{
    // 定义相关临时变量
	struct m_inode * inode;
	struct super_block * sb;
	int dev;
    // 查询设备对应的inode节点
	if (!(inode=namei(dev_name)))
		return -ENOENT;
    // 获取其对应的设备号
	dev = inode->i_zone[0];
    // 检查是否为块设备
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode); // 非块设备释放inode
		return -ENOTBLK;
	}
    // 释放设备文件名节点--已经获取到了其设备号
	iput(inode);
    // 根设备不允许卸载
	if (dev==ROOT_DEV)
		return -EBUSY;
    // 无挂载点，无安装节点直接返回
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
    // 无安装标志，显示告警信息
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
    // 确认已经没有进程在使用设备
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
    // 设置超级块中被安装i节点字段为空，释放设备文件系统的根i节点
    // 重置超级块中被安装系统根i节点指针为空
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
    // 释放设备超级块，执行设备上数据同步操作
	put_super(dev);
	sync_dev(dev);
	return 0;
}
/**
 * @brief  安装对应的文件系统调用函数
 * @param  dev_name         设备名称
 * @param  dir_name         设备对应的目标文件夹名称
 * @param  rw_flag          设备读写标志
 * @return int 
 */
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}
/**
 * @brief  挂载root文件系统
 */
void mount_root(void)
{
    // 定义相关辅助数据指针
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");
    // 初始化文件系统表
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
    // 发现软盘请求回车
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
        wait_for_keypress();
	}
    // 初始化所有超级块文件系统
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}
    // 超级文件系统读取
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");
    // 设置引用计数为4
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	// 设置当前进程的工作目录和根目录i节点，当前进程是1号进程
    p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
    // 统计设备上空闲块数。令i等于超级块中表明的设备逻辑块总数
	free=0;
	i=p->s_nzones;
    // 统计空闲逻辑块数目
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	printk("%d/%d free blocks\n\r",free,p->s_nzones);
    // 统计设备上空闲 i 节点数。首先令 i 等于超级块中表明的设备上 i 节点总数+1。
    // 加 1 是将 0 节点也统计进去。
    free=0;
	i=p->s_ninodes+1;
    // 统计inode 节点总数
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
