/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */
/*
 * 该模块实现控制台输入输出功能
 *     'void con_init(void)'
 *     'void con_write(struct tty_queue * queue)'
 * 希望这是一个非常完整的VT102 实现。
 *
 * 感谢 John T Kohl 实现了蜂鸣指示。
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 */

#define ORIG_X			(*(unsigned char *)0x90000)  // 光标列号
#define ORIG_Y			(*(unsigned char *)0x90001)
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004)
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff)
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8)
#define ORIG_VIDEO_LINES	(25)
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a)
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c)
// 定义显示器单色 / 彩色显示模式类型符号常数。
#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/

#define NPAR 16
// 键盘中断处理函数，见keyboard.S
extern void keyboard_interrupt(void);

static unsigned char	video_type;		/* Type of display being used	*/
static unsigned long	video_num_columns;	/* Number of text columns	*/
static unsigned long	video_size_row;		/* Bytes per row		*/
static unsigned long	video_num_lines;	/* Number of test lines		*/
static unsigned char	video_page;		/* Initial video page		*/
static unsigned long	video_mem_start;	/* Start of video RAM		*/
static unsigned long	video_mem_end;		/* End of video RAM (sort of)	*/
static unsigned short	video_port_reg;		/* Video register select port	*/
static unsigned short	video_port_val;		/* Video register value port	*/
static unsigned short	video_erase_char;	/* Char+Attrib to erase with	*/
// 以下变量用于屏幕卷屏操作
static unsigned long	origin;		/* Used for EGA/VGA fast scroll	*/
static unsigned long	scr_end;	/* Used for EGA/VGA fast scroll	*/
static unsigned long	pos;  // 综合位置
static unsigned long	x,y;  // 当前光标位置
static unsigned long	top,bottom;  // 滚动时顶行行号
static unsigned long	state=0;  // 当前文字状态
static unsigned long	npar,par[NPAR];
static unsigned long	ques=0;
static unsigned char	attr=0x07;
// 统蜂鸣函数。
static void sysbeep(void);

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).
 *  下面是终端回应 ESC-Z 或 csi0c 请求的应答(=vt100 响应)。
 */
//  csi - 控制序列引导码(Control Sequence Introducer)。
#define RESPONSE "\033[?1;2c"

/* NOTE! gotoxy thinks x==video_num_columns is ok */
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	x=new_x;
	y=new_y;
	pos=origin + y*video_size_row + (x<<1);
}
// 设置滚屏起始显示内存地址。
static inline void set_origin(void)
{
	cli();
	outb_p(12, video_port_reg);
	outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);
	outb_p(13, video_port_reg);
	outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
	sti();
}
/// 向上卷动一行（屏幕窗口向下移动）。     
// 将屏幕窗口向下移动一行。参见程序列表后说明。
static void scrup(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
			if (scr_end > video_mem_end) {
				__asm__("cld\n\t"
					"rep\n\t"
					"movsl\n\t"
					"movl _video_num_columns,%1\n\t"
					"rep\n\t"
					"stosw"
					::"a" (video_erase_char),
					"c" ((video_num_lines-1)*video_num_columns>>1),
					"D" (video_mem_start),
					"S" (origin)
					:"cx","di","si");
				scr_end -= origin-video_mem_start;
				pos -= origin-video_mem_start;
				origin = video_mem_start;
			} else {
				__asm__("cld\n\t"
					"rep\n\t"
					"stosw"
					::"a" (video_erase_char),
					"c" (video_num_columns),
					"D" (scr_end-video_size_row)
					:"cx","di");
			}
			set_origin();
		} else {
			__asm__("cld\n\t"
				"rep\n\t"
				"movsl\n\t"
				"movl _video_num_columns,%%ecx\n\t"
				"rep\n\t"
				"stosw"
				::"a" (video_erase_char),
				"c" ((bottom-top-1)*video_num_columns>>1),
				"D" (origin+video_size_row*top),
				"S" (origin+video_size_row*(top+1))
				:"cx","di","si");
		}
	}
	else		/* Not EGA/VGA */
	{
		__asm__("cld\n\t"
			"rep\n\t"
			"movsl\n\t"
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*top),
			"S" (origin+video_size_row*(top+1))
			:"cx","di","si");
	}
}
//// 向下卷动一行（屏幕窗口向上移动）。     
// 将屏幕窗口向上移动一行，屏幕显示的内容向下移动 1 行，在被移动开始行的上方出现一新行。参见     
// 程序列表后说明。处理方法与 scrup()相似，只是为了在移动显示内存数据时不出现数据覆盖错误情况，
// 复制是以反方向进行的，也即从屏幕倒数第 2 行的最后一个字符开始复制
static void scrdown(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
	else		/* Not EGA/VGA */
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t"	/* %edi has been decremented by 4 */
			"movl _video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw"
			::"a" (video_erase_char),
			"c" ((bottom-top-1)*video_num_columns>>1),
			"D" (origin+video_size_row*bottom-4),
			"S" (origin+video_size_row*(bottom-1)-4)
			:"ax","cx","di","si");
	}
}
// 光标下移一行
static void lf(void)
{
	if (y+1<bottom) {
		y++;
		pos += video_size_row;
		return;
	}
	scrup();
}
//  光标上移一行(ri - reverse line feed 反向换行)。
static void ri(void)
{
	if (y>top) {
		y--;
		pos -= video_size_row;
		return;
	}
	scrdown();
}
// 光标回到第 1 列(0 列)左端(cr - carriage return 回车)。
static void cr(void)
{
	pos -= x<<1;
	x=0;
}
// 擦除光标前一字符(用空格替代)(del - delete 删除)。 
static void del(void)
{
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}
// 删除屏幕上与光标位置相关的部分，以屏幕为单位。csi - 控制序列引导码(Control Sequence     
// Introducer)。     
// ANSI 转义序列：'ESC [sJ'(s = 0 删除光标到屏幕底端；1 删除屏幕开始到光标处；2 整屏删除)。    
// 参数：par - 对应上面 s。
static void csi_J(int par)
{
	long count __asm__("cx");
	long start __asm__("di");

	switch (par) {
		case 0:	/* erase from cursor to end of display */
			count = (scr_end-pos)>>1;
			start = pos;
			break;
		case 1:	/* erase from start to cursor */
			count = (pos-origin)>>1;
			start = origin;
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}
//// 删除行内与光标位置相关的部分，以一行为单位。     
// ANSI转义字符序列：'ESC [sK'(s = 0 删除到行尾；1 从开始删除；2 整行都删除)。
static void csi_K(int par)
{
	long count __asm__("cx");
	long start __asm__("di");

	switch (par) {
		case 0:	/* erase from cursor to end of line */
			if (x>=video_num_columns)
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - (x<<1);
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: /* erase whole line */
			start = pos - (x<<1);
			count = video_num_columns;
			break;
		default:
			return;
	}
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t"
		::"c" (count),
		"D" (start),"a" (video_erase_char)
		:"cx","di");
}

