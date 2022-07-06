/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* interestingly, we assume leap-years */
/* 考虑润年的，每个月开始时的秒数时间数组 */
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};
// 计算从1970 年1月1日到开机当日经过的秒数
long kernel_mktime(struct tm* tm)
{
	long res; // 秒数时间
	int year;
	// 计算年时间
	year = tm->tm_year - 70; // 70年到现在的年份数量(2位数)--千年虫问题
/* magic offsets (y+1) needed to get leapyears right.*/
	res = YEAR*year + DAY*((year+1)/4); // 考虑闰年，因此每个闰年多一天
	res += month[tm->tm_mon]; // 加上前面月分的时间
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
	if (tm->tm_mon>1 && ((year+2)%4)) // 非闰年，因为多算了一天，需要减去
		res -= DAY;
	res += DAY*(tm->tm_mday-1);  // 计算本月天数
	res += HOUR*tm->tm_hour;  // 计算小时
	res += MINUTE*tm->tm_min; // 计算分钟
	res += tm->tm_sec; // 计算秒
	return res;
}
