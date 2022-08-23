/*
 *  linux/fs/buffer.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'buffer.c' implements the buffer-cache functions. Race-conditions have
 * been avoided by NEVER letting a interrupt change a buffer (except for the
 * data, of course), but instead letting the caller do it. NOTE! As interrupts
 * can wake up a caller, some cli-sti sequences are needed to check for
 * sleep-on-calls. These should be extremely quick, though (I hope).
 */

/*
 * NOTE! There is one discordant note here: checking floppies for
 * disk change. This is where it fits best, I think, as it should
 * invalidate changed floppy-disk-caches.
 */
/*      
* 注意！这里有一个程序应不属于这里：检测软盘是否更换。但我想这里是      
* 放置该程序最好的地方了，因为它需要使已更换软盘缓冲失效。      
*/

/**
 * @file buffer.c
 * @brief 高速缓冲区实现函数
 * 'buffer.c'用于实现缓冲区高速缓存功能。通过不让中断过程改变缓冲区，而是让调用者      
 * 来执行，避免了竞争条件（当然除改变数据以外）。注意！由于中断可以唤醒一个调用者，      
 * 因此就需要开关中断指令（cli-sti）序列来检测等待调用返回。但需要非常地快(希望是这样)。
 * @author wangpengcheng  (wangpengcheng2018@gmail.com)
 * @version 1.0
 * @date 2022-08-16 01:09:22
 * @copyright Copyright (c) 2022  IRLSCU
 *
 * @par 修改日志:
 * <table>
 * <tr>
 *    <th> Commit date</th>
 *    <th> Version </th>
 *    <th> Author </th>
 *    <th> Description </th>
 * </tr>
 * <tr>
 *    <td> 2022-08-16 01:09:22 </td>
 *    <td> 1.0 </td>
 *    <td> wangpengcheng </td>
 *    <td> 添加文档注释 </td>
 * </tr>
 * </table>
 */
#include <stdarg.h>
 
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
/**
 * @brief 内核代码末端区域
 */
extern int end;
/**
 * @brief 初始化start 缓冲区
 */
struct buffer_head * start_buffer = (struct buffer_head *) &end;
/**
 * @brief 快速hash表
 */
struct buffer_head * hash_table[NR_HASH];
/**
 * @brief 空闲链表
 */
static struct buffer_head * free_list;
/**
 * @brief 等待缓冲区
 */
static struct task_struct * buffer_wait = NULL;
int NR_BUFFERS = 0;
/**
 * @brief 等待指定缓冲区解锁
 * 在进行buffer查找时，用于等待指定缓冲区解锁
 * @param  bh               缓冲区头部指针
 */
static inline void wait_on_buffer(struct buffer_head * bh)
{
	cli();
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	sti();
}
/**
 * @brief  将数据同步到硬盘
 * https://blog.csdn.net/THEANARKH/article/details/89790332
 * @return int
 */
int sys_sync(void)
{
	int i;
	struct buffer_head * bh;
	// 把所有inode写入buffer，等待回写，见下面代码
	sync_inodes();		/* write out inodes into buffers */
	bh = start_buffer;
    // 扫描所有缓冲区
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		wait_on_buffer(bh);  // 等待缓冲区解锁(如果已经上锁)
		// 块上数据存在修改
        if (bh->b_dirt)
			// 请求底层写硬盘操作，等待底层驱动回写到硬盘，不一定立刻写入
            // 交由对应的硬盘驱动程序实现，处理成功够对buffer进行解锁
			ll_rw_block(WRITE,bh);
	}
	return 0;
}
/**
 * @brief  对指定设备进行高速缓冲数据
 * 与设备上数据的同步操作
 * @param  dev              对应设备编号
 * @return int              最终执行结果
 */
int sync_dev(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
    // 遍历所有缓冲区慢
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		// 查询所有设备相关的bh
        if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
        // 检查到数据，进行写入
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
    // 将i节点数据写入高速缓冲
	sync_inodes();
	bh = start_buffer;
    // 遍历所有缓冲区
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
        // 确认buffer 解锁
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_dirt)
			ll_rw_block(WRITE,bh);
	}
	return 0;
}
/**
 * @brief 设置缓冲区中数据无效
 * @param  dev             清空缓冲区
 */
void inline invalidate_buffers(int dev)
{
	int i;
	struct buffer_head * bh;

	bh = start_buffer;
	for (i=0 ; i<NR_BUFFERS ; i++,bh++) {
		if (bh->b_dev != dev)
			continue;
		wait_on_buffer(bh);
		if (bh->b_dev == dev)
			bh->b_uptodate = bh->b_dirt = 0;
	}
}

