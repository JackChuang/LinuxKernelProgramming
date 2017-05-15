/**
 * This file contains the prototype core functionalities.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <asm/atomic.h>
#include <linux/time.h>

#include "core.h"
#include "device.h"
#include "hash.h"

/* Thresholds */
#define INVALID_THRESHOLD 48

/* other features */
#define RESERVED_PG_CNT 1

/* Metadata Header Magic Number */
#define META_HDR_BASE 1990
#define PRINT_PREF KERN_INFO "[LKP_KV]: "
#define NAND_DATA 123
#define NAND_META_DATA 456

/* prototypes */
int init_config(int mtd_index);
void destroy_config(void);
void print_config(void);
int write_page(int page_index, const char *buf);
int write_meta_page(int page_index, const char *buf);
int read_page(int page_index, char *buf);
int read_meta_page(int page_index, char *buf);
void format_callback(struct erase_info *e);
int get_next_page(void);
int get_healthy_block(void);
int get_meta_block(void);
int init_scan(void);
int flush_metadata(void);
void gb_check(void);
void gc(void);
void print_hash(void);
int is_read_only(void);
int move_pages_to_other_block(int victim_block);
void invalid_pg(int hash_idx);

/* Global Config Variables */
lkp_kv_cfg config;

/* Time variables */
struct timespec ts_flush, ts_write;
unsigned long time_flush, time_write;

/* The module tases one parameter which is the index of the target flash
 * partition */
int MTD_INDEX = -1;
module_param(MTD_INDEX, int, 0);
MODULE_PARM_DESC(MTD_INDEX, "Index of target mtd partition");

/**
 * Module initialization function
 */
static int __init lkp_kv_init(void)
{
	printk(PRINT_PREF "Loading... \n");
	
    /*Initialize array for holding metadata block index's */
	if (init_config(MTD_INDEX) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -2;
	}

	return 0;
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	printk(PRINT_PREF "Exiting ... \n");
	flush_metadata();
	printk(PRINT_PREF "Flush done ... \n");
	device_exit();
	printk(PRINT_PREF "Device exit ... \n");
	destroy_config();
	printk(PRINT_PREF "Module exit Complete!\n\n");
}

/**
 * Global state initialization
 * Both data and metadata
 *
 * Return 
 *  0: OK
 * -1: Data Config Error
 * -2: Metadata Config Error
 * -3: Kmalloc Error
 */
int init_config(int mtd_index)
{
	uint64_t tmp_blk_num;

	if (mtd_index == -1) {
		printk(PRINT_PREF
		       "Error, flash partition index missing, should be"
		       " indicated for example like this: MTD_INDEX=5\n");
		return -1;
	}

	/* Disk Config */
	config.format_done = 0;
	config.read_only = 0;
	config.mtd_index = mtd_index;

	/* The flash partition is manipulated by calling the driver, through the
	 * mtd_info object. There is one of these object per flash partition */
	config.mtd = get_mtd_device(NULL, mtd_index);
	
	if (config.mtd == NULL)
		return -1;

	config.block_size = config.mtd->erasesize;
	config.page_size = config.mtd->writesize;
	config.pages_per_block = config.block_size / config.page_size;

	tmp_blk_num = config.mtd->size;
	do_div(tmp_blk_num, (uint64_t) config.mtd->erasesize);
	 /* Defined by flash simulator */
	config.nb_blocks = (int)tmp_blk_num;

	/* Semaphore initialized to 1 (available) */
	sema_init(&config.format_lock, 1);
	time_flush = 0;
	time_write = 0;
	
	/* Allocates a chunk of memory according to size of disk config */
	/* Allocation for block infomation */
	config.blocks = kmalloc(sizeof(blk_info) * config.nb_blocks, GFP_ATOMIC);
	if (!config.blocks)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -3;
	}
	/* Allocation for hashtable */
	config.hashtable = kmalloc(sizeof(bucket) * config.nb_blocks * config.pages_per_block, GFP_ATOMIC);	
	if (!config.hashtable)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -3;
	}
	/* Flash scan for metadata creation: which flash blocks and pages are 
	 * free/occupied */
	if (init_scan() != 0) {
		printk(PRINT_PREF "init_scan() error\n");
		return -1;
	}

	printk(KERN_INFO "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
	print_config();
	printk(KERN_INFO "************************************************************\n");

	return 0;
}

/**
 * Launch time metadata creation: flash is scanned to determine which flash 
 * blocs and pages are free/occupied. 
 *
 * Return
 *  0: OK
 * -1: Error
 * -2: Kmalloc Error
 * -3: Read Page Error
 */
