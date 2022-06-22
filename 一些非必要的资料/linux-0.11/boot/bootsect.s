;
; SYS_SIZE is the number of clicks (16 bytes) to be loaded.
; 0x3000 is 0x30000 bytes = 196kB, more than enough for current
; versions of linux
;
SYSSIZE = 0x3000 ;编译连接后的system 模块大小，这里直接设置一个最大的默认值
;
;	bootsect.s		(C) 1991 Linus Torvalds
;
; bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
; iself out of the way to address 0x90000, and jumps there.
;
; It then loads 'setup' directly after itself (0x90200), and the system
; at 0x10000, using BIOS interrupts. 
;
; NOTE; currently system is at most 8*65536 bytes long. This should be no
; problem, even in the future. I want to keep it simple. This 512 kB
; kernel size should be enough, especially as this doesn't contain the
; buffer cache as in minix
;
; The loader has been made as simple as possible, and continuos
; read errors will result in a unbreakable loop. Reboot by hand. It
; loads pretty fast by getting whole sectors at a time whenever possible.

.globl begtext, begdata, begbss, endtext, enddata, endbss  ;定义全局标志符
.text  ;定义文本数据段
begtext:
.data  ;定义数据段
begdata: 
.bss ; 未初始化数据段(Block Started by Symbol)；
begbss: 
.text ; 文本段

SETUPLEN = 4				; nr of setup-sectors
BOOTSEG  = 0x07c0			; original address of boot-sector; bios 加载的原始地址
INITSEG  = 0x9000			; we move boot here - out of the way ;之后的迁移地址
SETUPSEG = 0x9020			; setup starts here ;迁移后的起始地址
SYSSEG   = 0x1000			; system loaded at 0x10000 (65536). ;系统模块加载的地址
ENDSEG   = SYSSEG + SYSSIZE		; where to stop loading ;最终的末尾地址

; ROOT_DEV:	0x000 - same type of floppy as boot.
;		0x301 - first partition on first drive etc; 
ROOT_DEV = 0x306 ;定义ROOT_DEV 为第二个磁盘的第一个分区

entry start
start:
	mov	ax,#BOOTSEG   	;加载开始地址，放入ax
	mov	ds,ax			;将ax值拷贝到ds,将ds段寄存器值指向开始的地址0x7c0
	mov	ax,#INITSEG		;将目标地址存入es段寄存器
	mov	es,ax   ;设置目标地址
	mov	cx,#256   ; 设置计数器的值为256 
	sub	si,si ; 清空si寄存器
	sub	di,di ; 重置di寄存器


; 开始移动自身
	rep    ; 重复执行，直到cx=0 将自身移动到 0x9000  处
	movw ; 移动一个字节， 

; 重新执行代码
	jmpi	go,INITSEG ; 间接跳转 到目标地址，之后再次执行
go:	mov	ax,cs 	;ds、es、ss、 都设置成移动后的地址(0x9000)
	mov	ds,ax ; 将数据段地址指向0x9000
	mov	es,ax ; 设置es 寄存器
; put stack at 0x9ff00.
	mov	ss,ax ; 设置栈段为0x9000
	mov	sp,#0xFF00		; 将栈顶部指针指向0xFF00 远远大于0x9000的地方 arbitrary value >>512




; 开始加载setup.s
; load the setup-sectors directly after the bootblock.
; Note that 'es' is already set up.
; 紧跟着boot.s代码段之后加载setup.s段
; 一下代码的主要用途，是利用BIOS中断 INT 0x13将setup 模块从磁盘第二个扇区
; 开始读取到0x90200处，读取扇区时，如果出错，则复位驱动器，并进行重试
; INT  0x13的使用方法如下：
; 读取指定扇区
; ah = 0x02 - 读取数据大小， al = 需要读取的数量
; ch = 道(柱面)号的低 8 位；   cl = 开始扇区(0-5 位)，磁道号高 2 位(6-7);
; h = 磁头号；                 dl = 驱动器号（如果是硬盘则位 7 要置位）；
; s:bx -> 指向数据缓冲区；  如果出错则 CF 标志置位。

