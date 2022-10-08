/*
 *  linux/fs/truncate.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>

#include <sys/stat.h>
/**
 * @brief  查找设备对应的一级块
 * @param  dev              设备名称
 * @param  block            块编号
 */
static void free_ind(int dev,int block)
{
    // 缓冲buffer 
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
		// 获取对应的数据指针
        p = (unsigned short *) bh->b_data;
		// 释放表中所有块
        for (i=0;i<512;i++,p++)
			if (*p)
				free_block(dev,*p);
		brelse(bh);
	}
    // 释放设备块
	free_block(dev,block);
}
/**
 * @brief  释放二级页表
 * @param  dev              设备名称
 * @param  block            设备块号
 */
static void free_dind(int dev,int block)
{
	struct buffer_head * bh;
	unsigned short * p;
	int i;

	if (!block)
		return;
	if (bh=bread(dev,block)) {
        // 查找一级页表项
		p = (unsigned short *) bh->b_data;
        for (i=0;i<512;i++,p++) {
            // 释放一级页表
			if (*p){
                free_ind(dev, *p);
            }
        }
        // 释放缓冲块
        brelse(bh);
	}
	free_block(dev,block);
}
/**
 * @brief 释放inode对应逻辑块
 * 包括对应的直接块、一次间接块和二次间接块
 * @param  inode            对应文件inode指针
 */
void truncate(struct m_inode * inode)
{
	int i;
    // 非常规文件或者目录文件，直接返回
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return;
    // 先释放inode 节点的7个直接节点
	for (i=0;i<7;i++)
		if (inode->i_zone[i]) {
			free_block(inode->i_dev,inode->i_zone[i]);
			inode->i_zone[i]=0;
		}
    // 释放一次间接块
	free_ind(inode->i_dev,inode->i_zone[7]);
    // 释放二次间接块
	free_dind(inode->i_dev,inode->i_zone[8]);
    // 重新设置为0
	inode->i_zone[7] = inode->i_zone[8] = 0;
	inode->i_size = 0;
	inode->i_dirt = 1;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