int init_scan(void)
{
	int i,j, cnt = 0, offset = 0;
	char *buf, *tmp;
	int blkordr_index = 0;
	int hdr = 0;
	int ret = 0;

	/* Set more metadata sizes */
	config.hashtable_size = sizeof(bucket) * config.nb_blocks * config.pages_per_block;
	config.block_info_size = sizeof(blk_info) * config.nb_blocks;
	config.metadata_size = config.hashtable_size + config.block_info_size;
	config.nb_meta_pages = (config.metadata_size / config.page_size) + 1;
	config.nb_meta_blocks = (config.nb_meta_pages / (config.pages_per_block - 1)) + 1;

	buf = kmalloc(config.page_size * config.nb_meta_pages, GFP_ATOMIC);
	if (!buf)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -2;
	}
	tmp = kmalloc(config.page_size, GFP_ATOMIC);
	if (!tmp)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -2;
	}
	config.meta_blkordr = kmalloc(sizeof(int) * config.nb_meta_blocks, GFP_ATOMIC);

	for (i = 0; i < config.nb_meta_blocks; i++)
		config.meta_blkordr[i] = -1;

	/* iterate blks */
    for (i = 0; i < config.nb_blocks; i++) 
	{
        if ( read_page(i * config.pages_per_block, tmp) != 0 ) {
            printk(PRINT_PREF "%s(): read_page failed\n", __func__);
            kfree(buf);
			kfree(tmp);
      		return -3;
        }

		memcpy(&hdr, tmp, sizeof(int));

		if ( hdr == META_HDR_BASE )
		{
			memcpy(&blkordr_index, tmp + sizeof(int), sizeof(int));
			config.meta_blkordr[blkordr_index] = i;
			cnt++;
		}
	}

	for (i = 0; i < cnt; i++)
	{
		for (j = 1; j < config.pages_per_block; j++)
		{	
			if (read_page( config.meta_blkordr[i]*config.pages_per_block + j, buf + (config.page_size * offset) ) != 0)
			{	
				printk(PRINT_PREF "read_page fails\n");
				return -4;
			}
			offset++;
		}
	}
	
	memcpy(config.blocks, buf, config.block_info_size);
	memcpy(config.hashtable, buf + config.block_info_size, config.hashtable_size);
	
	/* For all config blocks */
	for (i = 0; i < config.nb_blocks; i++)
	{
		config.blocks[i].list = (struct list_head *)kmalloc(sizeof(struct list_head), GFP_ATOMIC);
		INIT_LIST_HEAD(config.blocks[i].list);
	}
	/* For all config.hashtable entries */
	if (cnt == 0)
	{
		ret = format();
		if (ret)
		{
			printk(PRINT_PREF "format fails");
			return ret;
		}
	}
	else
	{
		for (i = 0; i < config.nb_blocks * config.pages_per_block; i++)
		{
			if (config.hashtable[i].p_state == PG_VALID)
				list_add(&config.hashtable[i].p_list, config.blocks[config.hashtable[i].index / config.pages_per_block].list); 
		}
	}

	if (is_read_only())
	{
		/* No healthy blocks left! */
		printk(PRINT_PREF "read only mode\n");

		/* Switch Disk to READ-ONLY */
		config.read_only = 1;
	}

	kfree(buf);
	kfree(tmp);
	return 0;
}

/*
 * move valid pages in the victim block to other blocks 
 */
