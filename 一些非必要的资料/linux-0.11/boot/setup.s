;
;	setup.s		(C) 1991 Linus Torvalds
;
; setup.s is responsible for getting the system data from the BIOS,
; and putting them into the appropriate places in system memory.
; both setup.s and system has been loaded by the bootblock.
;
; This code asks the bios for memory/disk/other parameters, and
; puts them in a "safe" place: 0x90000-0x901FF, ie where the
; boot-block used to be. It is then up to the protected mode
; system to read them from there before the area is overwritten
; for buffer-blocks.
;

; NOTE; These had better be the same as in bootsect.s;

; setup.s 负责从BIOS 中获取系统数据，并将这些数据放在系统内存中的适当地方
; 此时 setup.s 和system已经由bootsect 引导块加载到内存中
; 查询内存/磁盘/其它参数，并方到0x90000-0x901FF, 覆盖bootsect 

; NOTE：下面这些参数最好和bootsect.s 中相同


INITSEG  = 0x9000	; we move boot here - out of the way ; boostup.s的地址
SYSSEG   = 0x1000	; system loaded at 0x10000 (65536). ; 系统段开始的地址
SETUPSEG = 0x9020	; this is the current segment ; 当前段开始的地址

.globl begtext, begdata, begbss, endtext, enddata, endbss ; 定义全局代码段
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start ; 进入start 段
start:

; ok, the read went well so we get current cursor position and save it for
; posterity.

; ok，前面任务均已经完成，现在保存光标位置

	mov	ax,#INITSEG	; this is done in bootsect already, but...
	mov	ds,ax
	mov	ah,#0x03	; read cursor pos 设置当前的位置，用于调用光标读取 
	xor	bh,bh		; 清除
	int	0x10		; save it in known place, con_init fetches
					;执行系统调用 
					; 输入： bh = 页号
					; 返回： ch = 扫描开始线，cl = 扫描结束线
					; dh = 行号(0x00 是顶端)，dl = 列号(0x00 是左边)
	mov	[0],dx		; it from 0x90000.
					; 上两句是说将光标位置信息存放在0x90000 处，控制台初始化时会来取

; Get memory size (extended mem, kB)
; 查询扩展内存大小
; 调用0x15 中断，功能号ah = 0x88
; 返回：ax = 0x100000(1M) 处开始的扩展内存大小
; 如果出错则 CF 置位，ax = 出错码

	mov	ah,#0x88
	int	0x15
	mov	[2],ax   ; 将扩展内存大小存储在当前段位置第二个字节，即0x90002 处

; Get video-card data:
; 查询显卡模式数据
; 调用 BIOS 中断 0x10，功能号 ah = 0x0f
; 返回：ah = 字符列数，al = 显示模式，bh = 当前显示页。
; 0x90004(1 字)存放当前页，0x90006 显示模式，0x90007 字符列数

	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		; bh = display page ; 存储显示页数
	mov	[6],ax		; al = video mode, ah = window width ; 存储视频模式和窗口大小

; check for EGA/VGA and some config parameters
; 检查EGA/VGA 方式并取相关参数
; 调用0x10 中断，获取信息
; 功能号：ah = 0x12，bl = 0x10
; 返回：bh = 显示状态
; 	   (0x00 - 彩色模式，I/O 端口=0x3dX)
;      (0x01 - 单色模式，I/O 端口=0x3bX) 
; bl = 安装的显示内存 大小
; (0x00 - 64k, 0x01 - 128k, 0x02 - 192k, 0x03 = 256k)
; cx = 显示卡特性参数(参见程序后的说明)。

	mov	ah,#0x12 ; 设置附加功能号 
	mov	bl,#0x10  ; 设置bl 参数
	int	0x10
	mov	[8],ax    ; 存储 
	mov	[10],bx   ; 存储显示模式&显示内存
	mov	[12],cx   ; 显卡特性参数

