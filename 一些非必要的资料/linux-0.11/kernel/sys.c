/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <sys/times.h>
#include <sys/utsname.h>
// 返回日期和时间
int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

int sys_ptrace()
{
	return -ENOSYS;
}

int sys_stty()
{
	return -ENOSYS;
}

int sys_gtty()
{
	return -ENOSYS;
}

int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}
/**
 * @brief  设置当前任务的实际/有效组ID(gid)
 * 如果任务没有超级用户特权只能互换实际组ID
 * 和有效组ID。如果任务具有超级用户特权
 * 就能任意设置有效的和实际的组ID
 * 保留 gid(saved gid) 被设置成与有效gid同值
 * @param  rgid         实际组id
 * @param  egid         有效组id    
 * @return int  最终结果
 */
int sys_setregid(int rgid, int egid)
{
	if (rgid>0) {
		if ((current->gid == rgid) || 
		    suser())
			current->gid = rgid;
		else
			return(-EPERM);
	}
	if (egid>0) {
		if ((current->gid == egid) ||
		    (current->egid == egid) ||
		    (current->sgid == egid) ||
		    suser())
			current->egid = egid;
		else
			return(-EPERM);
	}
	return 0;
}
/**
 * @brief 设置进程组号(gid)。如果任务没有超级用户特权，它可以使用setgid()将其有效 gid     
 * （effective gid）设置为成其保留 gid(saved gid)或其实际 gid(real gid)。如果任务有
 * 超级用户特权，则实际 gid、有效 gid 和保留 gid 都被设置成参数指定的 gid
 * @param  gid              目标进程组id
 * @return int
 */
int sys_setgid(int gid)
{
	return(sys_setregid(gid, gid));
}
// 关闭进程记账功能
int sys_acct()
{
	return -ENOSYS;
}

int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}
// 系统调用，返回开始的时间值
// 时间存储在tloc
int sys_time(long * tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc) {
		// 确认内存是否足够
		verify_area(tloc,4);  
		// 将用户段数据进行放入
		put_fs_long(i,(unsigned long *)tloc);
	}
	return i;
}

/*
 * Unprivileged users may change the real user id to the effective uid
 * or vice versa.
 */

/**
 * @brief 特权的用户可以见实际用户标识符(real uid)改成有效用户标识符(effective uid)，反之也然。
 */

/**
 * @brief  设置任务的实际以及/或者有效用户 ID（uid）。如果任务没有超级用户特权，那么只能互换其     
 * 实际用户 ID 和有效用户 ID。如果任务具有超级用户特权，就能任意设置有效的和实际的用户 ID。     
 * 保留的 uid（saved uid）被设置成与有效 uid 同值。
 *
 * @param  ruid             实际uud
 * @param  euid             有效uud
 * @return int
 */
int sys_setreuid(int ruid, int euid)
{
    // 保存旧uid 
	int old_ruid = current->uid;
	// 真实RUID > 0 -- 需要设置
	if (ruid>0) {
		if (
            (current->euid==ruid) ||
            (old_ruid == ruid) ||
		    suser()
        ) {
            current->uid = ruid;
        } else {
            return (-EPERM);
        }
			
	}
	// 有效uid
	// 特权用户直接改写euid 
	// 这里也是root 用户创建文件
	// 读写需要权限的原因
	if (euid>0) {
		if ((old_ruid == euid) ||
                    (current->euid == euid) ||
		    suser())
			current->euid = euid;
		else {
			current->uid = old_ruid;
			return(-EPERM);
		}
	}
	return 0;
}
/**
 * @brief  设置任务用户号(uid)。如果任务没有超级用户特权，它可以使用 setuid()将其有效 uid
 * effective uid）设置成其保留 uid(saved uid)或其实际 uid(real uid)。如果任务有
 * 级用户特权，则实际 uid、有效 uid 和保留 uid 都被设置成参数指定的 uid。
 * @param  uid              My Param doc
 * @return int
 */
int sys_setuid(int uid)
{
	return(sys_setreuid(uid, uid));
}

int sys_stime(long * tptr)
{
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies/HZ;
	return 0;
}
// 获取当前任务时间。tms 结构中包括用户时间、系统时间、子进程用户时间、子进程系统时间。
int sys_times(struct tms * tbuf)
{
	if (tbuf) {
		verify_area(tbuf,sizeof *tbuf);
		put_fs_long(current->utime,(unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime,(unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime,(unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime,(unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}
/**
 * @brief  设置进程末尾数据
 * @param  end_data_seg     末尾数据值
 * @return int 
 */
int sys_brk(unsigned long end_data_seg)
{
	if (end_data_seg >= current->end_code &&  // 要大于代码段
	    end_data_seg < current->start_stack - 16384) // 小于堆栈段-16KB？？寄存器
		current->brk = end_data_seg;				 // 设置新数据段结尾值。
	return current->brk;
}

/*
 * This needs some heave checking ...
 * I just haven't get the stomach for it. I also don't fully
 * understand sessions/pgrp etc. Let somebody who does explain it.
 */

/**
 * @brief  设置进程组id
 * @param  pid              进程pid
 * @param  pgid             进程组id
 * @return int 
 */
int sys_setpgid(int pid, int pgid)
{
	int i;
	// 0 号进程
	if (!pid)
		pid = current->pid; // 
	if (!pgid) // 0号组
		pgid = current->pid;
	// 遍历查找目标任务
	for (i=0 ; i<NR_TASKS ; i++)
		// 找寻目标pid 
		if (task[i] && task[i]->pid==pid) {
			// 已经是首部--正在执行
			if (task[i]->leader)
				return -EPERM;
			// 会话与当前任务不同--正在执行其它会话
			if (task[i]->session != current->session)
				return -EPERM;
			// 设置任务pgrp
			task[i]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

int sys_getpgrp(void)
{
	return current->pgrp;
}
/**
 * @brief 创建一个会话
 * 即设置其 leader=1），并且设置其会话号=其组号=其进程号。
 * @return int
 */
int sys_setsid(void)
{
	// 非超级用户
	// 非当前执行
	if (current->leader && !suser())
		return -EPERM;
	// 设置leader为1
	current->leader = 1;
	current->session = current->pgrp = current->pid;
	current->tty = -1;
	return current->pgrp;
}

/**
 * @brief  获取系统信息
 * @param  name             My Param doc
 * @return int 
 */
int sys_uname(struct utsname * name)
{
	// 这里直接写死了
	static struct utsname thisname = {
		"linux .0","nodename","release ","version ","machine "
	};
	int i;
	// 空指针直接异常
	if (!name) return -ERROR;
	// 检测内存是否足够
	verify_area(name,sizeof *name); // 验证缓冲区是否可写，如果是只读的就进行复制
	// 进行数据拷贝
	for(i=0;i<sizeof *name;i++)
		put_fs_byte(((char *) &thisname)[i],i+(char *) name);
	return 0;
}
/**
 * @brief 置当前进程创建文件属性屏蔽码为mask & 0777。并返回原屏蔽码。
 * @param  mask             My Param doc
 * @return int
 */
int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}
