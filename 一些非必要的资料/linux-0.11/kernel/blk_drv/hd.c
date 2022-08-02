/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

/*      
* 本程序是底层硬盘中断辅助程序。主要用于扫描请求列表，使用中断在函数之间跳转。      
* 由于所有的函数都是在中断里调用的，所以这些函数不可以睡眠。请特别注意。      
* 由 Drew Eckhardt 修改，利用 CMOS 信息检测硬盘数。     
*/
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>


// 定义硬件相关类型定义
#define MAJOR_NR 3
#include "blk.h"
/**
 * @brief 读取CMOS 参数宏定义函数
 */
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7   // 读写一个扇区时允许的最多出错次数
#define MAX_HD		2  // 系统支持的最多硬盘数量

/**
 * @brief 硬盘中断程序在复位操作时会调用的重新校正函数(287 行)。
 */
static void recal_intr(void);
/**
 * @brief  重新校正标志
 * 将磁头移动到0 柱面
 */
static int recalibrate = 1;
/**
 * @brief 复位标志
 * 当发生读写错误时会设置该标志
 * 来进行复位硬盘和控制器
 */
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */

/**
 * @brief 硬盘参数以及类型定义
 */
struct hd_i_struct
{
	int head;  // 磁头数目
	int sect;  // 每个磁道扇区数
	int cyl;   // 柱面数
	int wpcom; // 写前预补偿柱面号
	int lzone; // 磁头着陆区柱面号
	int ctl;   // 控制字节
};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };
// 计算硬盘个数
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct))) 
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif
/**
 * @brief 定义硬盘描述结构数组
 */
static struct hd_struct {
	long start_sect; //< 起始扇区号
	long nr_sects;  // 扇区总数
} hd[5*MAX_HD]={{0,0},};
/**
 * @brief 数据读取
 * - port 读取端口
 * - nr   读取总计到子节数
 * - buf  目标数据保存地址
 */
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")
/**
 * @brief 端口 port，共写 nr 字，从 buf 中取数据。
 */
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

/**
 * @brief 硬盘中断处理函数
 * system_call.s，221 行）
 */
extern void hd_interrupt(void);
/**
 * @brief 虚拟盘创建加载函数
 * ramdisk.c，71 行）。
 */
extern void rd_load(void);

/* This may be used only once, enforced by 'static int callable' */
/**
 * @brief 系统初始化函数，只能被调用一次
 * 函数的参数由初始化程序init/main.c 的 init子程序设置为指向 0x90080 处，此处存放着 setup.s
 * 序从 BIOS 取得的 2 个硬盘的基本参数表(32 字节)。硬盘参数表信息参见下面列表后的说明。
 * 本函数主要功能是读取 CMOS 和硬盘参数表信息，用于设置硬盘分区结构 hd，并加载 RAM 虚拟盘和根文件系统。
 * @param  BIOS         硬盘基本参数
 * @return int
 */