; Get hd0 data
; 获取第一个硬盘参数(复制硬盘参数列表)
; 第一个硬盘参数表的首地址竟然是中断向量 0x41 的向量值！而第 2 个硬盘
; 参数表紧接第 1 个表的后面，中断向量 0x46 的向量值也指向这第 2 个硬盘
; 参数表首址。表的长度是 16 个字节(0x10)。                        
; 下面两段程序分别复制 BIOS 有关两个硬盘的参数表，0x90080 处存放第 1 个                       
; 硬盘的表，0x90090 处存放第 2 个硬盘的表。

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41] 	; 取中断向量0x41的值，也就是hd0参数表的地址 -> ds:si 
	mov	ax,#INITSEG     
	mov	es,ax			; 设置es地址为 0x90000
	mov	di,#0x0080		; 设置传输目的地址：0x9000:0x0080 -> es:di
	mov	cx,#0x10        ; 设置传输数据长度为 0x10 字节
	rep
	movsb     ; 重复进行数据拷贝--将对应的磁盘参数表进行拷贝

; Get hd1 data
; 和上面操作相同获取hd1的磁盘表
	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	rep
	movsb

; Check that there IS a hd1 :-)
; 检查是否存在第二个硬盘，如果不存在hd1, 就进行清零--hd1的数据在磁盘0表后面，如果hd1不存在，读取的是正常数据需要进行清空
; 用 BIOS 中断调用 0x13 的取盘类型功能。 
; 输入功能号 ah = 0x15；
; 输入：dl = 驱动器号（0x8X 是硬盘：0x80 指第 1 个硬盘，0x81 第 2 个硬盘） 
; 输出：ah = 类型码；00 --没有这个盘，
; CF置位； 01 --是软驱，没有 change-line 支持；02 --是软驱(或其它可移动设备)，有 change-line 支持； 03 --是硬盘。 

	mov	ax,#0x01500 ; 
	mov	dl,#0x81
	int	0x13 ; 进行系统调用
	jc	no_disk1
	cmp	ah,#3 ; 检查是否是硬盘
	je	is_disk1 ; 确定之后进行跳转
no_disk1:
	mov	ax,#INITSEG  ;第 2 个硬盘不存在，则对第 2 个硬盘表清零。
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb ; 将ax的值0x00 填充到目标地址，相当于进行清空 
is_disk1:

; now we want to move to protected mode ...
; 进入保护模式开始执行

	cli			; no interrupts allowed ; 此时禁止中断，防止问题出现

; first we move the system to it's rightful place
; 首先我们将system 模块移动到正确的位置0x00000处
; 现在system的位置是0x10000 ~ 0x8fff 需要将内存向低端移动0x10000(64K)的位置

	mov	ax,#0x0000
	cld			; 'direction'=0, movs moves forward
do_move:  ; 开始执行搬迁
	mov	es,ax		; destination segment
	add	ax,#0x1000 ;设置源地址为 0x1000
	cmp	ax,#0x9000 ; 确定指向地址超过0x9000--已经搬迁完毕
	jz	end_move  ; 结束移动
	mov	ds,ax		; source segment 设置段起开始地址
	sub	di,di 
	sub	si,si
	mov 	cx,#0x8000  ;设置数据大小
	rep
	movsw           ;执行移动
	jmp	do_move    ; 没有移动完，继续执行

; then we load the segment descriptors

; 结束加载后，我们继续加载段描述符号
; 这里开始会遇到 32 位保护模式的操作，因此需要 Intel 32位保护模式编程方面的知识了, 
; 有关这方面的信息请查阅列表后的简单介绍或附录中的详细说明。这里仅作概要说明。
; 进入保护模式中运行之前，我们需要首先设置好需要使用的段描述符表。这里需要设置全局
; 描述符表和中断描述符表。