void csi_m(void)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:attr=0x07;break;
			case 1:attr=0x0f;break;
			case 4:attr=0x0f;break;
			case 7:attr=0x70;break;
			case 27:attr=0x07;break;
		}
}
// 根据设置显示光标。     
// 根据显示内存光标对应位置 pos，设置显示控制器光标的显示位置
static inline void set_cursor(void)
{
	cli();
	outb_p(14, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
	sti();
}
//// 发送对终端 VT100 的响应序列。     
// 将响应序列放入读缓冲队列中。
static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();
	while (*p) {
		PUTCH(*p,tty->read_q);
		p++;
	}
	sti();
	copy_to_cooked(tty);
}
// 在光标处插入一空格字符。
static void insert_char(void)
{
	int i=x;
	unsigned short tmp, old = video_erase_char;
	unsigned short * p = (unsigned short *) pos;

	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}
/// 在光标处插入一行（则光标将处在新的空行上）。     
// 将屏幕从光标所在行到屏幕底向下卷动一行。
static void insert_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrdown();
	top=oldtop;
	bottom=oldbottom;
}
// 删除光标处的一个字符
static void delete_char(void)
{
	int i;
	unsigned short * p = (unsigned short *) pos;

	if (x>=video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;
}
// 删除光标所在行
static void delete_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrup();
	top=oldtop;
	bottom=oldbottom;
}
// 在光标处插入nr 个字符
static void csi_at(unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char();
}

static void csi_L(unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line();
}

static void csi_P(unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char();
}

static void csi_M(unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line();
}

static int saved_x=0;
static int saved_y=0;

static void save_cur(void)
{
	saved_x=x;
	saved_y=y;
}

