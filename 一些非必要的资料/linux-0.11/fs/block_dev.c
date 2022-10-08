/*
 *  linux/fs/block_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
/**
 * @brief  块设备写入函数
 * 从指定地址开始写入buf中的count长度
 * @param  dev              设备名称
 * @param  pos              设备对应的逻辑地址
 * @param  buf              数据缓冲buffer
 * @param  count            总数据长度
 * @return int              最终执行结果
 */
int block_write(int dev, long * pos, char * buf, int count)
{
    // 获取逻辑块号
	int block = *pos >> BLOCK_SIZE_BITS;
	// 获取块/页内偏移
    int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int written = 0;
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
        // 当前块，剩余可写入的字符长度
		chars = BLOCK_SIZE - offset;
        // 剩余空间充足，直接进行写入即可
		if (chars > count)
			chars=count;
        // 刚好写入一块
		if (chars == BLOCK_SIZE)
            // 直接获取当前块
			bh = getblk(dev,block);
		else
            // 读取当前块和接下来连续的3块
			bh = breada(dev,block,block+1,block+2,-1);
		block++;
        // 空指针，逻辑块获取失败
		if (!bh)
			return written?written:-EIO;
		// 指定开始指针
        p = offset + bh->b_data;
		// 重置offset 方便下一次循环
        offset = 0;
        // 更新pos 指针位置
		*pos += chars;
        // 更新写入长度
		written += chars;
        // 更新剩余长度
		count -= chars;
        // 执行连续写入
		while (chars-->0)
			*(p++) = get_fs_byte(buf++);
		// 设置标志位，要求进行写入
        bh->b_dirt = 1;
        // 释放缓冲块
		brelse(bh);
	}
	return written;
}
/**
 * @brief  块设备读取函数
 * @param  dev              设备
 * @param  pos              开始位置
 * @param  buf              缓冲buf
 * @param  count            总共统计数量
 * @return int              最终返回结果
 */
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
	int block = *pos >> BLOCK_SIZE_BITS;
	int offset = *pos & (BLOCK_SIZE-1);
	int chars;
	int read = 0;
	struct buffer_head * bh;
	register char * p;

	while (count>0) {
		chars = BLOCK_SIZE-offset;
		if (chars > count)
			chars = count;
        // 进行预先读取
		if (!(bh = breada(dev,block,block+1,block+2,-1)))
			return read?read:-EIO;
		block++;
		p = offset + bh->b_data;
		offset = 0;
		*pos += chars;
		read += chars;
		count -= chars;
		while (chars-->0)
            // 进行更新--写入到用户缓冲区
			put_fs_byte(*(p++),buf++);
		brelse(bh);
	}
	return read;
}
