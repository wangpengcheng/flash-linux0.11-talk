#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#include <sys/types.h>
/**
 * @brief 系统名称
 */
struct utsname {
	char sysname[9];  // 系统名称
	char nodename[9]; // 节点名称
	char release[9]; // 版本名称
	char version[9]; // 内核版本
	char machine[9]; // 机器
};

extern int uname(struct utsname * utsbuf);

#endif