/*
 * This routine checks whether a floppy has been changed, and
 * invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 *
 * NOTE! Although currently this is only for floppies, the idea is
 * that any additional removable block-device will use this routine,
 * and that mount/open needn't know that floppies/whatever are
 * special.
 */

/**
 * @brief  检查磁盘是否更换，如果已经更换
 * 就使对应高速缓冲区无效
 * @param  dev           设备编号
 */
void check_disk_change(int dev)
{
	int i;

	if (MAJOR(dev) != 2)
		return;
    // 检查软盘设备是否已经更换
    // 没有则直接退出
	if (!floppy_change(dev & 0x03))
		return;
    // 软盘已经更换，所以释放对应设备的i节点位图和逻辑块位图
    // 占用的高速缓冲区，并设置设备对应的高速缓冲区无效
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_block[i].s_dev == dev)
			put_super(super_block[i].s_dev);
    // 设置inode 无效
	invalidate_inodes(dev);
    // 设置buffer 无效
	invalidate_buffers(dev);
}
/**
 * @brief 定义简单的hash函数
 */
#define _hashfn(dev,block) (((unsigned)(dev^block))%NR_HASH)
/**
 * @brief 获取设备对应的hash表项
 */
#define hash(dev,block) hash_table[_hashfn(dev,block)]
/**
 * @brief  将bh从队列中移除
 * @param  bh               目标缓冲块
 */
static inline void remove_from_queues(struct buffer_head * bh)
{
/* remove from hash-queue */
    // 普通的指针移除步骤
	if (bh->b_next)
		bh->b_next->b_prev = bh->b_prev;
	if (bh->b_prev)
		bh->b_prev->b_next = bh->b_next;
	// 清除hash表
    if (hash(bh->b_dev,bh->b_blocknr) == bh)
		hash(bh->b_dev,bh->b_blocknr) = bh->b_next;
/* remove from free list */
    // 将其从空闲指针处移除
	if (!(bh->b_prev_free) || !(bh->b_next_free))
		panic("Free block list corrupted");
	bh->b_prev_free->b_next_free = bh->b_next_free;
	bh->b_next_free->b_prev_free = bh->b_prev_free;
	if (free_list == bh)
		free_list = bh->b_next_free;
}
/**
 * @brief  将buffer 插入队列中
 * @param  bh               目标缓冲区
 */
static inline void insert_into_queues(struct buffer_head * bh)
{
/* put at end of free list */
	bh->b_next_free = free_list;
	bh->b_prev_free = free_list->b_prev_free;
	free_list->b_prev_free->b_next_free = bh;
	free_list->b_prev_free = bh;
/* put the buffer in new hash-queue if it has a device */
	bh->b_prev = NULL;
	bh->b_next = NULL;
	if (!bh->b_dev)
		return;
	bh->b_next = hash(bh->b_dev,bh->b_blocknr);
	hash(bh->b_dev,bh->b_blocknr) = bh;
	bh->b_next->b_prev = bh;
}
/**
 * @brief  查找对应的设备和块好
 * @param  dev              设备
 * @param  block            块设备文件
 * @return struct buffer_head* 
 */
static struct buffer_head * find_buffer(int dev, int block)
{		
	struct buffer_head * tmp;

	for (tmp = hash(dev,block) ; tmp != NULL ; tmp = tmp->b_next)
		if (tmp->b_dev==dev && tmp->b_blocknr==block)
			return tmp;
	return NULL;
}

/*
 * Why like this, I hear you say... The reason is race-conditions.
 * As we don't lock buffers (unless we are readint them, that is),
 * something might happen to it while we sleep (ie a read-error
 * will force it bad). This shouldn't really happen currently, but
 * the code is ready.
 */
/*      
* 代码为什么会是这样子的？我听见你问... 原因是竞争条件。由于我们没有对      
* 缓冲区上锁（除非我们正在读取它们中的数据），那么当我们（进程）睡眠时      
* 缓冲区可能会发生一些问题（例如一个读错误将导致该缓冲区出错）。目前      
* 这种情况实际上是不会发生的，但处理的代码已经准备好了。      
*/

/**
 * @brief  从hash 表中直接查找设备
 * @param  dev              My Param doc
 * @param  block            My Param doc
 * @return struct buffer_head* 
 */