int sys_setup(void * BIOS)
{
	// 运行次数统计
	// 保证此函数只能被使用一次
	static int callable = 1;
	int i,drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head * bh;
	// 始化时 callable=1，当运行该函数时将其设置为 0，使本函数只能执行一次。
	if (!callable)
		return -1;
	// 重置为0
	callable = 0;
#ifndef HD_TYPE
	// 没有定义硬盘参数，就从0x90080 处读入
	// 遍历所有磁盘
	for (drive=0 ; drive<2 ; drive++) {
		// 读取相关数据
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}
	// 判断是否有第二个磁盘
	if (hd_info[1].cyl)
		NR_HD=2;
	else
		NR_HD=1;
#endif
	// 遍历所有硬盘
	for (i=0 ; i<NR_HD ; i++) {
		// 设置开始扇区号为0
		hd[i*5].start_sect = 0;
		// 设置扇区总数 = 磁头数 * 每个磁头扇区数 * 柱面数目
		hd[i*5].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/
	/*    
	* 我们对 CMOS 有关硬盘的信息有些怀疑：可能会出现这样的情况，我们有一块 SCSI/ESDI/等的      
	* 控制器，它是以 ST-506 方式与 BIOS兼容的，因而会出现在我们的 BIOS 参数表中，但却又不      
	* 是寄存器兼容的，因此这些参数在 CMOS 中又不存在。      
	* 另外，我们假设 ST-506驱动器（如果有的话）是系统中的基本驱动器，也即以驱动器 1 或 2      
	* 出现的驱动器。      
	* 第 1 个驱动器参数存放在 CMOS 字节 0x12 的高半字节中，第 2 个存放在低半字节中。该 4 位字节      
	* 信息可以是驱动器类型，也可能仅是 0xf。0xf 表示使用 CMOS 中 0x19 字节作为驱动器 1 的 8 位      
	* 类型字节，使用 CMOS 中 0x1A 字节作为驱动器 2 的类型字节。      
	* 总之，一个非零值意味着我们有一个 AT 控制器硬盘兼容的驱动器。      
	*/
	// 检测硬盘是否为AT 控制器兼容
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	else
		NR_HD = 0;
	// 重置磁盘信息
	// 若 NR_HD=0，则两个硬盘都不是 AT 控制器兼容的，硬盘数据结构清零。     
	// 若 NR_HD=1，则将第 2 个硬盘的参数清零。
	for (i = NR_HD ; i < 2 ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = 0;
	}
	// 读取每一个硬盘上第 1 块数据（第 1 个扇区有用），获取其中的分区表信息。 
	// 首先利用函数bread()读硬盘第 1 块数据(fs/buffer.c,267)，参数中的 0x300 是硬盘的主设备号    
	// (参见列表后的说明)。然后根据硬盘头1 个扇区位置 0x1fe处的两个字节是否为'55AA'来判断
	// 该扇区中位于 0x1BE 开始的分区表是否有效。最后将分区表信息放入硬盘分区数据结构 hd 中。 
	for (drive = 0; drive < NR_HD; drive++)
	{
		if (!(bh = bread(0x300 + drive*5,0))) {
			printk("Unable to read partition table of drive %d\n\r",
				drive);
			panic("");
		}
		if (bh->b_data[510] != 0x55 || (unsigned char)
		    bh->b_data[511] != 0xAA) {
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		p = 0x1BE + (void *)bh->b_data;
		for (i=1;i<5;i++,p++) {
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
		brelse(bh); // 释放为存放硬盘块而申请的内存缓冲区页。
	}
	if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load();    //  加载（创建）RAMDISK(kernel/blk_drv/ramdisk.c,71)。
	mount_root(); // 安装根文件系统(fs/super.c,242)。
	return (0);
}
/**
 * @brief   判断并循环等待驱动器就绪
 * 读硬盘控制器状态寄存器端口 HD_STATUS(0x1f7)
 * 并循环检测驱动器就绪比特位和控制器忙位。
 * 如果返回值为 0，则表示等待超时出错，否则 OK。
 * @return int
 */
static int controller_ready(void)
{
	// 最大重试次数
	int retries=10000;

	while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	return (retries); 
}
/**
 * @brief 检测硬盘执行命令后的状态(win_ 表示表示温切斯特硬盘的缩写)
 * 读取状态寄存器中的命令执行结果状态
 * 返回0表示正常，1 出错
 * 如果执行命令错误，则再读取错误寄存器HD_ERROR(0x1f1)
 * @return int
 */
static int win_result(void)
{
	// 获取端口当前状态
	int i=inb_p(HD_STATUS);
	// 状态正常且为 READY_STAT 或者 SEEK_STAT
	// 直接返回
	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	// 错误读取，对应的异常信息
	if (i&1) i=inb(HD_ERROR);
	return (1);
}
/**
 * @brief  向硬盘控制器发送命令块
 * @param  drive            硬盘号(0-1)
 * @param  nsect            读写扇区数目
 * @param  sect             起始扇区
 * @param  head             磁头号
 * @param  cyl              柱面号
 * @param  cmd              命令码
 * @param  intr_addr        硬中断处理调用函数
 */
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	// port 变量指定对应寄存器dx
	register int port asm("dx");
	// 确认是否越界
	if (drive>1 || head>15)
		panic("Trying to write bad sector");
	// 检查控制器是否就绪
	if (!controller_ready())
		panic("HD controller not ready");
	// 设置磁盘中断响应
	do_hd = intr_addr;
	
	// 向控制寄存器输出控制字节
	outb_p(hd_info[drive].ctl,HD_CMD);
	// 设置目标端口，相当于目标的总线地址
	port=HD_DATA;
	outb_p(hd_info[drive].wpcom>>2,++port);
	outb_p(nsect,++port);
	outb_p(sect,++port);
	outb_p(cyl,++port);
	outb_p(cyl>>8,++port);
	// 驱动器号+磁头号。
	outb_p(0xA0|(drive<<4)|head,++port);
	// 执行控制命令
	outb(cmd,++port);
}
/**
 * @brief 等待硬盘就绪
 * 也即循环等待主状态控制器忙标志位复位。若仅有就绪或寻道结束标志
 * 置位，则成功，返回 0。若经过一段时间仍为忙，则返回 1。
 * @return int
 */
static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
			break;
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == READY_STAT | SEEK_STAT)
		return(0);
	printk("HD controller times out\n\r");
	return(1);
}
/**
 * @brief  重置磁盘控制器
 */
static void reset_controller(void)
{
	int	i;
	// 向控制寄存器端口发送控制字节(4-复位)。
	outb(4,HD_CMD);
	// for 循环空操作
	for(i = 0; i < 100; i++) nop();
	// 发送控制子节-- 核心操作
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	// 检查是否忙碌
	if (drive_busy())
		printk("HD-controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n\r",i);
}
/**
 * @brief  重置硬盘
 * @param  nr               My Param doc
 */