int move_pages_to_other_block(int victim_block)
{
	int i, ret = 0;
	int key_len, val_len;
	int hash_index, page_index, target_index, target_block;
	char *key, *val, *buffer;

	if (config.blocks[victim_block].state == BLK_FREE)
		return 0;

	key = kmalloc(sizeof(char) * 128, GFP_ATOMIC);
	if (!key)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -2;
	}

	val = kmalloc(sizeof(char) * config.page_size, GFP_ATOMIC);
	if (!val)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -2;
	}

	buffer = kmalloc(sizeof(char) * config.page_size, GFP_ATOMIC);
	if (!buffer)
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -2;
	}

	if (config.blocks[victim_block].state == BLK_USED)
	{
		config.blocks[victim_block].state = BLK_INVALID;
		for (i = 1; i < config.pages_per_block; i++)
		{
			page_index = victim_block * config.pages_per_block + i;

			if (read_page(page_index, buffer) != 0)
			{
				printk(PRINT_PREF "read_page error\n");
				kfree(key);
				kfree(val);
				kfree(buffer);
				return -4;
			}
			
			memcpy(&key_len, buffer, sizeof(int));
			memcpy(&val_len, buffer + sizeof(int), sizeof(int));				
			
			if (key_len > 0)
			{
				memcpy(key, buffer + 2 * sizeof(int), key_len);
			} 
			else
				continue;

			if (val_len > 0)
			{
				memcpy(val, buffer + 2 * sizeof(int) + key_len, val_len);
			} 
			else
				continue;

			
			hash_index = hash_search(&config, config.hashtable, key);
			
			if (hash_index >= 0)
			{ 
				invalid_pg(hash_index);
				target_block = get_next_page();
				target_index = target_block * config.pages_per_block + config.blocks[target_block].current_page_offset;
				write_page(target_index, buffer);
				hash_index = hash_add(&config, config.hashtable, key, target_index);
				list_add(&config.hashtable[hash_index].p_list, config.blocks[target_block].list);
			}
		}
	}
	
	ret += format_single(victim_block);
	
	kfree(key);
	kfree(val);
	kfree(buffer);
	
	return ret;
}

int flush_metadata(void)
{
	char *buffer;
	int i,j;
	int buffer_size;
	int offset = 0;
	int target_block = -1;

	/*
	 *	read-only mode handling is needed
	 */

	buffer_size = config.page_size * config.nb_meta_pages;
	buffer = (char *)kmalloc(buffer_size, GFP_ATOMIC);
	if(!buffer) 
	{
		printk(PRINT_PREF "kmalloc failed\n");
		return -1;
	}
	
	for (i = 0; i < config.nb_meta_blocks; i++)
		format_single(config.meta_blkordr[i]);

	/* Find If we have enough free blocks to update */
	for (i = 0; i < config.nb_meta_blocks; i++)
	{
		target_block = get_meta_block();
		if (target_block == -1)
			printk(PRINT_PREF "will not happen\n");

		move_pages_to_other_block(target_block);

		config.meta_blkordr[i] = target_block;
		config.blocks[target_block].state = BLK_META;
	}

	/*
	 * copy metadata to buffer
	 */
	memcpy(buffer, config.blocks, config.block_info_size);	
	memcpy(buffer + config.block_info_size, config.hashtable, config.hashtable_size);

	offset = 0;
	for (i = 0; i < config.nb_meta_blocks; i++)
	{
		/* Actual flush */
		write_hdr(config.meta_blkordr[i] * config.pages_per_block, NAND_META_DATA, i);
		for (j = 1; j < config.pages_per_block; j++)
		{	
			if (write_page( (config.meta_blkordr[i] * config.pages_per_block) + j, buffer + (config.page_size * offset)) != 0)
			{
				printk(PRINT_PREF "write_page error\n");
				kfree(buffer);
				return -2;
			}
			offset++;
		}
	}
	
	kfree(buffer);
	
	/*
	 * get time when flushing
	 */
	getnstimeofday(&ts_flush);
	time_flush = ts_flush.tv_sec * 1000000 + ts_flush.tv_nsec;
	//printk(PRINT_PREF "Flush metadata done\n");
	
	return 0;
}

/**
 * Freeing structures on exit of module
 */
void destroy_config(void)
{
	/* Free all config.blocks */
	kfree(config.blocks);
	kfree(config.hashtable);
	kfree(config.meta_blkordr);
	put_mtd_device(config.mtd);
}

/* invalid_pg( int config.hashtable_index)
 * Takes a config.hashtable index and updates page data at index to be INVALIDATED
 *
 * Return
 * VOID
 */
void invalid_pg(int hash_idx)
{
    int pg_idx;
    
    config.hashtable[hash_idx].p_state = PG_FREE;
    pg_idx = config.hashtable[hash_idx].index;
    config.blocks[pg_idx/config.pages_per_block].nb_invalid++;
	/* delete the page metadata from the valid-page-list */
    list_del(&config.hashtable[hash_idx].p_list);
}

/* gb_check( void)
 * If too many invalid pages, don't wait until timmer interrupt handler
 *
 * Return
 * VOID
 */
void gb_check(void)
{
    int i;
    for (i = 0; i < config.nb_blocks; i++) {
        if(config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
            gc();
            break;
        }
	}
}

/**
 * Adding a key-value couple. Returns -1 when ok and a negative value on error:
 * -1 when the size to write is too big
 * -2 when the key already exists
 * -3 when we are in read-only mode
 * -4 when the MTD driver returns an error
 * -5 NULL pointer exception
 */