struct buffer_head * get_hash_table(int dev, int block)
{
	struct buffer_head * bh;

	for (;;) {
        // 查找buffer
		if (!(bh=find_buffer(dev,block)))
			return NULL;
        // 增加引用计数
		bh->b_count++;
        // 等待锁释放
		wait_on_buffer(bh);
		if (bh->b_dev == dev && bh->b_blocknr == block)
			return bh;
        // 如果缓冲区所属设备号或者块号在睡眠时发生了改变
        // 撤销对它的引用计数，重新进行讯号
		bh->b_count--;
	}
}

/*
 * Ok, this is getblk, and it isn't very clear, again to hinder
 * race-conditions. Most of the code is seldom used, (ie repeating),
 * so it should be much more efficient than it looks.
 *
 * The algoritm is changed: hopefully better, and an elusive bug removed.
 */

/**
 * @brief 下面宏定义用于同时判断缓冲区的修改标志和锁定标志，并且定义修改标志的权重要比锁定标志大。
 */
#define BADNESS(bh) (((bh)->b_dirt<<1)+(bh)->b_lock)
/**
 * @brief  查询对应高速缓冲区
 * @param  dev              设备
 * @param  block            块描述符
 * @return struct buffer_head*  最终结果描述符
 */
struct buffer_head * getblk(int dev,int block)
{
	struct buffer_head * tmp, * bh;

repeat:
	if (bh = get_hash_table(dev,block))
		return bh;
	tmp = free_list;
    // 遍历空闲链表
    // 查找目标块
	do {
        //  文件已经被引用，直接跳过
		if (tmp->b_count)
			continue;
        // tmp权重小于bh头标志权重
		if (!bh || BADNESS(tmp)<BADNESS(bh)) {
			// 指向对应设备
            bh = tmp;
            // 没有修改与锁定标志位，直接退出循环
			if (!BADNESS(tmp))
				break;
		}
/* and repeat until we find something good */
	} while ((tmp = tmp->b_next_free) != free_list);
    // 还是没有找到
    // 进程等待，下次再来
	if (!bh) {
		sleep_on(&buffer_wait);
		goto repeat;
	}
	wait_on_buffer(bh);
    // 如果正在被使用
    // 继续查找
	if (bh->b_count)
		goto repeat;
    // 存在脏数据
	while (bh->b_dirt) {
        // 进行写入
		sync_dev(bh->b_dev);
        // 等待锁释放
		wait_on_buffer(bh);
        // 这里再次检查
        // 防止中间被抢占
		if (bh->b_count)
			goto repeat;
	}
/* NOTE!! While we slept waiting for this block, somebody else might */
/* already have added "this" block to the cache. check it */
	if (find_buffer(dev,block))
		goto repeat;
/* OK, FINALLY we know that this buffer is the only one of it's kind, */
/* and that it's unused (b_count=0), unlocked (b_lock=0), and clean */
	// bh查找到了，进行一系列设置
    bh->b_count=1;
	bh->b_dirt=0;
	bh->b_uptodate=0;
    // 将其hash队列和空闲块链表中移出该缓冲区
    // 缓冲区指定设备和其上的指定块
	remove_from_queues(bh);
	bh->b_dev=dev;
	bh->b_blocknr=block;
    // 将buffer 重新插入链表
	insert_into_queues(bh);
	return bh;
}
/**
 * @brief  释放指定的缓冲块
 * @param  buf              缓冲块指针
 */
void brelse(struct buffer_head * buf)
{
	if (!buf)
		return;
	wait_on_buffer(buf);
    // 减少其引用计数
	if (!(buf->b_count--))
		panic("Trying to free free buffer");
	// 唤醒等待中的线程
    wake_up(&buffer_wait);
}

/*
 * bread() reads a specified block and returns the buffer that contains
 * it. It returns NULL if the block was unreadable.
 */

/**
 * @brief  读取其中的数据
 * @param  dev              设备
 * @param  block            块号
 * @return struct buffer_head*  最终的数据缓存指针
 */
struct buffer_head * bread(int dev,int block)
{
	struct buffer_head * bh;
    // 查询blk数据块
	if (!(bh=getblk(dev,block)))
		panic("bread: getblk returned NULL\n");
	if (bh->b_uptodate)
		return bh;
    // 读取对应数据块
	ll_rw_block(READ,bh);
    // 等待读取结束
	wait_on_buffer(bh);
    // 读取成功
	if (bh->b_uptodate)
		return bh;
    // 释放对应缓冲区
	brelse(bh);
	return NULL;
}
/**
 * @brief 复制内存块
 * 从from 地址复制一块到to位置地址
 */
