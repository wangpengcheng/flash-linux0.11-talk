/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>

/**
 * @brief 获取NR在信号位图中对应位的二进制数值。信号编号 1-32
 * 比如信号5的位图数值 = 1 <<(5-1) = 16 = 00010000b
 */
#define _S(nr) (1 << ((nr)-1))

/**
 * @brief 检查信号是否可以阻塞
 * 除了 SIGKILL 和 SIGSTOP 信号以外都是可以阻塞的
 *
 */
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

/**
 * @brief  显示任务号NR的进程号、进程状态和内核堆栈空闲最大字节数
 * @param  nr               任务号NR
 * @param  p                任务状态结构描述符
 */
void show_task(int nr, struct task_struct *p)
{
    int i, j = 4096 - sizeof(struct task_struct);

    printk("%d: pid=%d, state=%d, ", nr, p->pid, p->state);
    i = 0;
    while (i < j && !((char *)(p + 1))[i])
        i++;
    printk("%d (of %d) chars free in kernel stack\n\r", i, j);
}
/**
 * @brief 显示所有任务的任务号、进程号、进程状态
 * 和内核堆栈空闲字节数
 */
void show_stat(void)
{
    int i;
    // 遍历所有进程--最大进程任务数量为64 个
    for (i = 0; i < NR_TASKS; i++)
        if (task[i])
            show_task(i, task[i]);
}
//< 定义每个时间片的滴答数
#define LATCH (1193180 / HZ)

extern void mem_use(void);
/**
 * @brief  时钟中断处理程序
 * @return int 中断处理结果
 */
extern int timer_interrupt(void);
/**
 * @brief 系统调用中断处理程序
 * @return int 中断处理结果
 */
extern int system_call(void);
/**
 * @brief 任务联合结构体(任务成员和stack)
 * 因为一个任务的数据结构与其内核态堆栈放在同一内存页
 * 所以从堆栈段寄存器ss可以获取其数据段选择符号
 */
union task_union
{
    struct task_struct task;
    char stack[PAGE_SIZE];
};
/**
 * @brief 每个任务（进程）在内核态运行时都有自己的内核态堆栈
 * 这里定义了任务的内核态堆栈结构
 */
static union task_union init_task = {
    INIT_TASK,
};
/**
 * @brief 从开机开始算起的滴答数时间值
 */
long volatile jiffies = 0;
/**
 * @brief 计算从1970:0:0 开始计时的秒数
 */
long startup_time = 0;
/**
 * @brief current 指针
 * 指向当前正在执行的任务
 * 初始化时，指向第一个任务
 */
struct task_struct *current = &(init_task.task);
/**
 * @brief 使用过协处理器任务的指针
 */
struct task_struct *last_task_used_math = NULL;

/**
 * @brief 定义任务指针数组，用于存储全部任务
 */
struct task_struct *task[NR_TASKS] = {
    &(init_task.task),
};
/**
 * @brief 定义用户堆栈，4K。指针指在最后一项
 */
long user_stack[PAGE_SIZE >> 2];

/**
 * @brief 设置堆栈ss:esp(数据段选择符，指针)
 * 0x10 为用户态堆栈地址
 */
struct
{
    long *a;
    short b;
} stack_start = {&user_stack[PAGE_SIZE >> 2], 0x10};
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */

/**
 * @brief 将当前协处理器内容保存到老协处理器状态数组中，
 * 并将当前任务协处理器内容加载到协处理器
 * 任务被调度交换过以后，该函数用以保存原任务的协处理器状态（上下文）并恢复新调度进来的
 * 前任务的协处理器执行状态。
 */