int set_keyval(const char *key, const char *val)
{
	char *buffer;
	int target_block, key_len, val_len, i, ret, ret2, index, hash_idx;

	key_len = strlen(key);
	val_len = strlen(val);

	if (!key)
	{
		printk("NULL pointer execption\n");
		return -5;
	}

	if ((key_len + val_len + 2 * sizeof(int)) > config.page_size) {
		/* size to write is too big */
		printk(KERN_INFO ">> ERROR: DATA size too big!\n");
		return -1;
	}

	if (config.read_only) {
		printk(KERN_INFO ">> ERROR: Disk in READ-ONLY MODE!\n");
		return -3;
	}
	
	/* the buffer that we are going to write on flash */
	buffer = (char *)kmalloc(config.page_size * sizeof(char), GFP_ATOMIC);
	if(!buffer) {
		printk(PRINT_PREF "kmalloc failed\n");
		BUG();
	}

	/* if the key already exists: Invalidate curr page, & write new one!! */
	hash_idx = hash_search(&config, config.hashtable, key);
	if (hash_idx >= 0) {
		invalid_pg(hash_idx);
	}

	/* prepare the buffer we are going to write on flash */
	for (i = 0; i < config.page_size * sizeof(char); i++)
		buffer[i] = 0x00;

	/* key size ... */
	memcpy(buffer, &key_len, sizeof(int));
	/* ... value size ... */
	memcpy(buffer + sizeof(int), &val_len, sizeof(int));
	/* ... the key itself ... */
	memcpy(buffer + 2 * sizeof(int), key, key_len);
	/* ... then the value itself. */
	memcpy(buffer + 2 * sizeof(int) + key_len, val, val_len);

	target_block = get_next_page();

	//Get Index of page we are writing to
	index = target_block * config.pages_per_block + config.blocks[target_block].current_page_offset;

	/* actual write on flash */
	ret = write_page(index, buffer);
    
	/* Add Key value to RAM config.hashtable */
	ret2 = hash_add(&config, config.hashtable, key, index);
	list_add(&config.hashtable[ret2].p_list, config.blocks[target_block].list); 
	kfree(buffer);
	
	gb_check();
	
	getnstimeofday(&ts_write);
	time_write = ts_write.tv_sec * 1000000 + ts_write.tv_nsec;
	if (time_write - time_flush >= 1000000)
	{
 		flush_metadata();
	}
	
	if (ret == -1)		/* read-only */
		return -3;
	
	if (ret == -2)	/* write error */
		return -4;

	if (ret2 < 0)
		return -5;  /* hash_add error */

	return 0;
}

/**
 * Getting a value from a key.
 * Returns the index of the page containing the key/value couple on success,
 * and a negative number on error:
 * -1 when the key is not found
 * -2 on MTD read error
 */
int get_keyval(const char *key, char *val)
{
	char *buffer;
	int key_len, val_len;
	char *cur_key, *cur_val;
	int page_index = -1;
	int hash_index;

	hash_index = hash_search(&config, config.hashtable, key);
	buffer = (char *)kmalloc(config.page_size * sizeof(char), GFP_ATOMIC);
	if(!buffer) {
		printk(PRINT_PREF "kmalloc failed\n");
		BUG();
	}

	if (hash_index >= 0)
	{		
		page_index = config.hashtable[hash_index].index; 
		if(config.blocks[page_index / config.pages_per_block].state == BLK_USED)
		{
			if (config.hashtable[hash_index].p_state == PG_FREE)
			{
				return -1;
			}

			if (read_page(page_index, buffer) != 0) 
			{
				printk(PRINT_PREF "read_page on %d fail\n", page_index);
				kfree(buffer);
				return -2;
			}

			memcpy(&key_len, buffer, sizeof(int));
			memcpy(&val_len, buffer + sizeof(int), sizeof(int));

			cur_key = buffer + 2 * sizeof(int);
			cur_val = buffer + 2 * sizeof(int) + key_len;
			if (!strncmp(cur_key, key, strlen(key)))
			{
				memcpy(val, cur_val, val_len);
				val[val_len] = '\0';
				kfree(buffer);
				return page_index;
			}
		}
    }

	kfree(buffer);
	return -1;
}

/* del_key( const char *key)
 * Searches config.hashtable for given key, deletes and returns index of key deleted
 *
 * Return
 * -1: Key not found in config.hashtable OR deleting empty Key
 * 0 < x < MAX_HASH_INDEX: returns config.hashtable index of key that was deleted
 */
