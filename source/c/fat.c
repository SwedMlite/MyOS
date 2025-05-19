// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#include "fat.h"
#include "system.h"

//------------------------------------------------------------------------------
#define LIMIT(a, b) ((a) < (b) ? (a) : (b))

#define FSINFO_HEAD_SIG    0x41615252
#define FSINFO_STRUCT_SIG  0x61417272
#define FSINFO_TAIL_SIG    0xaa550000

#define MBR_PART_OFF  446

#define EXT_FLAG_MIRROR  (1 << 7)
#define EXT_FLAG_ACT     0x000f
#define EXT_FLAG_SECOND  0x0001

#define LFN_HEAD_MSK  0x40
#define LFN_SEQ_MSK   0x1f

#define SFN_FREE  0xe5
#define SFN_LAST  0x00
#define SFN_PAD   0x20

//------------------------------------------------------------------------------
enum
{
  FAT_BUF_DIRTY  = 0x01,
  FAT_INFO_DIRTY = 0x02,
};

enum
{
  CLUST_FREE = 0x01,
  CLUST_USED = 0x02,
  CLUST_LAST = 0x04,
  CLUST_BAD  = 0x08,
};

//------------------------------------------------------------------------------
typedef struct __attribute__((packed))
{
  unsigned char boot[446];
  struct
  {
    unsigned char status;
    unsigned char reserved_0[3];
    unsigned char type;
    unsigned char reserved_1[3];
    unsigned int lba;
    unsigned int size;
  } part[4];
  unsigned short sig;
} Mbr;

typedef struct __attribute__((packed))
{
  unsigned char jump[3];
  char name[8];
  unsigned short bytes_per_sect;
  unsigned char sect_per_clust;
  unsigned short res_sect_cnt;
  unsigned char fat_cnt;
  unsigned short root_ent_cnt;
  unsigned short sect_cnt_16;
  unsigned char media;
  unsigned short sect_per_fat_16;
  unsigned short sect_per_track;
  unsigned short head_cnt;
  unsigned int hidden_sect_cnt;
  unsigned int sect_cnt_32;
  unsigned int sect_per_fat_32;
  unsigned short ext_flags;
  unsigned char minor;
  unsigned char major;
  unsigned int root_cluster;
  unsigned short info_sect;
  unsigned short copy_bpb_sector;
  unsigned char reserved_0[12];
  unsigned char drive_num;
  unsigned char reserved_1;
  unsigned char boot_sig;
  unsigned int volume_id;
  char volume_label[11];
  char fs_type[8];
  unsigned char reserved_2[420];
  unsigned char sign[2];
} Bpb;

typedef struct __attribute__((packed))
{
  unsigned int head_sig;
  unsigned char reserved_0[480];
  unsigned int struct_sig;
  unsigned int free_cnt;
  unsigned int next_free;
  unsigned char reserved_1[12];
  unsigned int tail_sig;
} FsInfo;

typedef struct __attribute__((packed))
{
  unsigned char name[11];
  unsigned char attr;
  unsigned char reserved;
  unsigned char tenth;
  unsigned short cre_time;
  unsigned short cre_date;
  unsigned short acc_date;
  unsigned short clust_hi;
  unsigned short mod_time;
  unsigned short mod_date;
  unsigned short clust_lo;
  unsigned int size;
} Sfn;

typedef union __attribute__((packed))
{
  unsigned char raw[32];
  struct
  {
    unsigned char seq;
    unsigned char name0[10];
    unsigned char attr;
    unsigned char type;
    unsigned char crc;
    unsigned char name1[12];
    unsigned short clust;
    unsigned char name2[4];
  };
} Lfn;

typedef struct
{
  unsigned int sect;
  unsigned short idx;
} Loc;

//------------------------------------------------------------------------------
static Fat* g_fat_list;