void math_state_restore()
{
    // 使用协处理器指针与当前相同，无需处理
    if (last_task_used_math == current)
        return;
    __asm__("fwait");        // 发送fwait指令，避免协处理器被污染
    if (last_task_used_math) // 上一个任务使用了协处理器，则保存其状态
    {
        __asm__("fnsave %0" ::"m"(last_task_used_math->tss.i387));
    }
    last_task_used_math = current; // 重新设置当前指针
    if (current->used_math)        // 使用之后进行重置
    {
        __asm__("frstor %0" ::"m"(current->tss.i387));
    }
    else // 第一次使用，设置相关标志位
    {
        __asm__("fninit" ::);
        current->used_math = 1;
    }
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
void schedule(void)
{
    int i, next, c;
    struct task_struct **p; // 当前的任务结构体指针

    /* check alarm, wake up any interruptible tasks that have got a signal */
    // 从后向前遍历，找出任何得到信号的可中断任务,进行唤醒调度
    for (p = &LAST_TASK; p > &FIRST_TASK; --p)
    {
        if (*p)
        {
            // 果设置过任务的定时值 alarm，并且已经过期(alarm<jiffies),则在信号位图中置 SIGALRM 信号，
            // 向任务发送 SIGALARM 信号。然后清alarm。该信号的默认操作是终止进程。
            // jiffies 是系统从开机开始算起的滴答数（10ms/滴答）。定义在 sched.h 第 139 行。
            if ((*p)->alarm && (*p)->alarm < jiffies)
            {

                // 设置信号位为 SIGALRM
                (*p)->signal |= (1 << (SIGALRM - 1));
                // 设置调度时钟为 0
                (*p)->alarm = 0;
            }
            // 如果信号位图中除被阻塞的信号外还有其它信号，并且任务处于可中断状态，则置任务为就绪状态。
            // 其中'~(_BLOCKABLE & (*p)->blocked)'用于忽略被阻塞的信号，但 SIGKILL 和 SIGSTOP 不能被阻塞。
            if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
                (*p)->state == TASK_INTERRUPTIBLE)
            {
                // 恢复任务运行
                (*p)->state = TASK_RUNNING;
            }
        }
    }

    /* this is the scheduler proper: */
    //
    while (1)
    {
        c = -1;
        next = 0;
        i = NR_TASKS;        // 总任务数量
        p = &task[NR_TASKS]; // 任务队列
        // 遍历查找调度task
        // 这里使用的是时间片最长的最优先
        // 优先级时间片轮转算法
        while (--i)
        {
            // 跳过空指针
            if (!*--p)
            {
                continue;
            }
            // 程序处于RUNNING状态
            // 找出counter 最大的值
            // 下一个指向它
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
            {
                // 当前任务的运行时间计数
                c = (*p)->counter;
                next = i;
            }
        }
        // 如果 找到了就直接跳出
        if (c)
        {
            break;
        }
        // 遍历任务队列
        for (p = &LAST_TASK; p > &FIRST_TASK; --p)
        {
            if (*p)
            {
                // 重新计算计数器
                // counter = counter /2 + priority
                (*p)->counter = ((*p)->counter >> 1) +
                                (*p)->priority;
            }
        }
    }
    // 切换到next的任务运行
    switch_to(next);
}
// 将当前任务设置为可中断的等待状态
// 放入*p 指定的等待队列中
// 暂停系统调用，主要用于main主线程进行
// 循环执行，查询空闲并执行
// 每个子进程执行完之后会主动切换
// 创建shell 子进程并继续等待
// 相当于shell 由系统创建，只要有命令就会不断执行
// 每个子进程都是
int sys_pause(void)
{
    current->state = TASK_INTERRUPTIBLE;
    schedule();
    return 0;
}
/**
 * @brief  睡眠函数
 * @param  p   防止等待任务的队列头指针
 */
void sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p)
        return;
    // 0 号进程不允许睡眠
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");
    // 保存原始队头指针
    tmp = *p;
    // 头部重新指向当前进程
    *p = current;
    // 设置为不可中断
    current->state = TASK_UNINTERRUPTIBLE;
    // 再次进行调度
    schedule();
    // 将其他任务设置为0--运行状态(就绪准备运行)
    if (tmp)
        tmp->state = 0;
}
/**
 * @brief  将当前任务设置为可中断的状态，并放入*p 指定的等待队列中
 * 主要用来进行将当前正在执行的任务切出，并重新进行调度
 * @param  p  任务队列头部指针
 */