int del_key(const char *key)
{
	int hash_index, page_index = -1;

	hash_index = hash_search(&config, config.hashtable, key);
	if (hash_index >= 0) {
		page_index = config.hashtable[hash_index].index;
		if(config.blocks[page_index/config.pages_per_block].state == BLK_USED) {
			if (config.hashtable[hash_index].p_state == PG_FREE) {
				/* If we reach here, that means we ran invalid_pg on it
				 * but still hasnt been garbage collected
				 * GC is nondeterministic so this is safety 
				 */

				printk(PRINT_PREF "tryed to delete not garbage collected key\n");
				return -1;
			}
			invalid_pg(hash_index);
			return page_index;
		}
	}
	/* key not found */
	printk(PRINT_PREF "%s(): key not found\n", __func__);
	return -2;
}

/* is_read_only( void)
 * Iterates through DISK and checks if all blocks and pages therein are Valid
 * and NOT_FREE
 *
 * Return
 * 1: IS READ-ONLY, All blocks used
 * 0: IS NOT READ-ONLY, A block is free
 */
int is_read_only(void)
{
	int i;	

	for (i = 0; i < config.nb_blocks; i++)
	{
		if (config.blocks[i].current_page_offset < config.pages_per_block)
			return 0;
	}
#if 0	
		if ( ret ==  config.nb_blocks - config.nb_meta_blocks)		
		{
			/*  Blocks for metadata should be reserved
			 *  No block available. Read-only mode
		     */

			return 1;
		}
	}
#endif
	/* We have available blocks! */
	return 1;
}

/*
 * write_hdr(pg_idx, HDR)
 * write HDR to pg_idx
 * change ofs and return a new ofs for you
 *
 * data:
 *      1. NAND_DATA
 *      2. NAND_META_DATA
 * Header will be flagged as 000000000 (kzalloc) for data block
 * Header will be flagged as META_HDR_BASE[X] for Metadata block X
 * Where [X] is the block of the set (Metadata is organized as more than 1 block)
 *
 * meta_blk_num: only if data == NAND_META_DATA
 *      meta data blk offset
 */
void write_hdr(int pg_idx, int data, int meta_blk_num)
{
	int ret;
	int meta_hdr_base;
	char *buf;
    
	/* Zeros out buffer when allocating it */
	buf = kzalloc(config.page_size*sizeof(char), GFP_ATOMIC);
	if(!buf)
		BUG();
    
    if(data == NAND_DATA) {
        ret = write_page(pg_idx, buf);
		config.blocks[pg_idx/config.pages_per_block].state = BLK_USED;
    }
    else if(data == NAND_META_DATA) {
		meta_hdr_base = META_HDR_BASE;
        memcpy(buf, &meta_hdr_base, sizeof(int));
		memcpy(buf + sizeof(int), &meta_blk_num, sizeof(int));
        ret = write_page(pg_idx, buf);
    }
    else {
        printk(PRINT_PREF "WRONG!\n");
    }

	kfree(buf);
}

/**
 * After an insertion, determine which is the flash page that will receive the 
 * next insertion.
 *
 * Return 
 * the correspondign flash page index
 * -1: if the flash is full
 *
 *  Jack TODO: concurrency problem
 */
int get_next_page(void)
{
	int target_block, pg_idx;

	/* Returns block index */
	target_block = get_healthy_block();

	if (target_block == -1)
		return -1;
	
	/* If current_page_offset = 0, need to reserve to first page for Metadata flag */
	if (config.blocks[target_block].current_page_offset < RESERVED_PG_CNT) {
		config.blocks[target_block].state = BLK_USED;
		pg_idx = (target_block*config.pages_per_block) + 
				config.blocks[target_block].current_page_offset;
		/* Specify flag that this block is data block */
		write_hdr(pg_idx, NAND_DATA, 0);
	}
	
	return target_block;
}

/* get_healthy_block(void)
 * Iterates through disk blocks looking for healthy, free block to use.
 * Return
 * 0<x< config.nb_blocks: Index of healthy, free block to use
 * -1: No healthy, free blocks available, Disk is now in READ-ONLY Mode
 */
int get_healthy_block(void)
{
	int i;
	int ret = -1;
	int minimum = 0x7FFFFFFF;
	
    /* For all blocks on disk */
	for (i = 0; i < config.nb_blocks; i++) 
	{
		//if (config.blocks[i].state != BLK_META)
			if (config.blocks[i].state != BLK_INVALID)
				if (config.blocks[i].current_page_offset < config.pages_per_block) 
				{
					/* If block worn threshhold is within limits */
					if (config.blocks[i].worn < minimum) 
					{
						minimum = config.blocks[i].worn;
						ret = i;
					}
				}
	}

    return ret;
}