static unsigned char g_lfn_indices[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

static unsigned char g_buf[512];
static unsigned short g_len;
static unsigned char g_crc;

//------------------------------------------------------------------------------
static char to_upper(char c)
{
  return (c >= 'a' && c <= 'z') ? c & ~0x20 : c;
}

//------------------------------------------------------------------------------
static int subpath_len(const char* path)
{
  int i;
  for (i = 0; path[i] && path[i] != '/'; i++);
  return i;
}

//------------------------------------------------------------------------------
static int last_subpath_len(const char* path)
{
  int len = subpath_len(path);
  if (len == 0)
    return 0;

  path += len;

  // Verify last subpath
  while (*path == '/')
    path++;

  return *path ? 0 : len;
}

//------------------------------------------------------------------------------
static unsigned char get_crc(const unsigned char* name)
{
  unsigned char sum = 0;
  for (int i = 0; i < 11; i++)
    sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
  return sum;
}

//------------------------------------------------------------------------------
static void decode_timestamp(unsigned short date, unsigned short time, Timestamp* ts)
{
  ts->day = date & 0x1f;
  ts->month = (date >> 5) & 0xf;
  ts->year = ((date >> 9) & 0x3f) + 1980;
  ts->hour = (time >> 11) & 0x1f;
  ts->min = (time >> 5) & 0x3f;
  ts->sec = 2 * (time & 0x1f);
}

//------------------------------------------------------------------------------
static void encode_timestamp(unsigned short* date, unsigned short* time)
{
  Timestamp ts;
  fat_get_timestamp(&ts);
  
  *date = ((ts.year - 1980) & 0x3f) << 9 | (ts.month & 0xf) << 5 | (ts.day & 0x1f);
  *time = ((ts.sec / 2) & 0x1f) | (ts.min & 0x3f) << 5 | (ts.hour & 0x1f) << 11;
}

//------------------------------------------------------------------------------
static Fat* find_fat_volume(const char* name, int len)
{
  for (Fat* it = g_fat_list; it; it = it->next)
  {
    if (len == it->name_len && !memcmp(name, it->name, len))
      return it;
  }

  return NULL;
}

//------------------------------------------------------------------------------
static unsigned int sect_to_clust(Fat* fat, unsigned int sect)
{
  return ((sect - fat->data_sect) >> fat->clust_shift) + 2;
}

//------------------------------------------------------------------------------
static unsigned int clust_to_sect(Fat* fat, unsigned int clust)
{
  return ((clust - 2) << fat->clust_shift) + fat->data_sect;
}

//------------------------------------------------------------------------------
static int sync_buf(Fat* fat)
{
  if (fat->flags & FAT_BUF_DIRTY)
  {
    if (!fat->ops.write(fat->buf, fat->sect))
      return FAT_ERR_IO;

    fat->flags &= ~FAT_BUF_DIRTY;
  }
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int update_buf(Fat* fat, unsigned int sect)
{
  if (fat->sect != sect)
  {
    int err = sync_buf(fat);
    if (err)
      return err;
    
    if (!fat->ops.read(fat->buf, sect))
      return FAT_ERR_IO;

    fat->sect = sect;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int sync_fs(Fat* fat)
{
  int err = sync_buf(fat);
  if (err)
    return err;

  if (fat->flags & FAT_INFO_DIRTY)
  {
    err = update_buf(fat, fat->info_sect);
    if (err)
      return err;

    FsInfo* info = (FsInfo*)fat->buf;
    fat->flags |= FAT_BUF_DIRTY;
    info->next_free = fat->last_used;
    info->free_cnt = fat->free_cnt;

    err = sync_buf(fat);
    if (err)
      return err;

    fat->flags &= ~FAT_INFO_DIRTY;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int get_fat(Fat* fat, unsigned int clust, unsigned int* out_val, unsigned char* out_flags)
{
  unsigned int* items = (unsigned int*)fat->buf;
  unsigned int sect = fat->fat_sect[0] + clust / 128; // Active FAT
  unsigned int idx = clust % 128;

  int err = update_buf(fat, sect);
  if (err)
    return err;

  // Upper nibble is ignored
  unsigned int val = items[idx] & 0x0fffffff;
  unsigned char flags;

  if (val == 0)
    flags = CLUST_FREE;
  else if (val == 0x0ffffff7)
    flags = CLUST_BAD;
  else if (val >= 0x0ffffff8)
    flags = CLUST_USED | CLUST_LAST;
  else if (val >= 2 && val < fat->clust_cnt)
    flags = CLUST_USED;
  else
    return FAT_ERR_BROKEN;

  *out_val = val;
  *out_flags = flags;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int put_fat2(Fat* fat, unsigned int fat_sect, unsigned int clust, unsigned int val)
{
  unsigned int* items = (unsigned int*)fat->buf;
  unsigned int sect = fat_sect + clust / 128;
  unsigned short idx = clust % 128;

  int err = update_buf(fat, sect);
  if (err)
    return err;

  // Upper nibble must be preserved
  items[idx] = (items[idx] & 0xf0000000) | (val & 0x0fffffff);
  fat->flags |= FAT_BUF_DIRTY;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int put_fat(Fat* fat, unsigned int clust, unsigned int val)
{
  if (fat->fat_sect[1]) // Mirroring enabled
  {
    int err = put_fat2(fat, fat->fat_sect[1], clust, val);
    if (err)
      return err;
  }

  return put_fat2(fat, fat->fat_sect[0], clust, val);
}

//------------------------------------------------------------------------------
static int remove_chain(Fat* fat, unsigned int clust)
{
  unsigned char flags;
  unsigned int next;

  fat->flags |= FAT_INFO_DIRTY;

  for (;;)
  {
    int err = get_fat(fat, clust, &next, &flags);
    if (err)
      return err;

    if (flags & (CLUST_BAD | CLUST_FREE))
      return FAT_ERR_BROKEN;

    err = put_fat(fat, clust, 0);
    if (err)
      return err;

    fat->free_cnt++;
    clust = next;

    if (flags & CLUST_LAST)
      break;
  }

  return sync_fs(fat);
}

//------------------------------------------------------------------------------
static int stretch_chain(Fat* fat, unsigned int clust, unsigned int* out_clust)
{
  int err;
  unsigned char flags;
  unsigned int next;
  unsigned int prev = clust;
  int scan = 1;

  fat->flags |= FAT_INFO_DIRTY;

  if (prev)
  {
    // Stretching. Check next cluster.
    if (++clust >= fat->clust_cnt)
      clust = 2;

    err = get_fat(fat, clust, &next, &flags);
    if (err)
      return err;

    if (flags & CLUST_FREE)
      scan = 0;
  }

  if (scan)
  {
    clust = fat->last_used;

    for (;;)
    {
      if (++clust >= fat->clust_cnt)
        clust = 2;

      if (clust == fat->last_used)
        return FAT_ERR_FULL;

      err = get_fat(fat, clust, &next, &flags);
      if (err)
        return flags;

      if (flags & CLUST_FREE)
        break;
    }
  }

  err = put_fat(fat, clust, 0x0fffffff); // EOC
  if (err)
    return err;

  if (prev)
  {
    // Stretching. Add link.
    err = put_fat(fat, prev, clust);
    if (err)
      return err;
  }

  fat->last_used = clust;
  fat->free_cnt--;

  *out_clust = clust;
  return sync_fs(fat);
}

//------------------------------------------------------------------------------
static int create_chain(Fat* fat, unsigned int* out_clust)
{
  return stretch_chain(fat, 0, out_clust);
}

//------------------------------------------------------------------------------
static int clust_clear(Fat* fat, unsigned int clust)
{
  int err = sync_buf(fat);
  if (err)
    return err;
  
  unsigned int sect = clust_to_sect(fat, clust);
  memset(fat->buf, 0, 512);

  for (int i = 0; i < (1 << fat->clust_shift); i++)
  {
    fat->flags |= FAT_BUF_DIRTY;
    fat->sect = sect++;

    err = sync_buf(fat);
    if (err)
      return err;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static void dir_at_clust(Dir* dir, unsigned int clust)
{
  dir->clust = clust;
  dir->sect = clust_to_sect(dir->fat, clust);
  dir->idx = 0;
}

//------------------------------------------------------------------------------
static void dir_enter(Dir* dir, unsigned int clust)
{
  // Cluster is zero for .. entries pointing to root
  if (clust == 0)
    clust = dir->fat->root_clust;

  dir->sclust = clust;
  dir_at_clust(dir, clust);
}

//------------------------------------------------------------------------------
static int dir_next(Dir* dir)
{
  dir->idx += sizeof(Sfn);

  if (dir->idx < 512)
    return FAT_ERR_NONE;

  dir->idx = 0;
  dir->sect++;

  if (dir->sect & dir->fat->clust_msk) // Still in same cluster
    return FAT_ERR_NONE;

  unsigned char flags;
  unsigned int next;

  int err = get_fat(dir->fat, dir->clust, &next, &flags);
  if (err)
    return err;

  if (flags & (CLUST_BAD | CLUST_FREE))
    return FAT_ERR_BROKEN;
  
  if (flags & CLUST_LAST) // EOC
    return FAT_ERR_EOF;

  dir_at_clust(dir, next);
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int dir_advance(Dir* dir, int cnt)
{
  for (int i = 0; i < cnt; i++)
  {
    int err = dir_next(dir);
    if (err)
      return err;
  }
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int dir_next_stretch(Dir* dir)
{
  int err = dir_next(dir);
  if (err != FAT_ERR_EOF)
    return err;

  unsigned int next;
  err = stretch_chain(dir->fat, dir->clust, &next);
  if (err)
    return err;

  dir_at_clust(dir, next);
  return clust_clear(dir->fat, dir->clust);
}

//------------------------------------------------------------------------------
static void* dir_ptr(Dir* dir)
{
  return dir->fat->buf + dir->idx;
}

//------------------------------------------------------------------------------
static int sfn_is_last(Sfn* sfn)
{
  return sfn->name[0] == SFN_LAST;
}

//------------------------------------------------------------------------------
static int sfn_is_free(Sfn* sfn)
{
  return sfn->name[0] == SFN_LAST || sfn->name[0] == SFN_FREE;
}

//------------------------------------------------------------------------------
static int sfn_is_lfn(Sfn* sfn)
{
  return sfn->attr == FAT_ATTR_LFN;
}

//------------------------------------------------------------------------------
static unsigned int sfn_cluster(Sfn* sfn)
{
  return sfn->clust_hi << 16 | sfn->clust_lo;
}

//------------------------------------------------------------------------------
// Only certain characters are allowed in an SFN file name. Invalid characters 
// are converted to underscore. It does not follow Windows' algorithm, using
// ~N for duplicate names, since it relies on LFN names only.

static char sfn_char(char c)
{
  const char* str = "!#$%&'()-@^_`{}~ "; // Allowed special characters

  c = to_upper(c);
  if (c >= 'A' && c <= 'Z')
    return c;
  
  for (int i = 0; str[i]; i++)
  {
    if (c == str[i])
      return c;
  }

  return '_';
}

//------------------------------------------------------------------------------
static void put_sfn_name(unsigned char* sfn_name, const char* name, int len)
{
  int i, j;

  for (i = 0; i < LIMIT(len, 8) && name[i] != '.'; i++)
    sfn_name[i] = sfn_char(name[i]);

  for (j = i; j < 8; j++)
    sfn_name[j] = SFN_PAD;

  while (i < len && name[i++] != '.');

  for (j = 0; j < 3 && i < len; j++, i++)
    sfn_name[8 + j] = sfn_char(name[i]);

  for (; j < 3; j++)
    sfn_name[8 + j] = SFN_PAD;
}

//------------------------------------------------------------------------------
static void parse_sfn_name(unsigned char* sfn_name)
{
  unsigned char* ptr = g_buf;

  for (int i = 0; i < 8 && sfn_name[i] != SFN_PAD; i++)
    *ptr++ = sfn_name[i];

  if (sfn_name[8] != SFN_PAD)
    *ptr++ = '.';

  for (int i = 8; i < 11 && sfn_name[i] != SFN_PAD; i++)
    *ptr++ = sfn_name[i];

  g_len = ptr - g_buf;
}

//------------------------------------------------------------------------------
static void put_lfn_name_frag(Lfn* lfn, const char* name, int len)
{
  int i;
  for (i = 0; i < len; i++)
  {
    lfn->raw[g_lfn_indices[i] + 0] = name[i];
    lfn->raw[g_lfn_indices[i] + 1] = 0x00;
  }
  
  if (i < 13)
  {
    lfn->raw[g_lfn_indices[i] + 0] = 0x00;
    lfn->raw[g_lfn_indices[i] + 1] = 0x00;

    while (++i < 13)
    {
      lfn->raw[g_lfn_indices[i] + 0] = 0xff;
      lfn->raw[g_lfn_indices[i] + 1] = 0xff;
    }
  }
}

//------------------------------------------------------------------------------
static int parse_lfn_name(Dir* dir)
{
  int err = update_buf(dir->fat, dir->sect);
  if (err) return err;
  
  Lfn* lfn = dir_ptr(dir);
  g_crc = lfn->crc;
  g_len = 0;

  if (0 == (lfn->seq & LFN_HEAD_MSK))
    return FAT_ERR_BROKEN;

  int cnt = lfn->seq & LFN_SEQ_MSK;
  if (cnt > 20)
    return FAT_ERR_BROKEN;

  while (cnt--)
  {
    if (lfn->attr != FAT_ATTR_LFN || lfn->crc != g_crc)
      return FAT_ERR_BROKEN;

    for (int i = 0; i < 13; i++)
    {
      char c = lfn->raw[g_lfn_indices[i]];

      if (c == 0xff)
        return FAT_ERR_BROKEN; // 0x00 must be first

      if (c == 0x00)
        break;

      g_buf[13 * cnt + i] = c;
      g_len++;
    }

    err = dir_next(dir);
    if (err)
      return err;

    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;

    lfn = dir_ptr(dir);
  }

  return g_len <= 255 ? FAT_ERR_NONE : FAT_ERR_BROKEN;
}

//------------------------------------------------------------------------------
static int dir_search(Dir* dir, const char* name, int len, Loc* loc)
{
  // Should it support matching SFN?
  unsigned char sfn_name[11];
  put_sfn_name(sfn_name, name, len);

  dir_at_clust(dir, dir->sclust);

  for (int err = 0;; err = dir_next(dir))
  {
    if (err)
      return err;

    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;

    Sfn* sfn = dir_ptr(dir);

    if (sfn_is_last(sfn))
      return FAT_ERR_EOF;
    
    if (sfn_is_free(sfn))
      continue;

    if (loc)
    {
      // Update the start location (SFN or first LFN). Used when removing entries.
      loc->sect = dir->sect;
      loc->idx = dir->idx;
    }

    if (sfn_is_lfn(sfn))
    {
      err = parse_lfn_name(dir);
      if (err)
        return err;

      sfn = dir_ptr(dir);

      if (sfn_is_free(sfn) || sfn_is_lfn(sfn) || g_crc != get_crc(sfn->name))
        return FAT_ERR_BROKEN;

      if (g_len == len && !memcmp(g_buf, name, len))
        return FAT_ERR_NONE;
    }
    else
    {
      if (!memcmp(sfn_name, sfn->name, sizeof(sfn_name)))
        return FAT_ERR_NONE;
    }
  }
}

//------------------------------------------------------------------------------
static int dir_at_root(Dir* dir)
{
  return dir->clust == dir->fat->root_clust &&
    dir->sect == clust_to_sect(dir->fat, dir->fat->root_clust) && 
    dir->idx == 0;
}

//------------------------------------------------------------------------------
static int follow_path(Dir* dir, const char** path, Loc* loc)
{
  int err, len;
  unsigned int dir_clust;
  int dir_enterable;
  const char* str = *path;

  if (*str++ != '/')
    return FAT_ERR_PATH;
  len = subpath_len(str);
  if (len == 0)
    return FAT_ERR_PATH;

  dir->fat = find_fat_volume(str, len);
  if (!dir->fat)
    return FAT_ERR_PATH;

  // Enter root by default (no entry points to it)
  dir_enter(dir, dir->fat->root_clust);
  dir_clust = dir->clust;
  dir_enterable = 1;

  str += len;
  *path = str;

  for (;;)
  {
    while (*str == '/')
      str++;
    *path = str;

    len = subpath_len(str);
    if (len == 0)
      return FAT_ERR_NONE; // Do not enter directory. Dir points to the SFN of path.

    if (!dir_enterable)
      return FAT_ERR_PATH;

    dir_enter(dir, dir_clust);

    err = dir_search(dir, str, len, loc);
    if (err)
      return err;

    str += len;
    *path = str;

    Sfn* sfn = dir_ptr(dir);
    dir_clust = sfn_cluster(sfn);
    dir_enterable = (sfn->attr & FAT_ATTR_DIR) != 0;
  }
}

//------------------------------------------------------------------------------
static int remove_entries(Dir* dir, Loc* loc)
{
  // Save dir location (last entry to delete)
  unsigned int sect = dir->sect;
  unsigned short idx = dir->idx;

  // Rewind dir to loc (first entry to delete)
  dir->clust = sect_to_clust(dir->fat, loc->sect);
  dir->sect = loc->sect;
  dir->idx = loc->idx;

  for (;;)
  {
    int err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;

    Sfn* sfn = dir_ptr(dir);
    sfn->name[0] = SFN_FREE;
    dir->fat->flags |= FAT_BUF_DIRTY;

    if (dir->sect == sect && dir->idx == idx)
      return FAT_ERR_NONE;

    err = dir_next(dir);
    if (err)
      return err;
  }
}

//------------------------------------------------------------------------------
static int dir_add(Dir* dir, const char* name, int len, unsigned char attr, unsigned int clust)
{
  if (len <= 0 || len > 255)
    return FAT_ERR_PARAM;

  int err;
  int eod = 0;
  unsigned short idx = 0;
  unsigned int sect = 0;
  int lfns = (len + 12) / 13;

  dir_enter(dir, dir->sclust);

  // Try to find lfn_cnt + 1 consecutive free entries. Stretch cluster chain
  // if necessary. Store location of first entry in the sequence.
  for (int cnt = 0; cnt < lfns + 1;)
  {
    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;

    Sfn* sfn = dir_ptr(dir);
    if (eod || sfn_is_free(sfn))
    {
      if (cnt++ == 0)
      {
        sect = dir->sect;
        idx = dir->idx;
      }
    }
    else
      cnt = 0;

    if (sfn_is_last(sfn))
      eod = 1;

    err = dir_next_stretch(dir);
    if (err)
      return err;
  }

  if (eod)
  {
    // We are currently at the entry after the SFN we will create.
    // Since it hit EOD the entry is free. Create new EOD.
    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;
    
    Sfn* sfn = dir_ptr(dir);
    sfn->name[0] = 0x00;
    dir->fat->flags |= FAT_BUF_DIRTY;
  }

  // Rewind to the first free entry
  dir->clust = sect_to_clust(dir->fat, sect);
  dir->sect = sect;
  dir->idx = idx;

  unsigned char sfn_name[11];
  put_sfn_name(sfn_name, name, len);

  unsigned char crc = get_crc(sfn_name);
  unsigned char mask = LFN_HEAD_MSK;

  // Create LFN entries
  for (int i = lfns; i > 0; i--, mask = 0)
  {
    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;
    
    Lfn* lfn = dir_ptr(dir);
    dir->fat->flags |= FAT_BUF_DIRTY;

    int pos = 13 * (i - 1);
    put_lfn_name_frag(lfn, name + pos, LIMIT(len - pos, 13));
    lfn->attr = FAT_ATTR_LFN;
    lfn->seq = mask | i;
    lfn->crc = crc;
    lfn->type = 0;
    lfn->clust = 0;
    
    err = dir_next(dir);
    if (err)
      return err;
  }

  unsigned short time, date;
  encode_timestamp(&date, &time);

  err = update_buf(dir->fat, dir->sect);
  if (err)
    return err;

  Sfn* sfn = dir_ptr(dir);
  dir->fat->flags |= FAT_BUF_DIRTY;

  memcpy(sfn->name, sfn_name, sizeof(sfn->name));
  sfn->clust_hi = clust >> 16;
  sfn->clust_lo = clust & 0xffff;
  sfn->attr = attr;
  sfn->reserved = 0;
  sfn->tenth = 0;
  sfn->cre_time = time;
  sfn->mod_time = time;
  sfn->cre_date = date;
  sfn->mod_date = date;
  sfn->acc_date = date;
  sfn->size = 0;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int get_part_lba(unsigned char* buf, int partition, unsigned int* lba)
{
  Mbr* mbr = (Mbr*)buf;

  if (mbr->sig != 0xaa55)
    return 0;

  if (mbr->part[partition].type != 0x0c) // Must be FAT32
    return 0;
  
  *lba = mbr->part[partition].lba;
  return 1;
}

//------------------------------------------------------------------------------
static int check_fat(unsigned char* buf)
{
  Bpb* bpb = (Bpb*)buf;
  
  if (bpb->jump[0] != 0xeb && bpb->jump[0] != 0xe9)
    return 0;

  // Check if we need to be this strict.
  if (bpb->fat_cnt != 2)
    return 0;
  
  if (bpb->root_ent_cnt || bpb->sect_cnt_16 || bpb->sect_per_fat_16)
    return 0;
  
  if (bpb->info_sect != 1)
    return 0;

  if (memcmp(bpb->fs_type, "FAT32   ", 8))
    return 0;
    
  if (bpb->bytes_per_sect != 512)
    return 0;
  
  // Only two FAT tables should exist
  if (!(bpb->ext_flags & EXT_FLAG_MIRROR) && (bpb->ext_flags & EXT_FLAG_ACT) > 1)
    return 0;
  
  // FAT type is determined from the count of clusters
  unsigned int sect_cnt = bpb->sect_cnt_32 - (bpb->res_sect_cnt + bpb->fat_cnt * bpb->sect_per_fat_32);
  return sect_cnt/ bpb->sect_per_clust >= 65525;
}

//------------------------------------------------------------------------------
int probe(DiskOps* ops, int partition, unsigned int* lba)
{
  *lba = 0;
  if (!ops->read(g_buf, *lba))
    return FAT_ERR_IO;

  if (check_fat(g_buf))
    return partition == 0 ? FAT_ERR_NONE : FAT_ERR_NOFAT;

  if (!get_part_lba(g_buf, partition, lba))
    return FAT_ERR_NOFAT;
  
  if (!ops->read(g_buf, *lba))
    return FAT_ERR_IO;
  
  return check_fat(g_buf) ? FAT_ERR_NONE : FAT_ERR_NOFAT;
}

//------------------------------------------------------------------------------
// Probes a partition on the drive for a FAT32 file system. 
//
// Partition 0 returns success when either:
//  - the entire drive is formatted FAT32
//  - the drive contain an MBR with partition 0 formatted FAT32
//
// Partition 1 to 3 return success when:
//  - the drive contain an MBR with partition 1 to 3 formatted FAT32

int fat_probe(DiskOps* ops, int partition)
{
  unsigned int lba;
  return probe(ops, partition, &lba);
}

//------------------------------------------------------------------------------
// Mounts a file system. The name specifies which path is used to access it.
// For example: mounting using 'mnt', and accessing using '/mnt/path/file.txt'.
// Partition 0 referes to either the entire disk (absense of MBR), or to the 
// specified MBR partition.

int fat_mount(DiskOps* ops, int partition, Fat* fat, const char* name)
{
  unsigned int lba;
  int err = probe(ops, partition, &lba);
  if (err)
    return err;

  // Global buffer contains BPB when probe succeeds
  Bpb* bpb = (Bpb*)g_buf;

  int mirror    = (bpb->ext_flags & EXT_FLAG_MIRROR) != 0;
  int use_first = (bpb->ext_flags & EXT_FLAG_SECOND) == 0;

  unsigned int fat_0 = lba + bpb->res_sect_cnt;
  unsigned int fat_1 = lba + bpb->res_sect_cnt + bpb->sect_per_fat_32;
  
  fat->clust_shift = __builtin_ctz(bpb->sect_per_clust);
  fat->clust_msk = bpb->sect_per_clust - 1;
  fat->clust_cnt = bpb->sect_per_fat_32 * 128;
  fat->root_clust = bpb->root_cluster;
  fat->fat_sect[0] = use_first ? fat_0 : fat_1;
  fat->fat_sect[1] = mirror ? (use_first ? fat_1 : fat_0) : 0;
  fat->info_sect = lba + bpb->info_sect;
  fat->data_sect = lba + bpb->res_sect_cnt + bpb->fat_cnt * bpb->sect_per_fat_32;

  // Load FsInfo
  if (!ops->read(g_buf, fat->info_sect))
    return FAT_ERR_IO;

  FsInfo* info = (FsInfo*)g_buf;
  if (info->tail_sig != FSINFO_TAIL_SIG || 
      info->head_sig != FSINFO_HEAD_SIG ||
      info->struct_sig != FSINFO_STRUCT_SIG ||
      info->next_free == 0xffffffff || 
      info->free_cnt == 0xffffffff)
    return FAT_ERR_NOFAT;
  
  fat->last_used = info->next_free;
  fat->free_cnt  = info->free_cnt;

  int name_len = strlen(name);
  if (name_len > sizeof(fat->name))
    return FAT_ERR_PARAM;
  memcpy(fat->name, name, name_len);
  fat->name_len = name_len;

  fat->ops = *ops;
  fat->sect = 0;   // Causes buffering on first call

  fat->next = g_fat_list;
  g_fat_list = fat;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
// Syncronizes unwritten changes and removes the fat from the global list. All
// file must be closed before calling this.

int fat_umount(Fat* fat)
{
  Fat** it = &g_fat_list;
  while (*it && *it != fat)
    it = &(*it)->next;

  if (*it == NULL)
    return FAT_ERR_PARAM;

  *it = fat->next;
  return sync_fs(fat);
}

//------------------------------------------------------------------------------
// Synchronizes unwritten changes. Does not synchronize open files.

int fat_sync(Fat* fat)
{
  return sync_fs(fat);
}

//------------------------------------------------------------------------------
// Get information about a file or directory.

int fat_stat(const char* path, DirInfo* info)
{
  Dir dir;
  Loc loc;
  int err = follow_path(&dir, &path, &loc);
  if (err)
    return err;
  
  int len = subpath_len(path);
  if (len)
    return FAT_ERR_PATH;
  
  dir.clust = sect_to_clust(dir.fat, loc.sect);
  dir.sect = loc.sect;
  dir.idx = loc.idx;

  return fat_dir_read(&dir, info);
}

//------------------------------------------------------------------------------
// Unlinks (deletes) an existing file or empty directory.

int fat_unlink(const char* path)
{
  Dir dir;
  Loc loc;
  int err = follow_path(&dir, &path, &loc);
  if (err)
    return err;

  if (dir_at_root(&dir))
    return FAT_ERR_DENIED;

  Sfn* sfn = dir_ptr(&dir);
  unsigned int clust = sfn_cluster(sfn);

  if (sfn->attr & (FAT_ATTR_RO | FAT_ATTR_SYS | FAT_ATTR_LABEL))
    return FAT_ERR_DENIED;

  if (sfn->attr & FAT_ATTR_DIR)
  {
    // Make sure the directory is empty
    Dir tmp = dir;
    dir_enter(&tmp, sfn_cluster(sfn));

    err = dir_advance(&tmp, 2); // . and ..
    if (err)
      return err;
    
    for (;;)
    {
      err = update_buf(tmp.fat, tmp.sect);
      if (err)
        return err;

      sfn = dir_ptr(&tmp);
      
      if (sfn_is_last(sfn))
        break;

      if (!sfn_is_free(sfn))
        return FAT_ERR_DENIED;

      err = dir_next(&tmp);
      if (err)
        return err;
    }
  }

  // Delete clusters
  err = remove_chain(dir.fat, clust);
  if (err)
    return err;

  err = remove_entries(&dir, &loc);
  if (err)
    return err;

  return sync_fs(dir.fat);
}

//------------------------------------------------------------------------------
// Opens a file. The file structure contain the size and offset that can be read
// by the user at any point. Any combination of the following flags can be used:
//
//  - FAT_WRITE:   open for writing
//  - FAT_READ:    open for reading
//  - FAT_APPEND:  place file cursor at the end of the file
//  - FAT_TRUNC:   truncate the file
//  - FAT_CREATE:  create file if not existing

int fat_file_open(File* file, const char* path, unsigned char flags)
{
  Dir dir;
  dir.fat = 0;

  int err = follow_path(&dir, &path, NULL);
  if (err && err != FAT_ERR_EOF)
    return err;
  
  if (err == FAT_ERR_EOF) // File does not exist
  {
    if (0 == (flags & FAT_CREATE))
      return FAT_ERR_DENIED;
    
    int len = last_subpath_len(path);
    if (len == 0)
      return FAT_ERR_PATH;

    // Create a new file
    unsigned int clust;
    err = create_chain(dir.fat, &clust);
    if (err)
      return err;
    
    err = dir_add(&dir, path, len, FAT_ATTR_ARCHIVE, clust);
    if (err)
      return err;
  }

  Sfn* sfn = dir_ptr(&dir);

  file->fat = dir.fat;
  file->dir_sect = dir.sect;
  file->dir_idx = dir.idx;
  file->sclust = sfn_cluster(sfn);
  file->clust = file->sclust;
  file->sect = 0xffffffff;
  file->offset = 0;
  file->attr = sfn->attr;
  file->size = sfn->size;
  file->flags = flags;

  if (file->size && flags & FAT_TRUNC)
  {
    file->size = 0;
    file->flags |= FAT_MODIFIED;
  }

  return fat_file_seek(file, 0, (flags & FAT_APPEND) ? FAT_SEEK_END : FAT_SEEK_START);
}

//------------------------------------------------------------------------------
// Closes a file. Updates the directory entry if modified. Writes back the write 
// buffer if dirty.

int fat_file_close(File* file)
{
  if (!file->fat)
    return FAT_ERR_PARAM;
  return fat_file_sync(file);
}

//------------------------------------------------------------------------------
int fat_file_read(File* file, void* buf, int len, int* bytes)
{
  *bytes = 0;
  unsigned char* dst = buf;

  if (!file->fat)
    return FAT_ERR_PARAM;

  if (0 == (file->flags & FAT_READ))
    return FAT_ERR_DENIED;
  
  file->flags |= FAT_ACCESSED;

  while (len && file->offset < file->size)
  {
    int idx = file->offset % 512;
    int cnt = LIMIT(len, LIMIT(512 - idx, file->size - file->offset));
    memcpy(dst, file->buf + idx, cnt);

    *bytes += cnt;
    dst += cnt;
    len -= cnt;

    int err = fat_file_seek(file, cnt, FAT_SEEK_CURR);
    if (err)
      return err;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
// Write a number of bytes to the file. It allocates more clusters if the write
// exceeds the allocated space. It return the error code and the number of bytes
// written.

int fat_file_write(File* file, const void* buf, int len, int* bytes)
{
  int err = FAT_ERR_NONE;
  const unsigned char* src = buf;
  *bytes = 0;

  if (!file->fat)
    return FAT_ERR_PARAM;

  if (0 == (file->flags & FAT_WRITE))
    return FAT_ERR_DENIED;
  
  file->flags |= FAT_MODIFIED | FAT_ACCESSED;

  while (len)
  {
    int idx = file->offset % 512;
    int cnt = LIMIT(len, 512 - idx);
    memcpy(file->buf + idx, src, cnt);
    file->flags |= FAT_FILE_DIRTY;

    *bytes += cnt;
    src += cnt;
    len -= cnt;

    int err = fat_file_seek(file, cnt, FAT_SEEK_CURR);
    if (err)
      break;
  }

  if (file->offset > file->size)
    file->size = file->offset;

  return err;
}

//------------------------------------------------------------------------------
// Seek into the file. This is internally used to update the file buffer and extend
// the file when needed. For a user, this is used to ether:
//
//  - Update the offset of subsequent reads and writes
//  - Preallocate space in the file (just seek the number of bytes to allocate)
// 
// Seeking backwards take more time as the cluster chain (often) must be followed 
// from the beginning.

int fat_file_seek(File* file, int offset, int seek)
{
  unsigned int ssect = file->sect;
  long long off64 = 0;

  if (!file->fat)
    return FAT_ERR_PARAM;

  switch (seek)
  {
    case FAT_SEEK_START:
      off64 = 0;
      break;
    case FAT_SEEK_CURR:
      off64 = file->offset;
      break;
    case FAT_SEEK_END:
      off64 = file->size;
      break;
  }

  off64 += offset;
  if (off64 < 0 || off64 > 0xffffffff)
    return FAT_ERR_EOF;

  unsigned int off = (unsigned int)off64;
  unsigned int clust_size = 512 << file->fat->clust_shift;
  unsigned int dst_clust = off / clust_size;
  unsigned int src_clust = file->offset / clust_size;

  if (dst_clust < src_clust)
  {
    // Backtracking not possible. Start scan from the beginning.
    file->clust = file->sclust;
    file->sect = clust_to_sect(file->fat, file->sclust);
    file->offset = 0;
    src_clust = 0;
  }

  // Follow the cluster chain. Expand when EOF.
  for (int i = 0; i < dst_clust - src_clust; i++)
  {
    unsigned int next;
    unsigned char flags;
    int err = get_fat(file->fat, file->clust, &next, &flags);
    if (err)
      return err;

    if (flags & (CLUST_BAD | CLUST_FREE))
      return FAT_ERR_BROKEN;

    if (flags & CLUST_LAST)
    {
      err = stretch_chain(file->fat, file->clust, &next);
      if (err)
        return err;
    }
    
    file->clust = next;
  }

  file->sect = clust_to_sect(file->fat, file->clust) + ((off / 512) & file->fat->clust_msk);
  file->offset = off;

  // Update file buffer when moving to new sector
  if (file->sect != ssect)
  {
    if (file->flags & FAT_FILE_DIRTY)
    {
      if (!file->fat->ops.write(file->buf, ssect))
        return FAT_ERR_IO;
      file->flags &= ~FAT_FILE_DIRTY;
    }

    if (!file->fat->ops.read(file->buf, file->sect))
      return FAT_ERR_IO;
  }
  
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
// Synchronizes a file. Writes back dirty file data. Updates directory timestamp
// when accessed. Update directory size and timestamp when modified.

int fat_file_sync(File* file)
{
  int err;
  if (!file->fat)
    return FAT_ERR_PARAM;
  
  if (!file->fat->ops.write(file->buf, file->sect))
    return FAT_ERR_IO;

  if (file->flags & (FAT_ACCESSED | FAT_MODIFIED))
  {
    err = update_buf(file->fat, file->dir_sect);
    if (err)
      return err;
    
    Sfn* sfn = (Sfn*)(file->fat->buf + file->dir_idx);
    file->fat->flags |= FAT_BUF_DIRTY;

    unsigned short date, time;
    encode_timestamp(&date, &time);
    
    if (file->flags & FAT_ACCESSED)
      sfn->acc_date = date;

    if (file->flags & FAT_MODIFIED)
    {
      sfn->attr |= FAT_ATTR_ARCHIVE;
      sfn->size = file->size;
      sfn->mod_date = date;
      sfn->mod_time = time;
    }
  }

  err = sync_fs(file->fat);
  if (err)
    return err;

  file->flags &= ~(FAT_ACCESSED | FAT_MODIFIED);
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
// Creates and enter a directory. Don't know if there is any point in returning dir.

int fat_dir_create(Dir* dir, const char* path)
{
  int err = follow_path(dir, &path, NULL);
  if (err != FAT_ERR_EOF)
    return err;
  
  int len = last_subpath_len(path);
  if (len == 0)
    return FAT_ERR_PATH;

  // Create a new directory
  unsigned int clust;
  err = create_chain(dir->fat, &clust);
  if (err)
    return err;

  // Empty directory
  err = clust_clear(dir->fat, clust);
  if (err)
    return err;

  unsigned short date, time;
  encode_timestamp(&date, &time);

  err = update_buf(dir->fat, clust_to_sect(dir->fat, clust));
  if (err)
    return err;

  Sfn* sfn = (Sfn*)dir->fat->buf;
  dir->fat->flags |= FAT_BUF_DIRTY;

  memset(sfn[0].name, ' ', sizeof(sfn->name));
  sfn[0].name[0] = '.';
  sfn[0].attr = FAT_ATTR_DIR;
  sfn[0].clust_hi = clust >> 16;
  sfn[0].clust_lo = clust & 0xffff;
  sfn[0].cre_date = date;
  sfn[0].cre_time = time;
  sfn[0].mod_date = date;
  sfn[0].mod_time = time;
  sfn[0].acc_date = date;

  unsigned int parent = dir->sclust;
  if (parent == dir->fat->root_clust)
    parent = 0;
  
  memcpy(sfn + 1, sfn, sizeof(Sfn));
  sfn[1].name[1] = '.';
  sfn[1].clust_hi = parent >> 16;
  sfn[1].clust_lo = parent & 0xffff;

  err = dir_add(dir, path, len, FAT_ATTR_DIR, clust);
  if (err)
    return err;
  
  dir_enter(dir, clust);
  return sync_fs(dir->fat);
}

//------------------------------------------------------------------------------
int fat_dir_open(Dir* dir, const char* path)
{
  int err = follow_path(dir, &path, NULL);
  if (err)
    return err;
  
  if (dir_at_root(dir))
    return FAT_ERR_NONE;

  // Dir points to the directory SFN. Enter the directory.
  err = update_buf(dir->fat, dir->sect);
  if (err)
    return err;

  Sfn* sfn = dir_ptr(dir);
  if (0 == (sfn->attr & FAT_ATTR_DIR))
    return FAT_ERR_PATH;

  dir_enter(dir, sfn_cluster(sfn));
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
// Read directory entry pointed to by dir. Use dir_next to advance directory pointer.

int fat_dir_read(Dir* dir, DirInfo* info)
{
  if (!dir->fat)
    return FAT_ERR_PARAM;

  for (int err = 0;; err = dir_next(dir)) // Hack to allow continue
  {
    if (err)
      return err;

    err = update_buf(dir->fat, dir->sect);
    if (err)
      return err;

    Sfn* sfn = dir_ptr(dir);

    if (sfn_is_last(sfn))
      return FAT_ERR_EOF;

    if (sfn_is_free(sfn))
      continue;

    if (sfn_is_lfn(sfn))
    {
      err = parse_lfn_name(dir);
      if (err)
        return err;

      // Following entry must be SFN
      err = update_buf(dir->fat, dir->sect);
      if (err)
        return err;
      
      sfn = dir_ptr(dir);
      if (sfn_is_free(sfn) || g_crc != get_crc(sfn->name))
        return FAT_ERR_BROKEN;
    }
    else
      parse_sfn_name(sfn->name);

    // Parsed filename (SFN or LFN) are in global buffer
    memcpy(info->name, g_buf, g_len);
    info->name_len = g_len;

    decode_timestamp(sfn->cre_date, sfn->cre_time, &info->created);
    decode_timestamp(sfn->mod_date, sfn->mod_time, &info->modified);

    info->size = sfn->size;
    info->attr = sfn->attr;

    return FAT_ERR_NONE;
  }
}

//------------------------------------------------------------------------------
// Advances the directory pointer. Returns EOF when the EOF marker is hit. The 
// user should not call this after that point. Call rewind to reset the directory
// pointer to the beginning.

int fat_dir_next(Dir* dir)
{
  if (!dir->fat)
    return FAT_ERR_PARAM;

  return dir_next(dir);
}

//------------------------------------------------------------------------------
// Sets dir to point to the first entry in the directory.
int fat_dir_rewind(Dir* dir)
{
  if (!dir->fat)
    return FAT_ERR_PARAM;
  dir_at_clust(dir, dir->sclust);
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
const char* fat_get_error(int err)
{
  static const char* strs[] =
  {
    "FAT_ERR_NONE",
    "FAT_ERR_NOFAT",
    "FAT_ERR_BROKEN",
    "FAT_ERR_IO",
    "FAT_ERR_PARAM",
    "FAT_ERR_PATH",
    "FAT_ERR_EOF",
    "FAT_ERR_DENIED",
    "FAT_ERR_FULL",
  };

  err = -err;
  if (err < 0 || err >= sizeof(strs) / sizeof(*strs))
    return "NULL";
  return strs[err];
}

//------------------------------------------------------------------------------
// Override this function if it is needed. It is declared weak.

__attribute__((weak)) void fat_get_timestamp(Timestamp* ts)
{
  ts->day   = 1;
  ts->month = 1;
  ts->year  = 1980;
  ts->hour  = 0;
  ts->min   = 0;
  ts->sec   = 0;
}
