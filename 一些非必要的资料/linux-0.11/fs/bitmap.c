/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>
/**
 * @brief 将指定地址(addr)
 * 处的一块内存清零。
 * 输入：eax = 0
 * ecx = BLOCK_SIZE / 4
 * edi = addr
 */
#define clear_block(addr)                          \
    __asm__("cld\n\t"                              \
            "rep\n\t"                              \
            "stosl" ::"a"(0),                      \ 
            "c"(BLOCK_SIZE / 4), "D"((long)(addr)) \
            : "cx", "di")

/**
 * @brief 设置指定地址
 * 开始的第nr个位偏移处的
 * 比特位(nr 可以大于32)，返回原比特位(0或1)
 * 输入：%0 - eax(返回值)
 * %1 - eax(0)
 * %2 - nr
 * %3 - (addr)
 * 
 */
#define set_bit(nr, addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res; })

/**
 * @brief 复位指定地址开始的nr位偏移处的比特位
 * 返回原比特位的反码(1 或者 0)
 * 输入：%0 - eax(返回值)
 * %1 - eax(0)
 * %2 - nr，位偏移值
 * %3 - (addr)，addr的内容
 * 
 */
#define clear_bit(nr, addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res; })

/**
 * @brief 从addr 开始寻找第1个0 值比特位
 * 输入：%0 -ecx(返回值)
 * %1 - ecx(0)
 * %2 - esi(addr)
 * 在addr 指定地址开始的位图中寻找第一个是0的比特位，
 * 并将其距离addr的比特偏移值返回
 */
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \
	"bsfl %%eax,%%edx\n\t" \
	"je 2f\n\t" \
	"addl %%edx,%%ecx\n\t" \
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})


/**
 * @brief  释放设备dev对应数据区中的逻辑块 block
 * @param  dev              设备名称
 * @param  block            逻辑块号
 */
void free_block(int dev, int block)
{
    // 超级块结构体，包含inode等信息
	struct super_block * sb;
    // 高速缓冲区块指针
	struct buffer_head * bh;
    // 获取设备对应文件快
	if (!(sb = get_super(dev)))
		panic("trying to free block on nonexistent device");
    // 检查逻辑快是否越界
	if (block < sb->s_firstdatazone || block >= sb->s_nzones)
		panic("trying to free block not in datazone");
    // 查询对应缓冲块
	bh = get_hash_table(dev,block);
    // 缓冲块
	if (bh) {
        // 如果不为1 就释放缓冲块
		if (bh->b_count != 1) {
			printk("trying to free block (%04x:%d), count=%d\n",
				dev,block,bh->b_count);
			return;
		}
		bh->b_dirt=0;
		bh->b_uptodate=0;
		brelse(bh);
	}
    // 计算相对的逻辑块号
	block -= sb->s_firstdatazone - 1 ;
    // 清除逻辑块引用，对应的高速标志位
	if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
		printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
		panic("free_block: bit already cleared");
	}
    // 设置已修改标志位为1
	sb->s_zmap[block/8192]->b_dirt = 1;
}
/**
 * @brief  向dev设备申请一个新的逻辑块(盘块、区块) 返回逻辑号(盘块号)
 * 并重置逻辑块block的逻辑块位图比特位
 * @param  dev              设备名称
 * @return int              最终返回结果
 */
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;
    // 查询对应超级块
	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");
	j = 8192;
    // 遍历超级块对应逻辑位图，获取放置该逻辑块的块号
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i]) {
                // 查找第一个空闲的bit位
                if ((j = find_first_zero(bh->b_data)) < 8192) {
                    break;
                }
            }
    // 没有找到直接返回0
	if (i>=8 || !bh || j>=8192)
		return 0;
    // 将数据指针指向对应的标志位
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	bh->b_dirt = 1;
    // 计算相对标志位--块号
	j += i*8192 + sb->s_firstdatazone-1;
	if (j >= sb->s_nzones)
		return 0;
    // 获取读取设备上的新逻辑块数据
    // 失败则死机
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	// 引用计数应该为1，否则死机
    if (bh->b_count != 1)
		panic("new block: count is != 1");
    // 将该逻辑块清零，并设置对应标志位
	clear_block(bh->b_data);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
    // 释放对应缓冲区
	brelse(bh);
    // 返回对应逻辑块号
	return j;
}
/**
 * @brief  清除inode信息
 * @param  inode            inode节点指针
 */
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	if (!inode)
		return;
    // 设备号为0，清空对应i节点占用内存
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
    // 校验引用计数
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
    // 检查目录连接数不为0
	if (inode->i_nlinks)
		panic("trying to free inode with links");
    // 获取上级超级块
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
    // 检查节点号是否越界
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	// 检查对应的节点位图是否存在
    if (!(bh=sb->s_imap[inode->i_num>>13]))
		panic("nonexistent imap in superblock");
    // 清除对应的节点位图
	if (clear_bit(inode->i_num&8191,bh->b_data))
		printk("free_inode: bit already cleared.\n\r");
	// 修改对应标志位
    bh->b_dirt = 1;
    // 清空整个内存
	memset(inode,0,sizeof(*inode));
}
/**
 * @brief  创建新的inode节点
 * @param  dev              对应设备
 * @return struct m_inode*  生成的inode 节点
 */
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;
    // 查询空的inode
	if (!(inode=get_empty_inode()))
		return NULL;
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");
	j = 8192;
    // 查询空闲标志位
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
    // 还回inode
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
		iput(inode);
		return NULL;
	}
	if (set_bit(j,bh->b_data))
		panic("new_inode: bit already set");
	// 设置相关标志位
    bh->b_dirt = 1;
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	inode->i_num = j + i*8192;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
