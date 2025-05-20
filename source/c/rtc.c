#include "system.h"
#include "rtc.h"

/* Track if an RTC interrupt has occurred */
unsigned int rtc_ticks = 0;

/* An important note from the article:
 * It is important to know that upon a IRQ 8, Status Register C will contain a bitmask
 * telling which interrupt happened. If register C is not read after an IRQ 8,
 * then the interrupt will not happen again.
 */

/* RTC interrupt handler */
void rtc_handler(struct regs *r) {
    rtc_ticks++;
    
    /* Must read Status Register C to receive another interrupt */
    outportb(RTC_INDEX_PORT, RTC_STATUS_C);
    inportb(RTC_DATA_PORT); /* Just discard the contents */
}

/* A helper function to safely read/write CMOS/RTC registers */
void rtc_disable_nmi() {
    /* Clear interrupts and disable NMI (by setting the 0x80 bit) */
    __asm__ __volatile__("cli");
}

void rtc_enable_nmi() {
    /* Re-enable interrupts and NMI */
    __asm__ __volatile__("sti");
}

/* Enable the RTC periodic interrupt */
void rtc_enable_interrupt() {
    unsigned char prev;
    
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_STATUS_B);
    prev = inportb(RTC_DATA_PORT);
    outportb(RTC_INDEX_PORT, RTC_STATUS_B);
    outportb(RTC_DATA_PORT, prev | RTC_PIE);
    rtc_enable_nmi();
}

/* Disable the RTC periodic interrupt */
void rtc_disable_interrupt() {
    unsigned char prev;
    
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_STATUS_B);
    prev = inportb(RTC_DATA_PORT);
    outportb(RTC_INDEX_PORT, RTC_STATUS_B);
    outportb(RTC_DATA_PORT, prev & ~RTC_PIE);
    rtc_enable_nmi();
}

/* Set the RTC interrupt rate (3-15)
 * frequency = 32768 >> (rate-1)
 * 
 * Valid rates are from 3 to 15. The default is 6 (1024 Hz).
 * Rate 3 gives 8192 Hz, the maximum reliable rate.
 */
void rtc_set_rate(unsigned char rate) {
    unsigned char prev;
    
    if (rate < 3) rate = 3;
    if (rate > 15) rate = 15;
    
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_STATUS_A);
    prev = inportb(RTC_DATA_PORT);
    outportb(RTC_INDEX_PORT, RTC_STATUS_A);
    outportb(RTC_DATA_PORT, (prev & 0xF0) | rate);
    rtc_enable_nmi();
}

/* Binary-Coded Decimal (BCD) to binary conversion 
 * Many RTC chips store values in BCD format
 */
unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/* Functions to read time values from RTC */
unsigned char rtc_get_second() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_SECONDS);
    unsigned char second = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(second);
}

unsigned char rtc_get_minute() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_MINUTES);
    unsigned char minute = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(minute);
}

unsigned char rtc_get_hour() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_HOURS);
    unsigned char hour = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(hour);
}

unsigned char rtc_get_day() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_DAY_OF_MONTH);
    unsigned char day = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(day);
}

unsigned char rtc_get_month() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_MONTH);
    unsigned char month = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(month);
}

unsigned char rtc_get_year() {
    rtc_disable_nmi();
    outportb(RTC_INDEX_PORT, RTC_YEAR);
    unsigned char year = inportb(RTC_DATA_PORT);
    rtc_enable_nmi();
    return bcd_to_bin(year);
}

/* Initialize RTC with default settings and install handler */
void rtc_install() {
    /* Install the RTC handler for IRQ 8 */
    irq_install_handler(8, rtc_handler);
    
    /* Set the interrupt rate to default (6, which is 1024 Hz) */
    rtc_set_rate(6);
    
    /* Initial read of Status Register C to clear any pending interrupts */
    outportb(RTC_INDEX_PORT, RTC_STATUS_C);
    inportb(RTC_DATA_PORT);
    
    /* Enable the RTC periodic interrupt */
    rtc_enable_interrupt();
    
    /* Reset the tick counter */
    rtc_ticks = 0;
} 