static void reset_hd(int nr)
{
	// 重置控制器
	reset_controller();
	// 发送重置磁盘命令
	hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
		hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}
/**
 * @brief 意外硬盘中断调用函数。     
 * 发生意外硬盘中断时，硬盘中断处理程序中调用的默认C 处理函数。在被调用函数指针为空时     
 * 调用该函数。参见(kernel/system_call.s,241 行)。
 */
void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
}
/**
 * @brief 读写失败调用函数
 */
static void bad_rw_intr(void)
{
	// 超过错误上限
	// 直接结束
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	// 大于3次
	// 执行复位硬盘控制器
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
}
/**
 * @brief 硬盘读取中断处理函数
 * 
 */
static void read_intr(void)
{
	// 1. 判断原有处理是否出错
	if (win_result()) {
		// 进行失败处理
		bad_rw_intr();
		// 再次进行读取操作，对硬盘进行复位
		do_hd_request();
		return;
	}
	// 尝试读取数据
	port_read(HD_DATA,CURRENT->buffer,256);
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	// 增加起始扇区号
	CURRENT->sector++;
	// 检查是否读取完目标函数
	if (--CURRENT->nr_sectors) {
		// 没有读取完成
		// 再次进行调用读取
		do_hd = &read_intr;
		return;
	}
	// 结束此次请求
	end_request(1);
	// 处理下一个读取
	do_hd_request();
}
/**
 * @brief 写入函数
 */
static void write_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	// 存在剩余的操作
	if (--CURRENT->nr_sectors) {
		// 指向下一块扇区
		CURRENT->sector++;
		// 增加缓冲区
		CURRENT->buffer += 512;
		// 设置操作具柄--循环进行操作
		do_hd = &write_intr;
		// 继续写入
		port_write(HD_DATA,CURRENT->buffer,256);
		return;
	}
	// 结束处理
	end_request(1);
	// 调用下一个
	do_hd_request();
}
/**
 * @brief 磁盘矫正复位
 * 在硬盘中断处理程序中被调用。
 * 如果硬盘控制器返回错误信息，则首先进行硬盘读写失败处理，然后请求硬盘作相应(复位)处理。
 */
static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request();
}

/**
 * @brief 执行硬盘读写请求操作。     
 * 若请求项是块设备的第 1 个，则块设备当前请求项指针（参见 ll_rw_blk.c，28 行）会直接指向该请求项，
 * 并会立刻调用本函数执行读写操作。否则在一个读写操作完成而引发的硬盘中断过程中，
 * 若还有请求项需要处理，则也会在中断过程中调用本函数。参见kernel/system_call.s，221 行。
 * 用于初始化过程中的持续调用，后续依靠中断进行调用
 */
void do_hd_request(void)
{
	int i,r;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;
	//  初始化请求，没有就直接退出
	INIT_REQUEST;
	// 取设备号中的子设备号--硬盘分区号
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	// 检查是否超范围
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
		end_request(0);
		goto repeat;
	}
	// 计算绝对扇区号 
	block += hd[dev].start_sect;
	// 重新计算设备号
	dev /= 5;
	// 计算扇区号、柱面号
	// 磁头号
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;
	// 需要读写的扇区总数
	nsect = CURRENT->nr_sectors;
	// 需要重置
	if (reset) {
		reset = 0;
		recalibrate = 1;
		reset_hd(CURRENT_DEV);
		return;
	}
	// 需要进行标志位矫正
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
			WIN_RESTORE,&recal_intr);
		return;
	}	
	// 写扇区
	if (CURRENT->cmd == WRITE) {
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		// 如果请求服务 DRQ 置位则退出循环。若等到循环结束也没有置位，则表示此次写硬盘操作失败，去
		// 处理下一个硬盘请求。否则向硬盘控制器数据寄存器端口 HD_DATA 写入 1 个扇区的数据。
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
			/* nothing */ ;
		if (!r) {
			bad_rw_intr();
			goto repeat; // 继续重复执行
		}
		// 执行写操作
		port_write(HD_DATA,CURRENT->buffer,256);
	} else if (CURRENT->cmd == READ) {
		// 执行读操作
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
	} else
		panic("unknown hd-command");
}
// 硬盘系统初始化。
void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST; // 设置硬盘系统调用中断
	set_intr_gate(0x2E,&hd_interrupt);  // 设置硬盘中断门向量 int 0x2E
	outb_p(inb_p(0x21) & 0xfb, 0x21);	//  复位接联的主8259A int2的屏蔽位，允许从片发出中断请求信号。
	outb(inb_p(0xA1) & 0xbf, 0xA1);		//  复位硬盘的中断请求屏蔽位（在从片上），允许硬盘控制器发送中断请求信号。
}