load_setup:
	mov	dx,#0x0000		; drive 0, head 0 ;dx 数据寄存器设置为0
	mov	cx,#0x0002		; sector 2, track 0;cx 程序计数器设置为2,表示重复两次
	mov	bx,#0x0200		; address = 512, in INITSEG 设置指定定制为 0x0200
	mov	ax,#0x0200+SETUPLEN	; service 2, nr of sectors  设置地址为扇区 0x0200 + 扇区数量
	int	0x13			; read it 调用0x13 中断
	jnc	ok_load_setup		; ok - continue ;调用成功继续执行
	mov	dx,#0x0000 ; 调用失败重新开始
	mov	ax,#0x0000		; reset the diskette
	int	0x13
	j	load_setup

ok_load_setup:

; Get disk drive parameters, specifically nr of sectors/track
; 系统调用成功后，读取磁盘驱动器的参数，包含每道扇区的数量
; 进行相关设备的加载和读取

	mov	dl,#0x00 ; 将dl 设置为0--驱动器号
	mov	ax,#0x0800		; AH=8 is get drive parameters， 磁盘驱动参数
;返回信息：
;如果出错则 CF 置位，并且 ah = 状态码。    
; ah = 0， al = 0，        bl = 驱动器类型（AT/PS2）     
; ch = 最大磁道号的低 8 位，cl = 每磁道最大扇区数(位 0-5)，最大磁道号高 2 位(位 6-7)     
; dh = 最大磁头数，        dl = 驱动器数量，     
; es:di -> 软驱磁盘参数表。

	int	0x13 ; 再次调用磁盘读取
	mov	ch,#0x00 ;设置ch 为0
	seg cs ; 表示下一条语句的操作数在 cs 段寄存器所指的段中。
	mov	sectors,cx ;将cx 值存储到sectors 对应位置--保存最每个磁道的扇区数量
	mov	ax,#INITSEG ; 设置ax为 0x9000
	mov	es,ax ; 设置es为0x9000

; Print some inane message
; 答应相关的磁盘信息
	mov	ah,#0x03		; read cursor pos
	xor	bh,bh			; 重置光标为止
	int	0x10 	; 调用系统中断进行打印
	 
	mov	cx,#24 ; cx 设置为24 打印24个字母
	mov	bx,#0x0007		; page 0, attribute 7 (normal)
	mov	bp,#msg1 	; 基础指针指向msg1
	mov	ax,#0x1301		; write string, move cursor
	int	0x10		;字符串并移动光标，直到cx为0

; ok, we've written the message, now
; we want to load the system (at 0x10000)
; 现在开始将 system模块加载到 0x10000(64k)处
	mov	ax,#SYSSEG ;初始化ax为0x10000
	mov	es,ax		; segment of 0x010000,重新设置段寄存器地址
	call	read_it ; 调用read_it 子程序 读取磁盘上system 模块，到es指向地址
	call	kill_motor ; 调用kill_motor子程序 关闭动器马达，这样就可以知道驱动器的状态了

; After that we check which root-device to use. If the device is
; defined (!= 0), nothing is done and the given device is used.
; Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
; on the number of sectors that the BIOS reports currently.

