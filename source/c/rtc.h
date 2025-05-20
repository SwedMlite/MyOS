#ifndef __RTC_H
#define __RTC_H

/* RTC port addresses */
#define RTC_INDEX_PORT 0x70
#define RTC_DATA_PORT  0x71

/* RTC register indices */
#define RTC_SECONDS        0x00
#define RTC_MINUTES        0x02
#define RTC_HOURS          0x04
#define RTC_DAY_OF_WEEK    0x06
#define RTC_DAY_OF_MONTH   0x07
#define RTC_MONTH          0x08
#define RTC_YEAR           0x09
#define RTC_STATUS_A       0x0A
#define RTC_STATUS_B       0x0B
#define RTC_STATUS_C       0x0C

/* RTC Status Register B bits */
#define RTC_PIE 0x40 /* Periodic Interrupt Enable */

/* RTC functions */
void rtc_install();
void rtc_enable_interrupt();
void rtc_disable_interrupt();
void rtc_set_rate(unsigned char rate);
void rtc_handler(struct regs *r);

/* RTC time reading functions */
unsigned char rtc_get_second();
unsigned char rtc_get_minute();
unsigned char rtc_get_hour();
unsigned char rtc_get_day();
unsigned char rtc_get_month();
unsigned char rtc_get_year();

#endif 