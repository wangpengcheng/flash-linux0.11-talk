/*
 *  linux/fs/file_table.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/fs.h>
/**
 * @brief 文件表数组
 */
struct file file_table[NR_FILE];