; 此后，我们检查要使用哪个根文件系统设备(根设备)。
; 如果根设备已经存次，直接使用
; 否则根据BIOS 报告的磁盘扇区数量来确定使用/dev/PS0(2,28),还是 /dev/at0(2,8)
; 一行中两个设备文件的含义： 
;  Linux中软驱的主设备号是 2(参见第 43 行的注释)，次设备号 = type*4 + nr，其中
; r 为0-3 分别对应软驱 A、B、C 或 D；type 是软驱的类型（2 -> 1.2M 或 7 -> 1.44M 等）
; 7*4 + 0 = 28，所以 /dev/PS0 (2,28)指的是 1.44M A 驱动器,其设备号是 0x021c
; 同理 /dev/at0 (2,8)指的是 1.2M A驱动器，其设备号是0x0208。
 
	seg cs    ;表示下一条语句的操作数载cs段寄存器所指的段中
	mov	ax,root_dev ; 设置根设备号--第二个磁盘，第一个分析
	cmp	ax,#0 ;检查root_dev 是否为0
	jne	root_defined ; 如果不为0 直接 跳转到root_defined
	seg cs ;表示下一条语句的操作数载cs段寄存器所指的段中
	mov	bx,sectors ; 将磁道扇区数取出到bx,
	mov	ax,#0x0208		; /dev/ps0 - 1.2Mb ; 设置默认值为
	cmp	bx,#15 ; 如果 sectors=15-- 判断每磁道扇区数是否=15 说明是1.2Mb 的驱动器；
	je	root_defined  ;如果等于，则 ax 中就是引导驱动器的设备号。
	mov	ax,#0x021c		; /dev/PS0 - 1.44Mb ;重新设置磁盘地址
	cmp	bx,#18 		;如果相同
	je	root_defined ; 继续执行初始化
undef_root:  ; 如果都不一样，则死循环（死机）--磁盘初始化失败
	jmp undef_root ; 一直循环
root_defined:
	seg cs
	mov	root_dev,ax ; 将root_dev 设置为对应的硬件磁盘地址

; after that (everyting loaded), we jump to
; the setup-routine loaded directly after
; the bootblock:

; 到此，所有程序都加载完毕，跳转到setup.s 中继续执行
	jmpi	0,SETUPSEG ;转到 0x9020:0000(setup.s 程序的开始处)。 

; boot.s 引导段结束

; This routine loads the system at address 0x10000, making sure
; no 64kB boundaries are crossed. We try to load it as fast as
; possible, loading whole tracks whenever we can.
;
; in:	es - starting address segment (normally 0x1000)
;

; 子程序将系统模块加载到内存地址0x10000 处，并确定没有跨越 64KB的内存边界。我们试图尽快
; 进行加载，只要可能，就每次加载整条磁道的数据。
; 输入：es – 开始内存地址段值（通常是 0x1000）

sread:	.word 1+SETUPLEN	; sectors read of current track 
							; 当前磁道中已读的扇区数。开始时已经读进 1 扇区的引导扇区
							; bootsect 和setup 程序所占的扇区数 SETUPLEN
head:	.word 0			; current head 当前磁头号
track:	.word 0			; current track 当前磁道号


; read_it ====== start 


read_it:
	mov ax,es ; 设置目标地址
	test ax,#0x0fff ; 测试输入值是否在内存地址64KB的边界处开始
die:	jne die			; es must be at 64kB boundary ; es指向不是64KB边界，进入死循环
	xor bx,bx		; bx is starting address within segment ; 确认之后，重新设置bx为段内偏移