; lidt 指令用于加载中断描述符表(idt)寄存器，它的操作数是 6 个字节，0-1 字节是描述符表的 
; 长度值(字节)；2-5 字节是描述符表的 32 位线性基地址（首地址），其形式参见下面 
; 219-220 行和 223-224 行的说明。中断描述符表中的每一个表项（8 字节）指出发生中断时
;  需要调用的代码的信息，与中断向量有些相似，但要包含更多的信息。
;
; lgdt 指令用于加载全局描述符表(gdt)寄存器，其操作数格式与 lidt 指令的相同。
; 全局描述符中的每个描述符项(8 字节)描述了保护模式下数据和代码段（块）的信息。其中包括段的
; 最大长度限制(16 位)、段的线性基址（32 位）、段的特权级、段是否在内存、读写许可以及
; 它一些保护模式运行的标志。参见后面 205-216 行。

end_move:
	mov	ax,#SETUPSEG	; right, forgot this at first. didn't work :-)
	mov	ds,ax			; 将ds 寄存器重新指向本段代码
	lidt	idt_48		; load idt with 0,0   加载中断描述符表(idt)寄存器，idt_48 是 6字节操作数的位置
						;  前 2 字节表示 idt 表的限长，后 4 字节表示 idt 表所处的基地址
	lgdt	gdt_48		; load gdt with whatever appropriate 
						; 加载全局描述符表(gdt)寄存器，gdt_48 是 6字节操作数的位置
						; 

; that was painless, now we enable A20
; 开启A20地址线，参见程序列表后有关 A20 信号线的说明。
; 于所涉及到的一些端口和命令，可参考 kernel/chr_drv/keyboard.S 程序后对键盘接口的说明。

	call	empty_8042   ; 确定输入缓冲器为空，没有额外输入
	mov	al,#0xD1		; command write
	out	#0x64,al		; 向8042的P2 端口写入0xD1指令--这里指0x60口
	call	empty_8042 ; 等待输入为空
	mov	al,#0xDF		; A20 on; 继续开启A20 地址参数
	out	#0x60,al
	call	empty_8042 ; 输入缓冲区为空，表示A20线已经选通

