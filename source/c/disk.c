#include "disk.h"
#include "system.h"

unsigned char ram_disk[TOTAL_SECTORS * SECTOR_SIZE];

// https://gist.github.com/apparentlymart/88fa7e59ace5607c284e

void format_fat32_ramdisk(void) {
    unsigned char *b = ram_disk;
    unsigned int worst_clusters, fatsz, data_sectors;
    unsigned int actual_clusters, data_start;
    unsigned int i;

    /* --- Boot Sector (sector 0) --- */
    memset(b, 0, SECTOR_SIZE);
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;                /* JMP op   */
    memcpy(b + 3, "RAMDISK  ", 8);                        /* OEM name */

    /* --- Basic parameter block  --- */
    *(unsigned short*)(b + 11) = SECTOR_SIZE;             /* bytes/sector */
    b[13] = SECTORS_PER_CLUSTER;                          /* sectors/cluster */
    *(unsigned short*)(b + 14) = RESERVED_SECTORS;        /* reserved sectors */
    b[16] = FAT_COUNT;                                    /* #FATs */
    *(unsigned short*)(b + 17) = 0;                       /* root entries */
    *(unsigned short*)(b + 19) = 0;                       /* small total */
    b[21] = 0xF8;                                         /* media */
    *(unsigned short*)(b + 22) = 0;                       /* FAT16 size */
    *(unsigned short*)(b + 24) = 63;                      /* sectors/track */
    *(unsigned short*)(b + 26) = 255;                     /* heads */
    *(unsigned int*)(b + 28) = 0;                         /* hidden */
    *(unsigned int*)(b + 32) = TOTAL_SECTORS;             /* total sectors */

    /* compute worst-case clusters if no FAT at all */
    worst_clusters = (TOTAL_SECTORS - RESERVED_SECTORS) / SECTORS_PER_CLUSTER;
    /* size of one FAT to cover them */
    fatsz = ((worst_clusters + 2) * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    /* now remove both FATs from pool and recompute how many clusters really fit */
    data_sectors = TOTAL_SECTORS
                 - RESERVED_SECTORS
                 - FAT_COUNT * fatsz;
    actual_clusters = data_sectors / SECTORS_PER_CLUSTER;

    /* write the FAT32‐specific fields */
    *(unsigned int*)(b + 36) = fatsz;                      /* FAT size */
    *(unsigned short*)(b + 40) = 0;                       /* ext flags */
    *(unsigned short*)(b + 42) = 0;                       /* version */
    *(unsigned int*)(b + 44) = 2;                         /* root cluster */
    *(unsigned short*)(b + 48) = 1;                       /* FSInfo sector */
    *(unsigned short*)(b + 50) = 6;                       /* backup boot */
    b[510] = 0x55; b[511] = 0xAA;                         /* signature */
    
    /* --- Write the FAT32 type label --- */
    memcpy(b + 82, "FAT32   ", 8);

    /* --- FSInfo (sector 1) --- */
    {
      unsigned char *fsi = ram_disk + SECTOR_SIZE * 1;
      memset(fsi, 0, SECTOR_SIZE);
      *(unsigned int*)(fsi +   0) = 0x41615252;           /* lead sig */
      *(unsigned int*)(fsi + 484) = 0x61417272;           /* struct sig */
      *(unsigned int*)(fsi + 488) = actual_clusters;      /* free clusters */
      *(unsigned int*)(fsi + 492) = 2;                    /* next free */
      *(unsigned int*)(fsi + 508) = 0xAA550000;           /* trail sig */
    }

    /* backup boot sector (sector 6) */
    memcpy(ram_disk + SECTOR_SIZE * 6,
           ram_disk + SECTOR_SIZE * 0,
           SECTOR_SIZE);

    /* init both FAT tables */
    for (i = 0; i < FAT_COUNT; i++) {
      unsigned char *fat = ram_disk
        + SECTOR_SIZE * (RESERVED_SECTORS + i * fatsz);
      memset(fat, 0, fatsz * SECTOR_SIZE);
      *(unsigned int*)(fat + 0) = 0x0FFFFFF8;            /* media + EOC */
      *(unsigned int*)(fat + 4) = 0x0FFFFFFF;            /* EOC */
      *(unsigned int*)(fat + 8) = 0x0FFFFFFF;            /* root EOC */
    }

    /* clear data region */
    data_start = RESERVED_SECTORS + FAT_COUNT * fatsz;
    memset(ram_disk + data_start * SECTOR_SIZE,
           0,
           (TOTAL_SECTORS - data_start) * SECTOR_SIZE);
}

int ram_read_sector(unsigned char* buf, unsigned int sect) {
    if (sect >= TOTAL_SECTORS) {
        return 0; // error
    }
    memcpy(buf, ram_disk + sect * SECTOR_SIZE, SECTOR_SIZE);
    return 1; // success
}

int ram_write_sector(const unsigned char* buf, unsigned int sect) {
     if (sect >= TOTAL_SECTORS) {
        return 0; // error
    }
    memcpy(ram_disk + sect * SECTOR_SIZE, buf, SECTOR_SIZE);
    return 1; // success
}

DiskOps ram_disk_ops = {
    .read = ram_read_sector,
    .write = ram_write_sector
};