void interruptible_sleep_on(struct task_struct **p)
{
    struct task_struct *tmp;

    if (!p)
        return;
    if (current == &(init_task.task))
        panic("task[0] trying to sleep");
    // 保存原有头部指针
    tmp = *p;
    // 头部指向当前指针
    *p = current;
// 重复操作
repeat:
    // 设置状态为可中断
    // 方便进行切换
    current->state = TASK_INTERRUPTIBLE;
    // 重新进行调度
    schedule();
    /**
     * 队列头部不为当前运行任务--等待队列中存在其他等待任务
     * 当指针*p 所指向的不是当前任务时，表示在当前任务被放
     * 入队列后，又有新的任务被插入等待队列中，因此就应该将其他
     * 的等待任务设置为可运行状态
     */
    if (*p && *p != current)
    {
        (**p).state = 0;
        goto repeat;
    }
    // 此时所有任务均为已经调度的状态
    // 重新设置当前任务
    *p = NULL;
    // 如果头部任务任务仍旧存在
    // 将其设置为就绪状态
    if (tmp)
        tmp->state = 0;
}
/**
 * @brief  唤醒指定任务*p
 * @param  p                指定任务指针
 */
void wake_up(struct task_struct **p)
{
    if (p && *p)
    {
        (**p).state = 0; // 设置为就绪(可运行)状态
        *p = NULL;
    }
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
/**
 * 这里用于软驱定时处理的代码是 201–262 行。
 * 在阅读这段代码之前请先看一下块设备一章中有关
 * 软盘驱动程序（floppy.c）后面的说明。或者到阅读软盘块设备驱动程序时在来看这段代码。
 * 其中时间单位：1个滴答 = 1/100 秒
 * */
//下面数组存放等待软驱马达启动到正常转速的进程指针。数组索引 0 - 3 分别对应软驱 A - D。 static struct task_struct *wait_motor[4] = {NULL, NULL, NULL, NULL};
static struct task_struct *wait_motor[4] = {NULL, NULL, NULL, NULL};
/**
 * @brief 软驱马达需要的滴答数
 * 程序中默认启动时间为50 个滴答(0.5秒)
 */
static int mon_timer[4] = {0, 0, 0, 0};
static int moff_timer[4] = {0, 0, 0, 0};
unsigned char current_DOR = 0x0C;
/**
 * @brief  指定驱动启动到正常运转状态需要等待时间
 * @param  nr               软件驱动号(0-3),返回值为滴答数
 * @return int 处理结果
 */
int ticks_to_floppy_on(unsigned int nr)
{
    extern unsigned char selected;
    unsigned char mask = 0x10 << nr;

    if (nr > 3)
        panic("floppy_on: nr>3");
    moff_timer[nr] = 10000; /* 100 s = very big :-) */
    cli();                  /* use floppy_off to turn it off */
    mask |= current_DOR;
    if (!selected)
    {
        mask &= 0xFC;
        mask |= nr;
    }
    if (mask != current_DOR)
    {
        outb(mask, FD_DOR);
        if ((mask ^ current_DOR) & 0xf0)
            mon_timer[nr] = HZ / 2;
        else if (mon_timer[nr] < 2)
            mon_timer[nr] = 2;
        current_DOR = mask;
    }
    sti();
    return mon_timer[nr];
}
/**
 * @brief 等待指定软驱动马达启动需要的一段时间，然后返回
 * 设置指定软驱动的马达启动到正常转速需要的延时，然后睡眠等待
 * 在定时中断过程中会一直递减
 * 判断这里设定的延迟值。当延时到期，就会唤醒这里的等待进程
 * @param  nr               My Param doc
 */
void floppy_on(unsigned int nr)
{
    cli();
    // 如果马达启动定时还没到，就一直将当前进程设置为
    // 不可中断睡眠状态，并放入等待马达运行到队列中
    while (ticks_to_floppy_on(nr))
        sleep_on(nr + wait_motor);
    sti();
}

void floppy_off(unsigned int nr)
{
    moff_timer[nr] = 3 * HZ;
}
/**
 * @brief 软盘定时处理子程序
 * 每10ms调用一次
 * 如果某个马达停转定时达到，则将数字输出寄存器进行复位
 */
void do_floppy_timer(void)
{
    int i;
    unsigned char mask = 0x10;

    for (i = 0; i < 4; i++, mask <<= 1)
    {
        if (!(mask & current_DOR))
            continue;
        if (mon_timer[i])
        {
            if (!--mon_timer[i])
                wake_up(i + wait_motor);
        }
        else if (!moff_timer[i])
        {
            current_DOR &= ~mask;
            outb(current_DOR, FD_DOR);
        }
        else
            moff_timer[i]--;
    }
}

#define TIME_REQUESTS 64
/**
 * @brief 定时器链表结构和定时器数组
 */
static struct timer_list
{
    long jiffies;            //< 定时器滴答数
    void (*fn)();            //< 定时器指定处理程序
    struct timer_list *next; //< 下一个定时器指针
} timer_list[TIME_REQUESTS], *next_timer = NULL;
/**
 * @brief  增加定时器
 * @param  jiffies          指定时间片
 * @param  fn               对应的执行处理函数
 */
void add_timer(long jiffies, void (*fn)(void))
{
    // 时间数组指针
    struct timer_list *p;

    // 空指针直接返回
    if (!fn)
        return;
    // 禁止中断 https://blog.csdn.net/zang141588761/article/details/52325106
    cli();
    // 时间片耗尽--直接执行
    if (jiffies <= 0)
        (fn)();
    else
    {
        // 增加到时间队列中
        for (p = timer_list; p < timer_list + TIME_REQUESTS; p++) {
            // 找到一个函数为空的时间队列
            if (!p->fn) {
                break;
            }
        }
        // 没有空闲定时器--直接报错
        if (p >= timer_list + TIME_REQUESTS)
            panic("No more time requests free");
        // 重新设置时间片信息
        p->fn = fn;
        p->jiffies = jiffies;
        p->next = next_timer;
        // 将下一个时间片指针指向p
        next_timer = p;
        // 如果当前剩余时间片小于下一个
        // 进行调整，保证从小到大的顺序排序
        // 检查头部指针时间片是否为0 即可
        while (p->next && p->next->jiffies < p->jiffies)
        {
            p->jiffies -= p->next->jiffies;
            fn = p->fn;
            p->fn = p->next->fn;
            p->next->fn = fn;
            jiffies = p->jiffies;
            p->jiffies = p->next->jiffies;
            p->next->jiffies = jiffies;
            p = p->next;
        }
    }
    // 恢复中断
    sti();
}
/**
 * @brief  时钟中断c 处理程序，在kernel/system_call.s 的timer_interrupt 中被调用
 * 用于进行时间中断处理,主要用来唤醒调度函数用于进行程序调度
 * 对于一个进程由于执行时间片用完，则进行任务切换。并执行一个计时器更新
 * 这里写了程序调度和磁盘数据处理
 * @param  cpl              当前特权级别0或者3,0 表示内核代码在执行
 */
void do_timer(long cpl)
{
    extern int beepcount;   //< 扬声器发声时间滴答数(kernel/chr_drv/console.c,697)
    extern void sysbeepstop(void);  //< 关闭扬声器(kernel/chr_drv/console.c)

    // 如果发声计数次数到，则关闭发声(向 0x61发送命令，复位位0和1，位0控制8253计数器2的工作，位1控制扬声器)
    if (beepcount)
        if (!--beepcount)
            sysbeepstop();
    // 如果特权级别非0，增加用户时间片
    if (cpl)
        current->utime++;
    else // 内核级别，增加超级用户时间片
        current->stime++;

    // 存在其它的用户定时器--需要调度
    // 将当前定时器值--
    // 如果jiffies < 1 执行定时器函数，并取消该定时器
    if (next_timer)
    {
        next_timer->jiffies--;
        while (next_timer && next_timer->jiffies <= 0)
        {
            // 插入函数指针定义
            void (*fn)(void);
            // 获取函数指针
            fn = next_timer->fn;
            next_timer->fn = NULL;
            // 指向下一个
            next_timer = next_timer->next;
            // 继续执行
            (fn)();
        }
    }
    // 如果当前软盘控制器 FDC 的数字输出寄存器中马达启动位有置位的，则执行软盘定时程序(245 行)
    if (current_DOR & 0xf0)
        do_floppy_timer();
    // 发现当前时间片仍然存在
    // 之际返回
    if ((--current->counter) > 0)
        return;
    // 将当前时间片重新设置为1
    current->counter = 0;
    // 为内核调用，直接返回
    if (!cpl)
        return;
    // 用户调用执行调度程序
    schedule();
}
/**
 * @brief  系统调用功能-- 设置报警定时时间值(秒)
 * @param  seconds          定时报警参数，大于0时，设置该新的定时值并返回原定时值。否则返回0
 * @return int 原来的原始设置值
 */
int sys_alarm(long seconds)
{
    int old = current->alarm;

    if (old)
        old = (old - jiffies) / HZ;
    // 
    current->alarm = (seconds > 0) ? (jiffies + HZ * seconds) : 0;
    return (old);
}

int sys_getpid(void)
{
    return current->pid;
}

int sys_getppid(void)
{
    return current->father;
}

int sys_getuid(void)
{
    return current->uid;
}

int sys_geteuid(void)
{
    return current->euid;
}
// 获取组id
int sys_getgid(void)
{
    return current->gid;
}

int sys_getegid(void)
{
    return current->egid;
}
// 获取时间nice 值
// 降低对CPU的使用优先权
int sys_nice(long increment)
{
    if (current->priority - increment > 0)
        current->priority -= increment;
    return 0;
}

// 调度初始化函数
void sched_init(void)
{
    // 初始化变量
    int i;  // 下标索引
    struct desc_struct *p;
    // 检查信号处理结构体具柄
    if (sizeof(struct sigaction) != 16)
        panic("Struct sigaction MUST be 16 bytes");
    // 设置tss表，用于保存每个进程的寄存器
    set_tss_desc(gdt + FIRST_TSS_ENTRY, &(init_task.task.tss));
    // 保存初始任务的局部数据表描述符号
    set_ldt_desc(gdt + FIRST_LDT_ENTRY, &(init_task.task.ldt));
    // 计算当前页号
    p = gdt + 2 + FIRST_TSS_ENTRY;
    // 遍历任务缓冲区
    for (i = 1; i < NR_TASKS; i++)
    {
        task[i] = NULL;
        p->a = p->b = 0;
        p++;
        p->a = p->b = 0;
        p++;
    }
    /* Clear NT, so that we won't have troubles with that later on */
    // 清除标志寄存器中的位 NT，这样以后就不会有麻烦
    // NT 标志用于控制程序的递归调用(Nested Task)。当 NT 置位时，那么当前中断任务执行
    // iret 指令时就会引起任务切换。NT 指出 TSS 中的 back_link 字段是否有效。
    __asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");  // 复位NT标志
    ltr(0);  // 加载任务0的tss 到各个寄存器
    lldt(0);  // 将局部描述表加载到局部描述符表寄存器
    // 初始化相关定时器
    outb_p(0x36, 0x43);         /* binary, mode 3, LSB/MSB, ch 0 */
    outb_p(LATCH & 0xff, 0x40); /* LSB */
    outb(LATCH >> 8, 0x40);     /* MSB */
    set_intr_gate(0x20, &timer_interrupt);
    outb(inb_p(0x21) & ~0x01, 0x21);
    set_system_gate(0x80, &system_call);
}