/*
 * it finds proper blocks for metadata
 */
int get_meta_block(void)
{
	int i;
	int ret = -1;
	int minimum = 0x7FFFFFFF;
		
	for (i = 0; i < config.nb_blocks; i++) 
	{
		if (config.blocks[i].state != BLK_META)
        	if (config.blocks[i].current_page_offset == 0) 
			{
				if (config.blocks[i].worn < minimum) 
				{
					minimum = config.blocks[i].worn;
					ret = i;
				}
			}
	}

	if (ret != -1)
		return ret;

	minimum = 0x7FFFFFFF;
	for (i = 0; i < config.nb_blocks; i++)
	{
		if (config.blocks[i].state != BLK_META)
			if (config.blocks[i].current_page_offset < config.pages_per_block)
			{
				if (config.blocks[i].worn < minimum)
				{
					minimum = config.blocks[i].worn;
					ret = i;
				}
			}
	}

    return ret;
}
/**
 * Callback for the erase operation done during the format process
 */
void format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Format error...");
		down(&config.format_lock);
		config.format_done = -1;
		up(&config.format_lock);
		return;
	}

	down(&config.format_lock);
	config.format_done = 1;
	up(&config.format_lock);
}

/* format_single( int index)
 * Function erases a single block within disk at index and resets metadata 
 * info about given block
 *
 * Return
 * 0: Success
 * -1: Failed to erase 1 block, Error with Driver _erase
 */
int format_single(int idx)
{
	struct erase_info ei;
	bucket *current_page, *next;

    if(idx < 0)
        return 0;

	/* Block Location & length */
	ei.mtd = config.mtd;
	ei.len = ((uint64_t) config.block_size);
	ei.addr = idx * ((uint64_t) config.block_size);

	ei.callback = format_callback;
	config.format_done = 0;

	/* Call the MTD driver  */
	if (config.mtd->_erase(config.mtd, &ei) != 0)
		return -1;
	
	/* Wait while _erase happens */
	while (1) {
		//Get lock for formating disk
		if (!down_trylock(&config.format_lock)) {
			if (config.format_done || (config.format_done == -1)) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
        	}
	}
	
	if (config.format_done == -1)
		return -1;

	/* Reset target block metadata state info */
	config.blocks[idx].state = BLK_FREE;
	config.blocks[idx].nb_invalid = 0;
	config.blocks[idx].current_page_offset = 0;
	config.blocks[idx].worn++;
	
    /* Clear all valid pages associated from target block */
	list_for_each_entry_safe(current_page, next, config.blocks[idx].list, p_list) {
		if(current_page)
			list_del(&current_page->p_list);
	}

	/* global meta data in nand */
	/* Reset that there is room on Disk for writing */
	/* nandflash-wide */
	config.read_only = 0;

	return 0;
}

/**
 * Format operation: we erase the entire flash partition
 */
int format(void)
{
	int i;
	struct erase_info ei;
	bucket *current_page, *next;
   
	/* erasing one or several flash blocks is made through the use of an 
	 * erase_info structure passed to the MTD NAND driver */
	ei.mtd = config.mtd;
	ei.len = ((uint64_t) config.block_size) * ((uint64_t) config.nb_blocks);
	ei.addr = 0x0;
	
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done */
	ei.callback = format_callback;

	config.format_done = 0;

	/* Call the MTD driver  */
	if (config.mtd->_erase(config.mtd, &ei) != 0) {
 		return -1;
    }

	/* on attend la fin effective de l'operation avec un spinlock. 
	 * C'est la fonction callback qui mettra format_done a 1 */
	while (1)
		if (!down_trylock(&config.format_lock)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
		}


	/* was there a driver issue related to the erase oepration? */
	if (config.format_done == -1) {
		return -2;
    }

	config.read_only = 0;

	/* format metadata in the memory */
	for (i = 0; i < config.nb_blocks; i++) {
		list_for_each_entry_safe(current_page, next, config.blocks[i].list, p_list)
		{
			if(current_page)
			{
				list_del(&current_page->p_list);
			}
		}

		config.blocks[i].state = BLK_FREE;
		config.blocks[i].worn = 0;
		config.blocks[i].nb_invalid = 0;
		config.blocks[i].current_page_offset = 0;
	}

	for (i = 0; i < config.nb_blocks * config.pages_per_block; i++)
	{
		config.hashtable[i].p_state = PG_FREE;
		config.hashtable[i].dirty = 0;
	}

	printk(PRINT_PREF "Format done\n");

	return 0;
}

