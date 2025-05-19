// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#ifndef FAT_H
#define FAT_H

#define NULL ((void *)0)

enum
{
  FAT_ERR_NONE     =  0,
  FAT_ERR_NOFAT    = -1,
  FAT_ERR_BROKEN   = -2,
  FAT_ERR_IO     = -3,
  FAT_ERR_PARAM    = -4,
  FAT_ERR_PATH     = -5,
  FAT_ERR_EOF      = -6,
  FAT_ERR_DENIED   = -7,
  FAT_ERR_FULL     = -8,
};

enum
{
  FAT_ATTR_NONE     = 0x00,
  FAT_ATTR_RO       = 0x01,
  FAT_ATTR_HIDDEN   = 0x02,
  FAT_ATTR_SYS      = 0x04,
  FAT_ATTR_LABEL    = 0x08,
  FAT_ATTR_DIR      = 0x10,
  FAT_ATTR_ARCHIVE  = 0x20,
  FAT_ATTR_LFN      = 0x0f,
};

enum
{
  FAT_WRITE      = 0x01, // Open file for writing
  FAT_READ       = 0x02, // Open file for reading
  FAT_APPEND     = 0x04, // Set file offset to the end of the file
  FAT_TRUNC      = 0x08, // Truncate the file after opening
  FAT_CREATE     = 0x10, // Create the file if it do not exist

  FAT_ACCESSED   = 0x20, // do not use (internal)
  FAT_MODIFIED   = 0x40, // do not use (internal)
  FAT_FILE_DIRTY = 0x80, // do not use (internal)
};

enum
{
  FAT_SEEK_START,
  FAT_SEEK_CURR,
  FAT_SEEK_END,
};

//------------------------------------------------------------------------------
typedef struct
{
  int (*read)(unsigned char* buf, unsigned int sect);
  int (*write)(const unsigned char* buf, unsigned int sect);
} DiskOps;

typedef struct
{
  unsigned char hour;
  unsigned char min;
  unsigned char sec;
  unsigned char day;
  unsigned char month;
  unsigned short year;
} Timestamp;

typedef struct Fat
{
  struct Fat* next;
  DiskOps ops;
  unsigned int clust_msk;
  unsigned int clust_cnt;
  unsigned int info_sect;
  unsigned int fat_sect[2];
  unsigned int data_sect;  
  unsigned int root_clust;
  unsigned int last_used;
  unsigned int free_cnt;
  unsigned int sect;
  unsigned char buf[512];
  unsigned char flags;
  unsigned char clust_shift;
  unsigned char name_len;
  char name[32];
} Fat;

typedef struct
{
  Timestamp created;
  Timestamp modified;
  unsigned int size;
  unsigned char attr;
  char name[255];
  unsigned char name_len;
} DirInfo;

typedef struct
{
  Fat* fat;
  unsigned int sclust;
  unsigned int clust;
  unsigned int sect;
  unsigned short idx;
} Dir;

typedef struct
{
  Fat* fat;
  unsigned int dir_sect;
  unsigned int sclust;
  unsigned int clust;
  unsigned int sect;
  unsigned int size;
  unsigned int offset;
  unsigned short dir_idx;
  unsigned char attr;
  unsigned char flags;
  unsigned char buf[512];
} File;

//------------------------------------------------------------------------------
const char* fat_get_error(int err);

int fat_probe(DiskOps* ops, int partition);
int fat_mount(DiskOps* ops, int partition, Fat* fat, const char* path);
int fat_umount(Fat* fat);
int fat_sync(Fat* fat);

int fat_stat(const char* path, DirInfo* info);
int fat_unlink(const char* path);

int fat_file_open(File* file, const char* path, unsigned char flags);
int fat_file_close(File* file);
int fat_file_read(File* file, void* buf, int len, int* bytes);
int fat_file_write(File* file, const void* buf, int len, int* bytes);
int fat_file_seek(File* file, int offset, int seek);
int fat_file_sync(File* file);

int fat_dir_create(Dir* dir, const char* path);
int fat_dir_open(Dir* dir, const char* path);
int fat_dir_read(Dir* dir, DirInfo* info);
int fat_dir_rewind(Dir* dir);
int fat_dir_next(Dir* dir);

void fat_get_timestamp(Timestamp* ts);

#endif
