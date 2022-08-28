/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x) >= 1 && (x) <= 3)

#define READ 0
#define WRITE 1
#define READA 2  /* read-ahead - don't pause */
#define WRITEA 3 /* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a)) >> 8)
#define MINOR(a) ((a)&0xff)

#define NAME_LEN 14
#define ROOT_INO 1

#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137F

#define NR_OPEN 20
#define NR_INODE 32
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#ifndef NULL
#define NULL ((void *)0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct dir_entry)))

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode) - PIPE_TAIL(inode)) & (PAGE_SIZE - 1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode) == PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode) == (PAGE_SIZE - 1))
#define INC_PIPE(head) \
    __asm__("incl %0\n\tandl $4095,%0" ::"m"(head))

typedef char buffer_block[BLOCK_SIZE];
/**
 * @brief 文件系统缓冲区头部展示
 */
struct buffer_head
{
    char *b_data; /* pointer to data block (1024 bytes) */ //< 数据块指针,指向高速缓存内存块
    unsigned long b_blocknr; /* block number */            //< 块编号
    unsigned short b_dev; /* device (0 = free) */          //< 对应设备
    unsigned char b_uptodate;                              //< 是否需要进行更新--存在可读写的内容
    unsigned char b_dirt; /* 0-clean,1-dirty */            //< 是否为脏数据--正在读写
    unsigned char b_count; /* users using this block */    //< 文件引用计数
    unsigned char b_lock; /* 0 - ok, 1 -locked */          //< 是否已经被锁住
    struct task_struct *b_wait;                            //< 等待中断的节点
    struct buffer_head *b_prev;                            //< 链表指针前一个
    struct buffer_head *b_next;                            //< 链表指针后一个
    struct buffer_head *b_prev_free;                       //< 空闲链表指针，前一个
    struct buffer_head *b_next_free;                       //< 空闲链表指针，后一个
};
/**
 * @brief 每个文件对应的inode信息
 */
struct d_inode
{
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_time;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
};

struct m_inode
{
    unsigned short i_mode;      //< 文件类型和属性
    unsigned short i_uid;       //< 宿主用的用户Id
    unsigned long i_size;       //< 文件大小
    unsigned long i_mtime;      //< 最后的修改时间
    unsigned char i_gid;        //< 宿主用户的组id
    unsigned char i_nlinks;     //< 文件目录项目链接数
    unsigned short i_zone[9];   //< 数据占用的block 号，，最多为9个块
                                /* these are in memory also */
    struct task_struct *i_wait; //< 正在等待的i节点进程
    unsigned long i_atime;      //< 最后访问时间
    unsigned long i_ctime;      //< 节点自身修改时间
    unsigned short i_dev;       //< inode节点对应设备号
    unsigned short i_num;       //< inode对应节点号
    unsigned short i_count;     //< 被使用次数
    unsigned char i_lock;       //< lock锁定标志
    unsigned char i_dirt;       //< 脏标志--是否需要同步
    unsigned char i_pipe;       //< 管道标志
    unsigned char i_mount;      //< 安装标志
    unsigned char i_seek;       //< 搜寻标志
    unsigned char i_update;     //< 更新标志位
};

struct file
{
    unsigned short f_mode;
    unsigned short f_flags;
    unsigned short f_count;
    struct m_inode *f_inode;
    off_t f_pos;
};
/**
 * @brief 超级块结构体
 * https://www.cnblogs.com/fengkang1008/p/4691231.html
 */
struct super_block
{
    unsigned short s_ninodes;       //< i节点数目
    unsigned short s_nzones;        //< 逻辑块数目
    unsigned short s_imap_blocks;   //< i 节点位图所占块数目
    unsigned short s_zmap_blocks;   //< 逻辑块位图所占块数
    unsigned short s_firstdatazone; //< 第一个逻辑块好
    unsigned short s_log_zone_size; //< log2(数据块数目/逻辑块)
    unsigned long s_max_size;       //< 最大文件长度
    unsigned short s_magic;         //< 文件系统幻数
                                    /* These are only in memory */
    struct buffer_head *s_imap[8];  //< i节点位图在高速缓冲块指针数组
    struct buffer_head *s_zmap[8];  //< 逻辑块位图在高速缓冲块指针数组
    unsigned short s_dev;           //< 超级块所在设备号
    struct m_inode *s_isup;         //< 被安装文件系统根目录i节点
    struct m_inode *s_imount;       //< 该文件系统被安装到的i节点
    unsigned long s_time;           //< 修改时间
    struct task_struct *s_wait;     //< 等待本超级块的进程指针
    unsigned char s_lock;           //< 锁定标志
    unsigned char s_rd_only;        //< 只读标志
    unsigned char s_dirt;           //< 已经被修改(脏)标志
};

struct d_super_block
{
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
};

struct dir_entry
{
    unsigned short inode;
    char name[NAME_LEN];
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head *start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode *inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode *inode);
extern int bmap(struct m_inode *inode, int block);
extern int create_block(struct m_inode *inode, int block);
extern struct m_inode *namei(const char *pathname);
extern int open_namei(const char *pathname, int flag, int mode,
                      struct m_inode **res_inode);
extern void iput(struct m_inode *inode);
extern struct m_inode *iget(int dev, int nr);
extern struct m_inode *get_empty_inode(void);
extern struct m_inode *get_pipe_inode(void);
extern struct buffer_head *get_hash_table(int dev, int block);
extern struct buffer_head *getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head *bh);
extern void brelse(struct buffer_head *buf);
extern struct buffer_head *bread(int dev, int block);
extern void bread_page(unsigned long addr, int dev, int b[4]);
extern struct buffer_head *breada(int dev, int block, ...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode *new_inode(int dev);
extern void free_inode(struct m_inode *inode);
extern int sync_dev(int dev);
extern struct super_block *get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