/**
 * Write the flash page with index page_index, data to write is in buf. 
 * Returns:
 * 0 on success
 * -1 if we are in read-only mode
 * -2 when a write error occurs
 */
int write_page(int page_index, const char *buf)
{
	uint64_t addr;
	size_t retlen;
    
	/* if the flash partition is full, dont write */
	if (config.read_only) {
		return -1;
	}

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);

	/* call the NAND driver MTD to perform the write operation */
	if (config.mtd->_write(config.mtd, addr, config.page_size, &retlen, buf) != 0){
		return -2;
	}

	config.blocks[page_index/config.pages_per_block].current_page_offset++;
   

	/* if the flash partition is full, switch to read-only mode */
	if (is_read_only())
	{
		printk(PRINT_PREF "no free block left... swtiching to read-only mode\n");
		config.read_only = 1;
		return -1;
	}

	return 0;
}

/**
 * Read the flash page with index page_index, data read are placed in buf
 * Retourne 0 when ok, something else on error
 */
int read_page(int page_index, char *buf)
{
	int ret;
	uint64_t addr;
	size_t retlen;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);
	
	/* call the NAND driver MTD to perform the read operation */
	ret = config.mtd->_read(config.mtd, addr, config.page_size, &retlen, buf);
    
	return ret;
}

///////////////////////////////////////////////////////////////////////////////
/*****************************************************************************/
/* Print some statistics on the kernel log                                   */
/*****************************************************************************/
void print_config(void)
{
	printk(PRINT_PREF "Data partition's config : \n");
	printk(PRINT_PREF "=========\n");

	printk(PRINT_PREF "mtd_index: %d\n", config.mtd_index);
	printk(PRINT_PREF "nb_blocks: %d\n", config.nb_blocks);
	printk(PRINT_PREF "block_size: %d\n", config.block_size);
	printk(PRINT_PREF "page_size: %d\n", config.page_size);
	printk(PRINT_PREF "pages_per_block: %d\n", config.pages_per_block);
	printk(PRINT_PREF "hashtable size: %d\n", config.hashtable_size);
	printk(PRINT_PREF "block info size: %d\n", config.block_info_size);
	printk(PRINT_PREF "metadata size: %d\n", config.metadata_size);
	printk(PRINT_PREF "nb_blocks of metadata : %d\n", config.nb_meta_blocks);
	printk(PRINT_PREF "nb_pages of metadata size: %d\n", config.nb_meta_pages);
}

/* gc( void)
 * Return
 * VOID
 */
