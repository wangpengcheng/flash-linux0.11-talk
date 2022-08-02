/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/**
 * @brief 执行低层块设备读/写操作
 * 为块设备创建读写请求
 * 为硬盘软盘设备进行统一抽象
 */

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */

/**
 * @brief 请求队列列表
 * 最长为32个
 */
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */

/**
 * @brief 等待进程
 * 当无空闲进程项目时进行等待
 * 这里只有一个，多个等待会出现问题？？？？
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */


/**
 * @brief 块设备结构描述符
 */
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};
/**
 * @brief 对缓冲区进行锁定
 * @param  bh               缓冲区指针
 */
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();
	// 等待锁的释放
	// 感觉这里有死锁问题
	while (bh->b_lock)
		sleep_on(&bh->b_wait);
	bh->b_lock=1;
	sti();
}
/**
 * @brief 缓冲区进行解锁
 * @param  bh               缓冲区指针
 */
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;
	wake_up(&bh->b_wait);
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */

/**
 * @brief 将文件请求，添加到队列中
 * @param  dev              设备描述符，主要是请求处理函数
 * @param  req              指定相关请求
 */
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	// 临时指针用于指向链表
	struct request * tmp;
	// 
	req->next = NULL;
	cli();
	// 存在缓冲区
	if (req->bh)
		req->bh->b_dirt = 0;   // 设置缓冲区为clean
	// dev 当前请求为null
	// 设置tmp
	if (!(tmp = dev->current_request)) {
		dev->current_request = req;
		sti();
		// 执行设备请求处理函数
		(dev->request_fn)();
		return;
	}
	// dev 当前处理不为null
	// 将req 添加到队列中
	// 使用电梯算法搜索插入最佳位置
	for ( ; tmp->next ; tmp=tmp->next)
		// 进行队列添加 
		if ((IN_ORDER(tmp,req) ||
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;
	sti();
}
/**
 * @brief  创建请求项目并插入请求队列
 * 
 * @param  major            主设备号
 * @param  rw               读写指令
 * @param  bh               存放数据的缓冲区头指针bh
 */
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
	// 确认是读写操作
	if (rw_ahead = (rw == READA || rw == WRITEA)) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	// 缓冲区上锁
	lock_buffer(bh);
	// 如果命令是写并且缓冲区数据不脏（没有被修改过），或者命令是读并且缓冲区数据是更新过的，     
	// 则不用添加这个请求。将缓冲区解锁并退出。
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
/*
请求项是从请求数组末尾开始搜索空项填入的。根据上述要求，对于读命令请求，
可以直接从队列末尾开始操作，而写请求则只能从队列 2/3 处向队列头处搜索空项填入。
*/
if (rw == READ)
	req = request + NR_REQUEST;  // 对于读请求，将队列指针指向队列尾部
else
	req = request + ((NR_REQUEST * 2) / 3); // 对于写请求，队列指针指向队列 2/3 处。
/* find an empty request */
// 从后向前搜索，发现dev < 0，表示request 空闲
while (--req >= request)
	if (req->dev < 0)
		break;
/* if none found, sleep on new requests: check for rw_ahead */
	// 如果还是没有空闲
	// 持续进行等待
	if (req < request) {
		// 如果是预先读取，则解锁缓冲区，直接退出
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request); // 进行睡眠，之后再次进行等待
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
	// 对找到的空闲请求，进行数据写入
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr << 1; // 起始扇区。块号转换成扇区号(1 块=2 扇区)。
	req->nr_sectors = 2;   //< 读写扇区数亩
	req->buffer = bh->b_data;  //< 数据缓冲区
	req->waiting = NULL;	   // 务等待操作执行完成的地方。
	req->bh = bh;  // 缓冲区
	req->next = NULL;
	add_request(major+blk_dev,req);  // 将其添加到请求队列中
}

/**
 * @brief  读写块函数
 * 块设备与系统与其它部分到接口函数
 * @param  rw               读写指令
 * @param  bh               缓冲区指针
 */
void ll_rw_block(int rw, struct buffer_head * bh)
{
	// 主设备号
	unsigned int major;

	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	// 创建读写请求
	make_request(major,rw,bh);
}
/**
 * @brief 块设备初始化
 * 由初始化程序 main.c 调用（init/main.c,128）
 * 初始化请求数组，将所有请求项置为空闲项(dev = -1)。有 32 项(NR_REQUEST = 32)。
 */
void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
