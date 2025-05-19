#ifndef __SYSTEM_H
#define __SYSTEM_H

typedef int size_t;

/* This defines what the stack looks like after an ISR was running */
struct regs
{
    unsigned int gs, fs, es, ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags, useresp, ss;    
};

/* MAIN.C */
extern void *memcpy(void *dest, const void *src, size_t count);
extern void *memset(void *dest, char val, size_t count);
extern unsigned short *memsetw(unsigned short *dest, unsigned short val, size_t count);
extern size_t strlen(const char *str);
extern int strcmp(const char *str1, const char *str2);
extern char *strcpy(char *dst, const char *src);
extern char *strncpy(char *dst, const char *src, size_t n);
extern char *strcat(char *dst, const char *src);
extern char *strncat(char *dst, const char *src, size_t n);
extern unsigned char inportb (unsigned short _port);
extern void outportb (unsigned short _port, unsigned char _data);
extern void uitoa(unsigned int value, char *buf);
extern void check_and_process_input();

/* CONSOLE.C */
extern void init_video(void);
extern void puts(unsigned char *text);
extern void putch(unsigned char c);
extern void cls();
extern void settextcolor(unsigned char forecolor, unsigned char backcolor);

/* GDT.C */
extern void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);
extern void gdt_install();

/* IDT.C */
extern void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags);
extern void idt_install();

/* ISRS.C */
extern void isrs_install();
extern void fault_handler(struct regs *r);

/* IRQ.C */
extern void irq_install_handler(int irq, void (*handler)(struct regs *r));
extern void irq_uninstall_handler(int irq);
extern void irq_install();
extern void irq_handler(struct regs *r);

/* TIMER.C */
extern void timer_wait(int ticks);
extern void timer_install();
extern void timer_handler(struct regs *r);

/* KEYBOARD.C */
extern void keyboard_install();
extern void keyboard_handler(struct regs *r);

/* COMMANDS.C */
#define COMMAND_BUFFER_SIZE 256
#define PATH_MAX_LEN 256

extern char current_path[PATH_MAX_LEN];

extern char command_buffer[COMMAND_BUFFER_SIZE];
extern int command_buffer_pos;

extern void process_command(char *input);

#include "fat.h"
extern Fat* mounted_fat;
extern DiskOps ram_disk_ops;
extern Fat primary_fat_volume;
extern const char* fat_get_error(int err);

#endif