void gc(void)
{
	int pg_index, ret, ret2;
	int hash_index;
	int valid_cnt = 0;
	int i, j, target_blk1 = -1, victim_blk = -1;
	int key_len[config.pages_per_block], val_len[config.pages_per_block];
	char *buffer;
	char *cur_key[config.pages_per_block], *cur_val[config.pages_per_block];
	bucket *current_page, *next;

	/* find target: Will be read from */
	for (i = 0; i < config.nb_blocks; i++) {
		if(config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
			target_blk1 = i;
			break;
		}
	}

	/* No target found */
	if(target_blk1 == -1) {
		return;
	}

	//printk(PRINT_PREF "GCing......\n");
	
	/* Look for victim blk: Will be written to (the least worn) */
	for (i = 0; i < config.nb_blocks; i++) {
		if (BLK_FREE) {
			if (victim_blk == -1)
				victim_blk = i;
			else if (config.blocks[i].worn < 
                    config.blocks[victim_blk].worn)
                victim_blk = i;
		}
	}

	/* No Free Block, we can still write back to ourself */
	if(victim_blk == -1) {
		victim_blk = target_blk1;
	}

    /* 1. read pages */
	buffer = kmalloc(config.page_size * sizeof(char) * config.pages_per_block, GFP_ATOMIC);
    if(!buffer) {
        printk(PRINT_PREF "kmalloc failed\n");
        BUG();
    }
    for (j = 0; j < config.page_size * sizeof(char) * config.pages_per_block; j++)
        *(buffer+j) = 0x00;
    
    /* walk valid_list, store all target_blk data */
    list_for_each_entry_safe(current_page, next, config.blocks[target_blk1].list, p_list)
    {
        char* true_key;
        if (!current_page) {
            printk(KERN_WARNING "WARN: valid_list info is wrong\n");
            break;
        }
        if ( current_page->index/config.pages_per_block != target_blk1 ) {
            printk(KERN_WARNING "WARN: valid_list info is wrong\n");
            break;
        }

        if (read_page(current_page->index, buffer+(valid_cnt*config.pages_per_block)) != 0) {
            printk(PRINT_PREF "%s(): read_page failed\n", __func__);
            kfree(buffer);
            BUG();
        }
        memcpy(&key_len[valid_cnt], buffer+(valid_cnt*config.pages_per_block), sizeof(int));
        memcpy(&val_len[valid_cnt], buffer+(valid_cnt*config.pages_per_block) + sizeof(int), sizeof(int));
        cur_key[valid_cnt] = buffer+(valid_cnt*config.pages_per_block) + 2 * sizeof(int);
        cur_val[valid_cnt] = buffer+(valid_cnt*config.pages_per_block) + 2 * sizeof(int) + key_len[valid_cnt];

        /* invalide in config.hashtable */
        true_key = kmalloc(key_len[valid_cnt]+1, GFP_ATOMIC);
        if(!true_key) {
            printk(PRINT_PREF "vallocation failed\n");
            BUG();
        }
        strncpy(true_key, cur_key[valid_cnt], key_len[valid_cnt]);
        *(true_key+key_len[valid_cnt]) = '\0';

        hash_index = hash_search(&config, config.hashtable, true_key);

        invalid_pg(hash_index); 

        kfree(true_key);

        /* record */
        valid_cnt++;
    }

    /* 2. erase victim_blk */
    format_single(victim_blk);
    
    pg_index = (victim_blk * config.pages_per_block)
                        + config.blocks[victim_blk].current_page_offset;
    write_hdr(pg_index, NAND_DATA, 0);

    /* 3. write */
    for (i=0; i<valid_cnt; i++) {
        char* shown_key;
        
        memcpy(buffer+(i*config.pages_per_block), &key_len[i], sizeof(int));
        memcpy(buffer+(i*config.pages_per_block) + sizeof(int), &val_len[i], sizeof(int));
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int), cur_key[i], key_len[i]);
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int) + key_len[i], cur_val[i], val_len[i]);
	    pg_index = (victim_blk * config.pages_per_block)
                            + config.blocks[victim_blk].current_page_offset;
        
        shown_key = kmalloc(key_len[i]+1, GFP_ATOMIC);
        if(!shown_key) {
            printk(PRINT_PREF "vallocation failed\n");
            BUG();
        }
        strncpy(shown_key, cur_key[i], key_len[i]);
        *(shown_key+key_len[i]) = '\0';
       
		/* write to disk and update metadata (blk info) on RAM */
	    ret = write_page(pg_index, buffer+(i*config.pages_per_block));  /* TODO update .current_page_offset */
        ret2 = hash_add(&config, config.hashtable, shown_key, pg_index);                /*  PG_VALID  */
        if (ret<0 || ret2<0) {
            printk("%s: failed to write back to ram/disk\n", __func__);
            BUG();
        }
        list_add(&config.hashtable[ret2].p_list, config.blocks[victim_blk].list);
        
		kfree(shown_key);
    }
    
    /* update blk info */
    config.blocks[victim_blk].state = BLK_USED;

	kfree(buffer);
    
	return;
}

/* print_hash( void)
 * TODO
 * Return
 * VOID
 */
void print_hash(void)
{
	int i;
    int j;
#if 0
	for (i = 0; i < config.nb_blocks * config.pages_per_block; i++) {
		if(config.hashtable[i].p_state == PG_VALID)
			printk(PRINT_PREF "%d: dirty: %d, p_state: %d, index: %d, key:%s\n", i, \
				config.hashtable[i].dirty, \
				config.hashtable[i].p_state, \
				config.hashtable[i].index, \
				config.hashtable[i].key);
	}
#endif
	for (i=0, j=0; i < config.nb_blocks; i++) 
	{
 
        printk(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                    i, config.blocks[i].state,
                                    config.blocks[i].worn,
                                    config.blocks[i].nb_invalid,
                                    config.blocks[i].current_page_offset);
    }

	for (i = 0; i < config.nb_meta_blocks; i++)
		printk("config.meta_blkordr[%d]: %d\n",i, config.meta_blkordr[i]);
}

/* Setup init and exit functions */
module_init(lkp_kv_init);
module_exit(lkp_kv_exit);

/**
 * Infos generale sur le module
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LKP VT");
MODULE_DESCRIPTION("LKP key-value store prototype");