#define COPYBLK(from,to) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"movsl\n\t" \
	::"c" (BLOCK_SIZE/4),"S" (from),"D" (to) \
	:"cx","di","si")

/*
 * bread_page reads four buffers into memory at the desired address. It's
 * a function of its own, as there is some speed to be got by reading them
 * all at the same time, not waiting for one to be read, and then another
 * etc.
 */

/**
 * @brief  读设备上一个页面(4个缓冲块)的内容到指定地址
 * 一次读取4个缓冲块内容到内存指定地址
 * 它是一个完整函数，这样预先读取效率更高
 * @param  address          目标磁盘地址
 * @param  dev              设备名称
 * @param  b                块号数组
 */
void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;
    // 循环执行，每次先读取一页内容
	for (i=0 ; i<4 ; i++){
		if (b[i]) {
            // 查询对应高速缓冲块
			if (bh[i] = getblk(dev,b[i]))
				if (!bh[i]->b_uptodate)
                    // 进行读写操作
					ll_rw_block(READ,bh[i]);
		} else {
			bh[i] = NULL;
        }
    }
    // 遍历进行数据拷贝
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
            // 等待执行完成
			wait_on_buffer(bh[i]);
            // 存在更新，执行数据拷贝
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			// 释放缓冲块
            brelse(bh[i]);
		}
}

/*
 * Ok, breada can be used as bread, but additionally to mark other
 * blocks for reading as well. End the argument list with a negative
 * number.
 */

/**
 * @brief  从指定设备读取指定的一些块
 * @param  dev              设备描述符
 * @param  first            对应的指针
 * @param  ...
 * @return struct buffer_head*  第一块缓冲区头指针
 */
struct buffer_head * breada(int dev,int first, ...)
{
	va_list args;
	struct buffer_head * bh, *tmp;
    // 检查内存空间
	va_start(args,first);
    // 查询高速缓冲块
	if (!(bh=getblk(dev,first)))
		panic("bread: getblk returned NULL\n");
	// 没有读取
    if (!bh->b_uptodate) {
        // 进行读取
        ll_rw_block(READ, bh);
    }
    // 顺序取可变参数表中其它预读块号
    // 
	while ((first=va_arg(args,int))>=0) {
		// 查询对应高速缓冲块
        tmp=getblk(dev,first);
        // 存在对应高速缓冲块
		if (tmp) {
			if (!tmp->b_uptodate)
                // 执行读写操作
				ll_rw_block(READA,bh);
			tmp->b_count--;
		}
	}
	va_end(args);
    // 等待锁释放
	wait_on_buffer(bh);
	if (bh->b_uptodate)
		return bh;
    // 释放对应锁资源
	brelse(bh);
	return (NULL);
}
/**
 * @brief  缓冲区初始化函数，用于系统初始化时进行缓冲区
 * 初始化
 * @param  buffer_end       指定缓冲区内存末端
 */
void buffer_init(long buffer_end)
{
    // 初始化缓冲区
    // 缓冲区头部
	struct buffer_head * h = start_buffer;
	void *b;
	int i;
    // 如果缓冲区高端等于1MB，
	if (buffer_end == 1<<20)
		b = (void *) (640*1024);
	else // 缓冲区大于1Mb
		b = (void *) buffer_end;
    // 循环进行缓冲区的初始化
	while ( (b -= BLOCK_SIZE) >= ((void *) (h+1)) ) {
		h->b_dev = 0;
		h->b_dirt = 0;
		h->b_count = 0;
		h->b_lock = 0;
		h->b_uptodate = 0;
		h->b_wait = NULL;
		h->b_next = NULL;
		h->b_prev = NULL;
		h->b_data = (char *) b;
		h->b_prev_free = h-1;
		h->b_next_free = h+1;
		h++;
		NR_BUFFERS++;
		if (b == (void *) 0x100000)
			b = (void *) 0xA0000;
	}
	h--;  // h指向最后一个有效缓冲头部
	free_list = start_buffer;  // 空闲链表头部指向一个缓冲区头部
	free_list->b_prev_free = h; // 链表头部指向前一项--最后一项
	h->b_next_free = free_list;  // h的下一项指针指向第一项，形成环路
	for (i=0;i<NR_HASH;i++)  // 初始化hash表，设置表中所有指针为NULL
		hash_table[i]=NULL;
}	