; well, that went ok, I hope. Now we have to reprogram the interrupts :-(
; we put them right after the intel-reserved hardware interrupts, at
; int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
; messed this up with the original PC, and they haven't been able to
; rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
; which is used for the internal hardware interrupts as well. We just
; have to reprogram the 8259's, and it isn't fun.

; 希望以上一切正常。现在我们必须重新对中断进行编程
; 我们将它们放在正好处于 intel 保留的硬件中断后面，在 int 0x20-0x2F。
; 那里它们不会引起冲突。不幸的是 IBM 在原 PC 机中搞糟了，以后也没有纠正过来。
; PC 机的 bios 将中断放在了0x08-0x0f，这些中断也被用于内部硬件中断。 
; 所以我们就必须重新对 8259 中断控制器进行编程，这一点都没劲。

	mov	al,#0x11		; initialization sequence
	out	#0x20,al		; send it to 8259A-1
	.word	0x00eb,0x00eb		; jmp $+2, jmp $+2
	out	#0xA0,al		; and to 8259A-2
	.word	0x00eb,0x00eb
	mov	al,#0x20		; start of hardware int's (0x20)
	out	#0x21,al
	.word	0x00eb,0x00eb
	mov	al,#0x28		; start of hardware int's 2 (0x28)
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0x04		; 8259-1 is master
	out	#0x21,al
	.word	0x00eb,0x00eb
	mov	al,#0x02		; 8259-2 is slave
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0x01		; 8086 mode for both
	out	#0x21,al
	.word	0x00eb,0x00eb
	out	#0xA1,al
	.word	0x00eb,0x00eb
	mov	al,#0xFF		; mask off all interrupts for now
	out	#0x21,al
	.word	0x00eb,0x00eb
	out	#0xA1,al

; well, that certainly wasn't fun :-(. Hopefully it works, and we don't
; need no steenking BIOS anyway (except for the initial loading :-).
; The BIOS-routine wants lots of unnecessary data, and it's less
; "interesting" anyway. This is how REAL programmers do it.
;
; Well, now's the time to actually move into protected mode. To make
; things as simple as possible, we do no register set-up or anything,
; we let the gnu-compiled 32-bit programs do that. We just jump to
; absolute address 0x00000, in 32-bit protected mode.

; 现在是时候进入保护模式了
; 这里设置进入32位保护模式运行，首先加载机器状态字(lmsw-Load Machine Status Word)，也称
; 控制寄存器 CR0，其比特位 0 置 1 将导致 CPU 工作在保护模式。 

	mov	ax,#0x0001	; protected mode (PE) bit 保护模式比特位(PE)。 
	lmsw	ax		; This is it; 就这样加载机器状态字! 
	jmpi	0,8		; jmp offset 0 of segment 8 (cs)  跳转至 cs 段 8，偏移 0 处。
					; 这里会跳转到head.s 处继续执行下去P
					; 这条指令中的'8'是段选择符，用来指定所需使用的描述符项，此处是指gdt中的代码段描述符。'0'是描述符项指定的代码段中的偏移值。

; 我们已经将 system 模块移动到 0x00000 开始的地方，所以这里的偏移地址是 0。这里的段 
; 值的 8 已经是保护模式下的段选择符了，用于选择描述符表和描述符表项以及所要求的特权级。
; 段选择符长度为 16 位（2 字节）；位 0-1 表示请求的特权级 0-3，linux 操作系统只 
; 用到两级：0 级（系统级）和 3 级（用户级）；位 2 用于选择全局描述符表(0)还是局部描 
; 述符表(1)；位 3-15 是描述符表项的索引，指出选择第几项描述符。所以段选择符
; 8(0b0000,0000,0000,1000)表示请求特权级 0、使用全局描述符表中的第 1 项，该项指出
; 代码的基地址是 0（参见 209 行），因此这里的跳转指令就会去执行 system 中的代码。



; This routine checks that the keyboard command queue is empty
; No timeout is used - if this hangs there is something wrong with
; the machine, and we probably couldn't proceed anyway.
; 这个代码，确定键盘指令为空
; 下面这个子程序检查键盘命令队列是否为空。这里不使用超时方法 - 如果这里死机，
; 说明 PC 机有问题，我们就没有办法再处理下去了。
; 只有当输入缓冲器为空时（状态寄存器位2 = 0）才可以对其进行写命令。


empty_8042:
	.word	0x00eb,0x00eb   ; 这是两个跳转指令的机器码(跳转到下一句)，相当于延时空操作。
	in	al,#0x64	; 8042 status port  读AT 键盘控制器状态寄存器。 
	test	al,#2		; is input buffer full? 测试位2，输入缓冲器满？
	jnz	empty_8042	; yes - loop
	ret

;  全局描述符表开始处。描述符表由多个 8 字节长的描述符项组成。           
; 这里给出了 3 个描述符项。第 1 项无用（206 行），但须存在。第 2 项是系统代码段           
; 描述符（208-211 行），第 3 项是系统数据段描述符(213-216 行)。每个描述符的具体           
; 含义参见列表后说明。


gdt:
	.word	0,0,0,0		; dummy   ! 第 1 个描述符，不用。 
						;  这里在 gdt 表中的偏移量为 0x08，当加载代码段寄存器(段选择符)时，使用的是这个偏移值。

	.word	0x07FF		; 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		; base address=0
	.word	0x9A00		; code read/exec
	.word	0x00C0		; granularity=4096, 386
						;  这里在 gdt 表中的偏移量是 0x10，当加载数据段寄存器(如 ds 等)时，使用的是这个偏移值。

	.word	0x07FF		; 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		; base address=0
	.word	0x9200		; data read/write
	.word	0x00C0		; granularity=4096, 386

idt_48:
	.word	0			; idt limit=0
	.word	0,0			; idt base=0L

gdt_48:
	.word	0x800		; gdt limit=2048, 256 GDT entries
						; 全局表长度为 2k 字节，因为每 8 字节组成一个段描述符项         ! 所以表中共可有 256 项。
	.word	512+gdt,0x9	; gdt base = 0X9xxxx
						;  4 个字节构成的内存线性地址：0x0009<<16 + 0x0200+gdt   ! 也即 0x90200 + gdt(即在本程序段中的偏移地址，205 行)。
	
.text
endtext:
.data
enddata:
.bss
endbss:
