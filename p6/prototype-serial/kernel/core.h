/**
 * Header for the protoype main file
 */

#ifndef LKP_KV_H
#define LKP_KV_H

#include <linux/mtd/mtd.h>
#include <linux/semaphore.h>
#include <linux/list.h>

typedef struct bucket_ bucket;

/* state for a flash block: used or free */
typedef enum {
	BLK_FREE,
	BLK_USED,
	BLK_META,
	BLK_INVALID,
} blk_state;

/* state for a flash page: used or free */
typedef enum {
	PG_FREE,
	PG_VALID
} page_state;

/* data structure containing the state, wear level, and the number of invalid pages of a flash block */
typedef struct {
	struct list_head *list;
	blk_state state;
	int worn;        /* for wear leveling */
	int nb_invalid;  /* for GC */
	int current_page_offset;
} blk_info;

/* global attributes for our system */
typedef struct {
	struct mtd_info *mtd;	/* pointer to the used flash partition mtd_info object */
	int mtd_index;		/* the partition index */
	int nb_blocks;		/* amount of managed flash blocks */
	int block_size;		/* flash bock size in bytes */
	int page_size;		/* flash page size in bytes */
	int pages_per_block;	/* number of flash pages per block */
	blk_info *blocks; /*metadata: flash blocks state */
	bucket *hashtable;
	int *meta_blkordr;
	int format_done;	/* used during format operation */
	int read_only;		/* are we in read-only mode? */
	struct semaphore format_lock;	/* used during the format operation */
	int hashtable_size;	/* Total size of hash table in bytes */
	int block_info_size;	/* Total size of Flash in bytes */
	int metadata_size;	/* Total size of hashtable_size + block_info_size */
	int nb_meta_pages;  /* number of pages of metadata */
	int nb_meta_blocks; /* number of blocks of metadata */
} lkp_kv_cfg;

/* export some prototypes for function used in the virtual device file */
int set_keyval(const char *key, const char *val);
int get_keyval(const char *key, char *val);
int del_key(const char *key);
int format(void);
int format_single( int idx);
void print_hash(void);
int my_gbtest(void);
void gc(void);
void write_hdr(int pg_idx, int data, int meta_blk_num);

/* prototypes */
int init_config(int mtd_index);

extern lkp_kv_cfg config;
#endif /* LKP_KV_H */
