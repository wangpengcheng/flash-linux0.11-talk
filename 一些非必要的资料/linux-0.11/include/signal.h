#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

typedef int sig_atomic_t;
typedef unsigned int sigset_t; /* 32 bits */

#define _NSIG 32
#define NSIG _NSIG

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT 6
#define SIGUNUSED 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22

/* Ok, I haven't implemented sigactions, but trying to keep headers POSIX */
#define SA_NOCLDSTOP 1
/**
 * @brief 无掩码
 */
#define SA_NOMASK 0x40000000 
/**
 * @brief 只允许使用一次
 */
#define SA_ONESHOT 0x80000000

#define SIG_BLOCK 0   /* for blocking signals */
#define SIG_UNBLOCK 1 /* for unblocking signals */
#define SIG_SETMASK 2 /* for setting the signal mask */
/**
 * @brief 默认恢复句柄
 */
#define SIG_DFL ((void (*)(int))0) /* default signal handling */
/**
 * @brief 恢复处理句柄
 * 默认忽略相关信号
 */
#define SIG_IGN ((void (*)(int))1) /* ignore signal */
/**
 * @brief 信号结构体，表示对应的信号以及处理函数
 * 是函数的超级集合
 */
struct sigaction
{
    void (*sa_handler)(int);   // 信号处理句柄
    sigset_t sa_mask;          // 信号处理屏蔽掩码，可以阻塞指定的信号集合
    int sa_flags;              // 信号标志位
    void (*sa_restorer)(void); // 信号恢复指针函数(系统内部使用)
};
/**
 * @brief  信号处理函数，信号处理一次后会恢复默认句柄
 * 防止重复捕获信号处理，造成内核压力过大
 *
 * 主要
 * @param  _sig             信号
 * @param  _func            指定处理句柄
 */
void (*signal(int _sig, void (*_func)(int)))(int);
int raise(int sig);
int kill(pid_t pid, int sig);
int sigaddset(sigset_t *mask, int signo);
int sigdelset(sigset_t *mask, int signo);
int sigemptyset(sigset_t *mask);
int sigfillset(sigset_t *mask);
int sigismember(sigset_t *mask, int signo); /* 1 - is, 0 - not, -1 error */
int sigpending(sigset_t *set);
int sigprocmask(int how, sigset_t *set, sigset_t *oldset);
int sigsuspend(sigset_t *sigmask);
/**
 * @brief  设置信号处理综合句柄
 * @param  sig              指定信号编号
 * @param  act              指定的新的信号处理行为
 * @param  oldact           旧信号处理行为，作为返回值返回
 * @return int
 */
int sigaction(int sig, struct sigaction *act, struct sigaction *oldact);

#endif /* _SIGNAL_H */