static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}
// 控制台写入函数
// 从终端对应的tty 写缓冲区取字符，并显示在屏幕上
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;
    // 获取缓冲区中现有字符
	nr = CHARS(tty->write_q);
    // 遍历进行打印
	while (nr--) {
		GETCH(tty->write_q,c);
		switch(state) {
			case 0:
				if (c>31 && c<127) {
					if (x>=video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					__asm__("movb _attr,%%ah\n\t"
						"movw %%ax,%1\n\t"
						::"a" (c),"m" (*(short *)pos)
						:"ax");
					pos += 2;
					x++;
				} else if (c==27)
					state=1;
				else if (c==10 || c==11 || c==12)
					lf();
				else if (c==13)
					cr();
				else if (c==ERASE_CHAR(tty))
					del();
				else if (c==8) {
					if (x) {
						x--;
						pos -= 2;
					}
				} else if (c==9) {
					c=8-(x&7);
					x += c;
					pos += c<<1;
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
				} else if (c==7)
					sysbeep();
				break;
			case 1:
				state=0;
				if (c=='[')
					state=2;
				else if (c=='E')
					gotoxy(0,y+1);
				else if (c=='M')
					ri();
				else if (c=='D')
					lf();
				else if (c=='Z')
					respond(tty);
				else if (x=='7')
					save_cur();
				else if (x=='8')
					restore_cur();
				break;
			case 2:
				for(npar=0;npar<NPAR;npar++)
					par[npar]=0;
				npar=0;
				state=3;
				if (ques=(c=='?'))
					break;
			case 3:
				if (c==';' && npar<NPAR-1) {
					npar++;
					break;
				} else if (c>='0' && c<='9') {
					par[npar]=10*par[npar]+c-'0';
					break;
				} else state=4;
			case 4:
				state=0;
				switch(c) {
					case 'G': case '`':
						if (par[0]) par[0]--;
						gotoxy(par[0],y);
						break;
					case 'A':
						if (!par[0]) par[0]++;
						gotoxy(x,y-par[0]);
						break;
					case 'B': case 'e':
						if (!par[0]) par[0]++;
						gotoxy(x,y+par[0]);
						break;
					case 'C': case 'a':
						if (!par[0]) par[0]++;
						gotoxy(x+par[0],y);
						break;
					case 'D':
						if (!par[0]) par[0]++;
						gotoxy(x-par[0],y);
						break;
					case 'E':
						if (!par[0]) par[0]++;
						gotoxy(0,y+par[0]);
						break;
					case 'F':
						if (!par[0]) par[0]++;
						gotoxy(0,y-par[0]);
						break;
					case 'd':
						if (par[0]) par[0]--;
						gotoxy(x,par[0]);
						break;
					case 'H': case 'f':
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(par[1],par[0]);
						break;
					case 'J':
						csi_J(par[0]);
						break;
					case 'K':
						csi_K(par[0]);
						break;
					case 'L':
						csi_L(par[0]);
						break;
					case 'M':
						csi_M(par[0]);
						break;
					case 'P':
						csi_P(par[0]);
						break;
					case '@':
						csi_at(par[0]);
						break;
					case 'm':
						csi_m();
						break;
					case 'r':
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0];
							bottom=par[1];
						}
						break;
					case 's':
						save_cur();
						break;
					case 'u':
						restore_cur();
						break;
				}
		}
	}
    // 最后根据上面设置的光标位置，向显示器发送光标显示位置
	set_cursor();
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */

/**
 * @brief 
 * 键盘中断控制台初始化函数，用于初始化中断控制台
 * 
 * 读取setup.s 程序保存的信息，用以确定当前显示器类型，
 * 并设置所有相关参数
 */
void con_init(void)
{
	// 
	register unsigned char a;
	char *display_desc = "????";
	char *display_ptr;
	// 显示器显示字符列数目
	video_num_columns = ORIG_VIDEO_COLS;
	// 计算行
	video_size_row = video_num_columns * 2;
	video_num_lines = ORIG_VIDEO_LINES;
	video_page = ORIG_VIDEO_PAGE;
	video_erase_char = 0x0720;
	// 原始模式为7--单色显示器
	if (ORIG_VIDEO_MODE == 7)			/* Is this a monochrome display? */
	{
		video_mem_start = 0xb0000;
		video_port_reg = 0x3b4;
		video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAM;
			video_mem_end = 0xb8000;
			display_desc = "EGAm";
		}
		else
		{
			video_type = VIDEO_TYPE_MDA;
			video_mem_end	= 0xb2000;
			display_desc = "*MDA";
		}
	}
	else								/* If not, it is color. */
	{
		video_mem_start = 0xb8000;
		video_port_reg	= 0x3d4;
		video_port_val	= 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			video_type = VIDEO_TYPE_EGAC;
			video_mem_end = 0xbc000;
			display_desc = "EGAc";
		}
		else
		{
			video_type = VIDEO_TYPE_CGA;
			video_mem_end = 0xba000;
			display_desc = "*CGA";
		}
	}

	/* Let the user known what kind of display driver we are using */
	
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	while (*display_desc)
	{
		*display_ptr++ = *display_desc++;
		display_ptr++;
	}
	
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	
	origin	= video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	top	= 0;
	bottom	= video_num_lines;

	gotoxy(ORIG_X,ORIG_Y);
	set_trap_gate(0x21,&keyboard_interrupt);
	outb_p(inb_p(0x21)&0xfd,0x21);
	a=inb_p(0x61);
	outb_p(a|0x80,0x61);
	outb(a,0x61);
}
/* from bsd-net-2: */
//// 停止蜂鸣。
// 复位 8255A PB 端口的位 1 和位 0。
void sysbeepstop(void)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;

// 开通蜂鸣。     
// 8255A 芯片 PB 端口的位 1 用作扬声器的开门信号；位 0 用作 8253 定时器 2 的门信号，该定时器的     
// 输出脉冲送往扬声器，作为扬声器发声的频率。因此要使扬声器蜂鸣，需要两步：首先开启 PB 端口     
// 位 1 和位 0（置位），然后设置定时器发送一定的定时频率即可。
static void sysbeep(void)
{
	/* enable counter 2 */
	outb_p(inb_p(0x61)|3, 0x61);
	/* set command for counter 2, 2 byte write */
	outb_p(0xB6, 0x43);
	/* send 0x637 for 750 HZ */
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);
	/* 1/8 second */
	beepcount = HZ/8;	
}
