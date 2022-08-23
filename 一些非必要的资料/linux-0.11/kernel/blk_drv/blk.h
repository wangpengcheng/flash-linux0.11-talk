#ifndef _BLK_H
#define _BLK_H
/**
 * @brief 块设备数量
 */
#define NR_BLK_DEV	7
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */

/**
 * @brief 请求队列长度
 * 注意：读操作仅仅使用这些低端的2/3；读操作优先
 * 
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */

/**
 * @brief 使用request 进行封装扩展，统一抽象
 * 之后可以在分页请求中使用同样的request 结构
 * 分页处理中，'BH'是NULL
 * 而waiting 则是用于等待读写的完成
 */
struct request
{
    int dev; /* -1 if no request */ // 使用的目标设备号
    int cmd; /* READ or WRITE */    /* 对应的设备命令READ 或 WRITE */
    int errors;                     //< 操作时产生的错误次数
    unsigned long sector;           //< 起始扇区(1块=2扇区)
    unsigned long nr_sectors;       //< 读/写扇区
    char *buffer;                   //< 读写数据缓冲区
    struct task_struct *waiting;    //< 等待IO操作的任务
    struct buffer_head *bh;         //< 缓冲区头指针(include/linux/fs.h)
    struct request *next;           //< 指向下一项请求项
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */

/**
 * @brief 定义电梯梯度算法
 * 读操作优先，对于写操作优先判定严苛
 * - 写操作优先
 * - 相同操作设备号小的优先
 * - 前面均相同的，起始扇区小的优先
 */
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || (s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))

/**
 * @brief 块设备描述
 * 结构体
 */
struct blk_dev_struct {
	void (*request_fn)(void);  //< 对应处理函数
	struct request * current_request; //< 当前请求处理结构体
};
/**
 * @brief 块设备数组
 */
extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
/**
 * @brief 块设备请求
 */
extern struct request request[NR_REQUEST];
/**
 * @brief 等待中的任务队列
 * 指针指向当前正在等待中的任务队列
 */
extern struct task_struct * wait_for_request;
#define MAJOR_NR 3
/**
 * @brief 定义主设备号
 * 在块设备驱动程序（如 hd.c）要包含此头文件时，必须先定义驱动程序对应设备的主设备号。这样     
 * 下面 61 行—87 行就能为包含本文件的驱动程序给出正确的宏定义。
 */
#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1)
/* ram disk */
/**
 * @brief  定义设备名称为
 * 闪存
 */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3)
/* harddisk */
/**
 * @brief 硬盘
 */
#define DEVICE_NAME "harddisk"
/**
 * @brief 宏定义内联函数
 * 用于进行软中断处理
 */
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
/**
 * @brief 
 */
#define DEVICE_NR(device) (MINOR(device)/5)
/**
 * @brief 设备开启
 */
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"

#endif
/**
 * @brief 获取当前的
 * 等待进程
 */
#define CURRENT (blk_dev[MAJOR_NR].current_request)
/**
 * @brief 获取当前设备号
 */
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

/**
 * @brief 
 */
#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);

/**
 * @brief  解除buffer锁定
 * @param  bh               指定的buffer头部
 */
extern inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk(DEVICE_NAME ": free buffer being unlocked\n");
	// 进行解锁
	bh->b_lock=0;
	// 唤醒等待中的线程
	wake_up(&bh->b_wait);
}
/**
 * @brief 终止请求
 * 关闭指定块设备
 * 
 * 时间 = 0 是打印错误信息
 * @param  uptodate         缓冲区更新标志
 */
extern inline void end_request(int uptodate)
{
	// 关闭当前设备
	DEVICE_OFF(CURRENT->dev);
	// 存在当前缓冲区
	if (CURRENT->bh) {
		// 更新 缓冲区更新标志
		CURRENT->bh->b_uptodate = uptodate;
		// 解锁
		unlock_buffer(CURRENT->bh);
	}
	// 标志为0
	// 请求失败，打印错误信息
	if (!uptodate) {
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	// 唤醒等待进程
	wake_up(&CURRENT->waiting);
	// 唤醒等待请求的进程
	wake_up(&wait_for_request);
	// 重置设备符号
	// 释放该项请求
	CURRENT->dev = -1; 
	// 指向洗衣歌请求
	CURRENT = CURRENT->next;
}
/**
 * @brief 初始化等待任务队列
 */
#define INIT_REQUEST \
repeat: \
	if (!CURRENT) \
		return; \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \  // 当前主设备号不对则死机
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \ // 请求时缓冲区没有锁定就死机
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif

#endif