rp_read:
; 判断是否已经读入全部数据。比较当前所读段是否就是系统数据末端所处的段(#ENDSEG)，如果不是就  
; 跳转至下面 ok1_read 标号处继续读数据。否则退出子程序返回。
	mov ax,es
	cmp ax,#ENDSEG		; have we loaded all yet?
	jb ok1_read ; 不是就跳转至ok1_read 继续读取
	ret ; 否则进行返回
ok1_read:
; 计算和验证当前磁道需要读取的扇区数，放在 ax 寄存器中。    
; 根据当前磁道还未读取的扇区数以及段内数据字节开始偏移位置，计算如果全部读取这些未读扇区，    
; 所读总字节数是否会超过 64KB 段长度的限制。若会超过，则根据此次最多能读入的字节数(64KB – 段内 

	seg cs ;
	mov ax,sectors  ; 获取磁道的扇区数
	sub ax,sread ; 减去前面已经读取的bootsec.s和setup.s对应的容量，扇区数目--目标读取扇区数目
	mov cx,ax    ; cx = ax = 未读取的扇区数量
	shl cx,#9	 ; x = cx * 512(2^9) 字节。
	add cx,bx 	; cx = cx + 段内当前偏移值(bx)  = 此次操作一共需要读取的字节数
	jnc ok2_read ; 没有超过64KB 
	je ok2_read ; 跳转至ok2_read 执行
	xor ax,ax ; 此次将读磁道上所有未读扇区时会超过 64KB，则计算
	sub ax,bx ; 此时最多能读入的字节数(64KB – 段内读偏移位置)，再转换
	shr ax,#9 ; 需要读取的扇区数。
ok2_read:
	call read_track ;调用read_track 函数
	mov cx,ax       ; cx= 该次操作已读取的扇区数。 
	add ax,sread    ; 当前磁道上已经读取的扇区数。
	seg cs 			; 
	cmp ax,sectors ; 存在扇区没有读取，跳转到ok3_read 读取
	jne ok3_read
; 读取该磁道的下一磁头面(1 号磁头)上的数据。如果已经完成，则去读下一磁道。
	mov ax,#1
	sub ax,head ; 当前磁头号是否为1
	jne ok4_read ; 是 0 磁头，继续读取1磁头上面的扇区数据--保证剩余的扇区数据被读完
	inc track  ; 否则继续读取下一个磁道
ok4_read:
	mov head,ax ;保存当前磁头号
	xor ax,ax ; 清空ax，继续进行读取
ok3_read:
	mov sread,ax ; 保存当前已经读取的大小
	shl cx,#9    ; 计算已经读取大小 = cx(已经读取扇区数量) * 512(扇区大小)
	add bx,cx    ; 设置数据段开始位置
	jnc rp_read	 ; 如果还是没有全部读取
	mov ax,es    ; 继续执行读取操作
	add ax,#0x1000 ; 将段基础地址，调整为下一个64KB 内存开始处
	mov es,ax	; 设置目标地址
	xor bx,bx ; 设置已经读取的大小为0
	jmp rp_read ; 再次执行读取

; 读当前磁道上指定开始扇区和需读扇区数的数据到 es:bx 开始处。参见第 67 行下对 BIOS 磁盘读中断     
; int 0x13，ah=2 的说明。     
; al–需读扇区数；es:bx–缓冲区开始位置。


; 读取当前磁道
; https://blog.csdn.net/weixin_45142411/article/details/122984765
read_track:
	; 暂存所有数据
	push ax
	push bx
	push cx
	push dx
	mov dx,track ; 获取当前磁道号
	mov cx,sread ; 当前已经读取扇区数
	inc cx 		 ; cl = 开始读取扇区
	mov ch,dl    ; ch 设置为当前的磁道号
	mov dx,head  ; dx 为当前的磁头号
	mov dh,dl    ; dh = 磁头号
	mov dl,#0    ; dl = 驱动器号(为 0 表示当前 A 驱动器)。
	and dx,#0x0100 ; 磁头号不大于 1。 
	mov ah,#2    ; ah = 2，读磁盘扇区功能号。 
	int 0x13     ; 中断信号进行磁盘数据读取
	jc bad_rt    ; 出错，则跳转至bad_rt。

	; 跳出对应函数
	pop dx
	pop cx
	pop bx
	pop ax
	ret

; 执行驱动器复位操作（磁盘中断功能号 0），再跳转到 read_track 处重试。
bad_rt:	mov ax,#0
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track


; read_it ====== end

/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
; 这个个子程序用于关闭软驱的马达，这样我们进入内核后它处于已知状态，以后也就无须担心它了。

; kill_motor ====== start 

kill_motor:
	push dx
	mov dx,#0x3f2 ; 设置软驱控制卡的端口号
	mov al,#0 ;
	outb      ;  al 中的内容输出到 dx 指定的端口去-- 0 输出到指定端口
	pop dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
