/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

/**
 * @file ramdisk.c
 * @brief  虚拟盘设置相关文件
 * @author wangpengcheng  (wangpengcheng2018@gmail.com)
 * @version 1.0
 * @date 2022-07-31 19:51:46
 * @copyright Copyright (c) 2022  WPC
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
 *    <td> 2022-07-31 19:51:46 </td>
 *    <td> 1.0 </td>
 *    <td> wangpengcheng </td>
 *    <td>内容</td>
 * </tr>
 * </table>
 */

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1
#include "blk.h"

char	*rd_start;  // 虚拟盘在内存中的起始位置，在52行初始化函数 rd_init()中确定
int	rd_length = 0;   // 虚拟盘所占内存大小(子节)

/**
 * @brief
 * 虚拟盘当前请求项操作函数。程序结构与do_hd_request()类似(hd.c,294)。    
 * 在低级块设备接口函数ll_rw_block()建立了虚拟盘（rd）的请求项并添加到 rd 的链表中之后，    
 * 就会调用该函数对 rd 当前请求项进行处理。该函数首先计算当前请求项中指定的起始扇区对应     
 * 虚拟盘所处内存的起始位置 addr 和要求的扇区数对应的字节长度值 len，然后根据请求项中的     
 * 命令进行操作。若是写命令 WRITE，就把请求项所指缓冲区中的数据直接复制到内存位置 addr处。
 * 若是读操作则反之。数据复制完成后即可直接调用end_request()对本次请求项作结束处理。
 * 然后跳转到函数开始处再去处理下一个请求项。
 */
void do_rd_request(void)
{
	int	len;
	char	*addr;

	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	// 进行内存拷贝
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
	end_request(1);
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */

/**
 * @brief  返回内存虚拟盘ramdisk 所需的内存量
 * 虚拟盘初始化函数。确定虚拟盘在内存中的起始地址，长度。并对整个虚拟盘区清零。
 * @param  mem_start        内存开始地址
 * @param  length           长度
 * @return long				操纵长度
 */
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST; // do_rd_request()。
	rd_start = (char *) mem_start;  // 对于 16MB 系统，该值为 4MB。 
	rd_length = length;
	cp = rd_start;
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */

/*
 * 如果根文件系统设备(root device)是 ramdisk 的话，则尝试加载它。root device 原先是指向
 * 软盘的，我们将它改成指向ramdisk。
 */

/**
 * @brief
 * 尝试把根文件系统加载到虚拟盘中。
 * 该函数将在内核设置函数setup()（hd.c，156 行）中被调用。另外，1 磁盘块 = 1024 字节。
 */
void rd_load(void)
{
	struct buffer_head *bh;   // 高速缓冲块头指针
	struct super_block	s;    // 文件超级块结构
	int		block = 256;	/* Start at block 256 */
	int     i = 1;				/* 表示根文件系统映象文件在 boot 盘第 256 磁盘块开始处*/
	int		nblocks;
	char		*cp;		/* Move pointer */
	// 如果长度为0，直接退出
	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start); // 显示 ramdisk的大小以及内存起始位置
	// 非软盘直接退出
	if (MAJOR(ROOT_DEV) != 2)
		return;
	// 读软盘块 256 + 1, 256, 256 + 2。breada() 用于读取指定的数据块，并标出还需要读的块，然后返回 
	// 含有数据块的缓冲区指针。如果返回 NULL，则表示数据块不可读(fs/buffer.c,322)。     
	// 这里 block+1 是指磁盘上的超级块。
	bh = breada(ROOT_DEV, block + 1, block, block + 2, -1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	// 获取高速缓冲区块
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	// 进行缓冲区释放
	brelse(bh);
	// 如果超级块中魔数不对，则说明不是minix 文件系统。
	if (s.s_magic != SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */
		/* 磁盘中没有 ramdisk映像文件，退出去执行通常的软盘引导 */ 
		return;
	// 计算逻辑块数
	// 块数 = 逻辑块数(区段数) * 2 ^ (每区段块数的次方)
	nblocks = s.s_nzones << s.s_log_zone_size;
	// 如果数据块数大于内存中虚拟盘所能容纳的块数
	// 不能加载
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	// cp 指向虚拟盘起始处
	// 然后将磁盘上的根文件系统印象文件复制到虚拟盘上
	cp = rd_start;
	// 循环进行读取
	while (nblocks) {
		// 单次读取数较大
		// 采用超前预读方式进行数据读取
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			// 就单块进行读取
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			// 输出IO error
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		// 进行内存拷贝，将缓冲区中的数据复制到cp处
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);
		// 释放高速缓冲区
		brelse(bh);
		printk("\010\010\010\010\010%4dk",i);
		cp += BLOCK_SIZE;
		block++;
		nblocks--;
		i++;
	}
	printk("\010\010\010\010\010done \n");
	// 改 ROOT_DEV 使其指向虚拟盘 ramdisk。
	ROOT_DEV=0x0101;
}
