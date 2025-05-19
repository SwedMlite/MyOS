
#ifndef _DISK_H
#define _DISK_H

#include "system.h"

#define SECTOR_SIZE        512
#define DISK_SIZE_BYTES    (33 * 1024 * 1024)
#define TOTAL_SECTORS      (DISK_SIZE_BYTES / SECTOR_SIZE)
#define RESERVED_SECTORS   32
#define FAT_COUNT          2
#define SECTORS_PER_CLUSTER 1

void format_fat32_ramdisk(void);
int ram_read_sector(unsigned char* buf, unsigned int sect);
int ram_write_sector(const unsigned char* buf, unsigned int sect);

extern unsigned char ram_disk[TOTAL_SECTORS * SECTOR_SIZE];

#endif