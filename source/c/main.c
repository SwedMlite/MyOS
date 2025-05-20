#include "system.h"
#include "disk.h"

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && (*str1 == *str2))
    {
        str1++;
        str2++;
    }
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

char *strcpy(char *dst, const char *src) {
    char *p = dst;
    while ((*p++ = *src++));
    return dst;
}

size_t strlen(const char *str)
{
    size_t retval;
    for (retval = 0; *str != '\0'; str++)
        retval++;
    return retval;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *p = dst;
    size_t i = 0;
    for (; i < n && src[i]; ++i) p[i] = src[i];
    for (; i < n; ++i) p[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *p = dst + strlen(dst);
    while ((*p++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *p = dst + strlen(dst);
    size_t i = 0;
    for (; i < n && src[i]; ++i) p[i] = src[i];
    p[i] = '\0';
    return dst;
}

void uitoa(unsigned int value, char *buf)
{
    char tmp[12];
    int i = 0, j;

    if (value == 0)
    {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }

    while (value > 0)
    {
        tmp[i++] = '0' + (value % 10);
        value /= 10;
    }

    for (j = 0; j < i; ++j)
    {
        buf[j] = tmp[i - j - 1];
    }
    buf[i] = '\0';
}

void *memcpy(void *dest, const void *src, size_t count)
{
    const char *sp = (const char *)src;
    char *dp = (char *)dest;
    for (; count != 0; count--)
        *dp++ = *sp++;
    return dest;
}

void *memset(void *dest, char val, size_t count)
{
    char *temp = (char *)dest;
    for (; count != 0; count--)
        *temp++ = val;
    return dest;
}

unsigned short *memsetw(unsigned short *dest, unsigned short val, size_t count)
{
    unsigned short *temp = (unsigned short *)dest;
    for (; count != 0; count--)
        *temp++ = val;
    return dest;
}

int memcmp(const void *str1, const void *str2, size_t count)
{
    register const unsigned char *s1 = (const unsigned char *)str1;
    register const unsigned char *s2 = (const unsigned char *)str2;

    while (count-- > 0)
    {
        if (*s1++ != *s2++)
            return s1[-1] < s2[-1] ? -1 : 1;
    }
    return 0;
}

unsigned char inportb(unsigned short _port)
{
    unsigned char rv;
    __asm__ __volatile__("inb %1, %0" : "=a"(rv) : "dN"(_port));
    return rv;
}

void outportb(unsigned short _port, unsigned char _data)
{
    __asm__ __volatile__("outb %1, %0" : : "dN"(_port), "a"(_data));
}

void update_prompt(void) {
    puts(current_path);
    puts(" > ");
}

void check_and_process_input()
{
    int i;
    for (i = 0; i < command_buffer_pos; i++)
    {
        if (command_buffer[i] == '\n')
        {
            process_command(command_buffer);

            command_buffer_pos = 0;
            memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
            update_prompt();
            return;
        }
    }

    if (command_buffer_pos >= COMMAND_BUFFER_SIZE - 1)
    {
        puts("\nError: Command too long.\n");
        command_buffer_pos = 0;
        memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
        update_prompt();
    }
}

void fat_get_timestamp(Timestamp* ts)
{
  ts->day   = rtc_get_day();
  ts->month = rtc_get_month();
  ts->year  = rtc_get_year();
  ts->hour  = rtc_get_hour();
  ts->min   = rtc_get_minute();
  ts->sec   = rtc_get_second();
}

void main()
{
    gdt_install();
    idt_install();
    isrs_install();
    irq_install();
    init_video();
    timer_install();
    keyboard_install();
    rtc_install();

    memset(command_buffer, 0, COMMAND_BUFFER_SIZE);

    format_fat32_ramdisk();
    
    char buf[12];
    puts("RAM disk formatted as FAT32 (");
    uitoa(TOTAL_SECTORS * SECTOR_SIZE, buf);
    puts((unsigned char *)buf);
    puts(" bytes)\n");

    handle_mount("");

    __asm__ __volatile__("sti");

    update_prompt();

    while (1)
    {
        check_and_process_input();
    }
}
