/**
 * This file contains the prototype core functionalities.
 */
meta_config


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/hrtimer.h>

#include "core.h"
#include "device.h"
#include "hash.h"

#define MERGE 0
#define INVALID_THRESHOLD 3
#define PRINT_PREF KERN_INFO "[LKP_KV]: "

spinlock_t one_lock;

#define JACK_DBG_LOG 1
#if JACK_DBG_LOG
#define JDBG(...) printk(__VA_ARGS__) 
#else
#define JDBG(...) ;
#endif

/* prototypes */
int init_config(int mtd_index, int meta_index);
void destroy_config(void);
void print_config(void);
void print_meta_config(void);
int write_page(int page_index, const char *buf);
int write_meta_page(int page_index, const char *buf);
int read_page(int page_index, char *buf);
int read_meta_page(int page_index, char *buf);
void format_callback(struct erase_info *e);
int get_next_block_to_write(void);
int get_healthy_block(void);
int init_scan(void);
int flush_metadata(void);
void gbtest(void);
void print_hash(void);
int meta_on_disk_format(void);

/* Timer Interrupt Prototypes & Globals */
static int init_flush_timer(void);
static void clear_flush_timer(void);
static enum hrtimer_restart flush_timer_callback( struct hrtimer *f_timer);
static struct hrtimer f_timer;

static int init_wear_timer(void);
static void clear_wear_timer(void);
static enum hrtimer_restart wear_timer_callback( struct hrtimer *w_timer);
static struct hrtimer w_timer;

/* Global Config Variables */
lkp_kv_cfg config;
lkp_meta_cfg meta_config;
HASH_TABLE(hashtable, HASH_SIZE);

/* The module tases one parameter which is the index of the target flash
 * partition */
int MTD_INDEX = -1;
int META_INDEX = -2;
module_param(MTD_INDEX, int, 0);
module_param(META_INDEX, int, 0);
MODULE_PARM_DESC(MTD_INDEX, "Index of target mtd partition");
MODULE_PARM_DESC(META_INDEX, "Index of metadata partition");
/**
 * Module initialization function
 */
static int __init lkp_kv_init(void)
{
	printk(PRINT_PREF "Loading... \n");

	if (init_config(MTD_INDEX, META_INDEX) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -2;
	}
    
    spin_lock_init(&one_lock);

	/*Initialize Periodic flushing of RAM metadata to disk */
	if( init_flush_timer() != 0){
		printk(KERN_ERR "Metadata flush-to-disk interrupt creation Error\n");
		return -3;
	}

	/*Initialize Periodic Wear Leveling shuffling interrupt */
	if( init_wear_timer() != 0){
		printk(KERN_ERR "Wear leveling interrupt creation Error\n");
		return -4;
	}

	return 0;
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	/* TODO */

	printk(PRINT_PREF "Exiting ... \n");
	
	if (flush_metadata() == 0)
		printk(PRINT_PREF "Flush success ... \n");
	else
		printk(PRINT_PREF "Flush failed ... \n");

	//Disable RAM-2-disk timer interrupt
	clear_flush_timer();
	clear_wear_timer();
	
	device_exit();
	destroy_config();
	printk(PRINT_PREF "Module exit\n\n");
}

/**
 * Global state initialization, return 0 when ok, -1 on error
 */
int init_config(int mtd_index, int meta_index)
{
	uint64_t tmp_blk_num;
	
	if (mtd_index == -1) {
		printk(PRINT_PREF
		       "Error, flash partition index missing, should be"
		       " indicated for example like this: MTD_INDEX=5\n");
		return -1;
	}

	if (meta_index == -2) {
		printk(PRINT_PREF
		       "Error, metadata partition index missing, should be"
		       " indicated for example like this: META_INDEX=6\n");
		return -1;

	}

	config.format_done = 0;
	config.read_only = 0;

	meta_config.format_done = 0;
	meta_config.read_only = 0;

	config.mtd_index = mtd_index;
	meta_config.mtd_index = meta_index;
	/* The flash partition is manipulated by caling the driver, through the
	 * mtd_info object. There is one of these object per flash partition */
	config.mtd = get_mtd_device(NULL, mtd_index);
	meta_config.mtd = get_mtd_device(NULL, meta_index);

	if (config.mtd == NULL)
		return -1;

	if (meta_config.mtd == NULL)
		return -2;

	config.block_size = config.mtd->erasesize;
	config.page_size = config.mtd->writesize;
	config.pages_per_block = config.block_size / config.page_size;

	meta_config.block_size = meta_config.mtd->erasesize;
	meta_config.page_size = meta_config.mtd->writesize;
	meta_config.pages_per_block = meta_config.block_size / meta_config.page_size;

	tmp_blk_num = config.mtd->size;
	do_div(tmp_blk_num, (uint64_t) config.mtd->erasesize);
	config.nb_blocks = (int)tmp_blk_num;

	tmp_blk_num = meta_config.mtd->size;
	do_div(tmp_blk_num, (uint64_t) meta_config.mtd->erasesize);
	meta_config.nb_blocks = (int)tmp_blk_num;

	/* Semaphore initialized to 1 (available) */
	sema_init(&config.format_lock, 1);
	sema_init(&meta_config.format_lock, 1);

	meta_config.blocks = vmalloc(sizeof(blk_info)*config.nb_blocks);
	config.blocks = meta_config.blocks;

	/* Flash scan for metadata creation: which flash blocks and pages are 
	 * free/occupied */
	if (init_scan() != 0) {
		printk(PRINT_PREF "Init scan error\n");
		return -1;
	}

	print_config();
	printk("\n");
	print_meta_config();

	return 0;
}

/**
 * Launch time metadata creation: flash is scanned to determine which flash 
 * blocs and pages are free/occupied. Return 0 when ok, -1 on error
 */
int init_scan()
{
	int i;
	//char *offset;
	char *buffer;
	int nb_pages;
	int buffer_size;
	
	meta_config.hashtable_size = sizeof(hashtable);
	meta_config.block_info_size = sizeof(blk_info) * config.nb_blocks;
	meta_config.metadata_size = meta_config.hashtable_size + meta_config.block_info_size;
	nb_pages = (meta_config.metadata_size / meta_config.page_size) + 1;
	buffer_size = meta_config.page_size * nb_pages;

	buffer = (char *)vmalloc(buffer_size);

	for (i = 0; i < nb_pages; i++)
		if(read_meta_page(i, buffer + meta_config.page_size *i) != 0)
			return -1;

	memcpy(meta_config.blocks, buffer, meta_config.block_info_size);

	for (i = 0; i < config.nb_blocks; i++)
	{
		meta_config.blocks[i].list = (struct list_head *)vmalloc(sizeof(struct list_head));
		INIT_LIST_HEAD(meta_config.blocks[i].list);

		if(meta_config.blocks[i].state == 0xFFFFFFFF)
		{
			meta_config.blocks[i].state = BLK_FREE;
			meta_config.blocks[i].worn = 0;
			meta_config.blocks[i].nb_invalid = 0;
			meta_config.blocks[i].current_page_offset = 0;
		}
	}

	memcpy(hashtable, buffer + meta_config.block_info_size, meta_config.hashtable_size);

	for (i = 0; i < HASH_SIZE; i++)
	{
/*
		offset = buffer + meta_config.block_info_size + (sizeof(bucket) * i); 
		memcpy(&hashtable[i].dirty,   offset,  sizeof(int)); 
		memcpy(&hashtable[i].p_state, offset + sizeof(int),  sizeof(page_state)); 
		memcpy(&hashtable[i].index,   offset + sizeof(int) + sizeof(page_state),  sizeof(int)); 
		memcpy(hashtable[i].key,      offset + sizeof(int) + sizeof(page_state) + sizeof(int), 128); 
*/
		if (hashtable[i].p_state == PG_VALID)
			list_add(&hashtable[i].p_list, meta_config.blocks[hashtable[i].index / config.pages_per_block].list); 
	}

	if (get_healthy_block() == -1)
	{
		printk(PRINT_PREF "read only mode\n");
		config.read_only = 1;
		meta_config.read_only = 1;
	}

	vfree(buffer);
	return 0;
}

int flush_metadata()
{
	int i;
	char *buffer;
	int nb_pages = (meta_config.metadata_size / meta_config.page_size) + 1;
	int buffer_size = meta_config.page_size * nb_pages;
	
	if (meta_on_disk_format() != 0)
		return -2;
	
	buffer = (char *)kmalloc(buffer_size, GFP_ATOMIC);
    if(!buffer) {
        printk(KERN_ERR "kmalloc failed\n");
        BUG();
    }

	//memcpy(buffer, &meta_config.number_of_valid_pages, sizeof(int));
	
	memcpy(buffer, meta_config.blocks, meta_config.block_info_size);
	memcpy(buffer + meta_config.block_info_size, hashtable, meta_config.hashtable_size);

	for (i = 0; i < nb_pages; i++)
		if ( write_meta_page(i, buffer + meta_config.page_size * i) != 0 )
			return -1;

	kfree(buffer);
	return 0;
}

/**
 * Freeing stuff on exit
 */
void destroy_config(void)
{
	vfree(meta_config.blocks);
	put_mtd_device(config.mtd);
	put_mtd_device(meta_config.mtd);
}

void invalid_pg(int hash_idx)
{
    int pg_idx;
    unsigned long flags;
    spin_lock_irqsave(&one_lock, flags);
    
    hashtable[hash_idx].p_state = PG_FREE;
    pg_idx = hashtable[hash_idx].index;
    meta_config.blocks[pg_idx/config.pages_per_block].nb_invalid++;
	/* delete the page metadata from the valid-page-list */
    list_del(&hashtable[hash_idx].p_list);
	printk("invalid a page in blk %d\n", pg_idx/config.pages_per_block);
    
    spin_unlock_irqrestore(&one_lock, flags);
}

/**
 * Adding a key-value couple. Returns -1 when ok and a negative value on error:
 * -1 when the size to write is too big
 * -2 when the key already exists
 * -3 when we are in read-only mode
 * -4 when the MTD driver returns an error
 */
int set_keyval(const char *key, const char *val)
{
	int key_len, val_len, i, ret, ret2, index, hash_idx;
	char *buffer;
	int target_block;

	key_len = strlen(key);
	val_len = strlen(val);

	if (!key)
	{
		printk("NULL pointer execption\n");
		return -5;
	}

	if ((key_len + val_len + 2 * sizeof(int)) > config.page_size) {
		/* size to write is too big */
		return -1;
	}

	/* the buffer that we are going to write on flash */
	buffer = (char *)kmalloc(config.page_size * sizeof(char), GFP_ATOMIC);
    if(!buffer) {
        printk(KERN_ERR "kmalloc failed\n");
        BUG();
    }

	/* if the key already exists, return without writing anything to flash */
	hash_idx = hash_search(hashtable, key);
	if (hash_idx >= 0) {
		//printk(PRINT_PREF "Key \"%s\" already exists in page %d. Replacing it\n", key, hashtable[ret].index);
        invalid_pg(hash_idx);
	}

	/* prepare the buffer we are going to write on flash */
	for (i = 0; i < config.page_size; i++)
		buffer[i] = 0x0;

	/* key size ... */
	memcpy(buffer, &key_len, sizeof(int));
	/* ... value size ... */
	memcpy(buffer + sizeof(int), &val_len, sizeof(int));
	/* ... the key itself ... */
	memcpy(buffer + 2 * sizeof(int), key, key_len);
	/* ... then the value itself. */
	memcpy(buffer + 2 * sizeof(int) + key_len, val, val_len);

	target_block = get_next_block_to_write();

	index = target_block * config.pages_per_block + meta_config.blocks[target_block].current_page_offset;
	//printk("index: %d\n",index);	
	meta_config.blocks[target_block].state = BLK_USED;

	/* actual write on flash */
	ret = write_page(index, buffer);
	ret2 = hash_add(hashtable, key, index);
	
	/* add page metadata to the valid-page-list */
	list_add(&hashtable[ret2].p_list, meta_config.blocks[target_block].list); 
	
	kfree(buffer);

	if (ret == -1)		/* read-only */
		return -3;
	else if (ret == -2)	/* write error */
		return -4;

	if (ret2 < 0)
		return -5; /* hash_add error */

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
	//int i, j;
	char *buffer;
	int key_len, val_len;
	char *cur_key, *cur_val;
	int hash_index = hash_search(hashtable, key);
	int page_index = -1;

	buffer = (char *)kmalloc(config.page_size * sizeof(char), GFP_ATOMIC);
    if(!buffer) {
        printk(KERN_ERR "kmalloc failed\n");
        BUG();
    }

	if (hash_index >= 0)
	{		
		page_index = hashtable[hash_index].index; 
		if(meta_config.blocks[page_index / config.pages_per_block].state == BLK_USED)
		{
			if (hashtable[hash_index].p_state == PG_FREE)
			{
				return -1;
			}

			if (read_page(page_index, buffer) != 0) 
			{
				vfree(buffer);
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
				vfree(buffer);
				return page_index;
			}
		}
	}
	/* key not found */
	kfree(buffer);
	return -1;
}

int del_key(const char *key)
{
	int hash_index, page_index = -1;

	hash_index = hash_search(hashtable, key);
	if (hash_index >= 0) {
		page_index = hashtable[hash_index].index;
		if(meta_config.blocks[page_index/config.pages_per_block].state == BLK_USED) {
            if (hashtable[hash_index].p_state == PG_FREE) {
                //printk("deleting an empty key\n");
				return -1;
            }
            invalid_pg(hash_index);
            //printk("deleting key \"%s\"\n", key);
            return page_index;
		}
	}
    
    /* key not found */
    printk("%s(): key not found\n", __func__);
	return -1;
}

/**
 * After an insertion, determine which is the flash page that will receive the 
 * next insertion. Return the correspondign flash page index, or -1 if the 
 * flash is full
 */
int get_next_block_to_write()
{
	int target_block;
	target_block = get_healthy_block();

	if (target_block == -1)
		return -1;

	meta_config.blocks[target_block].state = BLK_USED;

	return target_block;
}

int get_healthy_block()
{
	int i;
	int ret = -1;
	int minimum = 0x7FFFFFFF;

	for (i = 0; i < config.nb_blocks; i++) {
		if (meta_config.blocks[i].current_page_offset != config.pages_per_block)
			if (meta_config.blocks[i].worn < minimum)
			{
				minimum = meta_config.blocks[i].worn;
				ret = i;
			}
	}	
	
	/* If we get there, no free block left... */
	//if (minimum == config.pages_per_block - 1)
	//	return -1;

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

/**
 * METADATA
 * Callback for the erase operation done during the format process of metadata
 */
void meta_format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Format error...");
		down(&meta_config.format_lock);
		meta_config.format_done = -1;
		up(&meta_config.format_lock);
		return;
	}

	down(&meta_config.format_lock);
	meta_config.format_done = 1;
	up(&meta_config.format_lock);
}


/* meta_on_disk_format
 * Action: Will format/erase entire metadata partition 
 * (currently metadata has its own partition)
 * 
 * Return:
 * 0 = Sucess
 * -1 = MTD driver (_erase) error
 * -2 = format error within _erase
 */
int meta_on_disk_format()
{
	struct erase_info mei;
	
	/* metadata partition */
	mei.mtd = meta_config.mtd;
	mei.len = ((uint64_t) meta_config.block_size) * ((uint64_t) meta_config.nb_blocks);
	mei.addr = 0x0;
	
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done
	 * callback is meta_format_callback(struct erase_info *e)
	 * (Will be called by _erase?!)
	 */
	mei.callback = meta_format_callback;

	//Reset format_done flag
	meta_config.format_done = 0;

	/* Call the MTD driver  */
	if (meta_config.mtd->_erase(meta_config.mtd, &mei) != 0)
		return -1;

	while (1){
		//attempt semaphore loc
		if (!down_trylock(&meta_config.format_lock)) {
			//semaphore acquired 
			if (meta_config.format_done) {
				//format of metadata completed
				//release semaphore, get out of infinite loop
				up(&meta_config.format_lock);
				break;
			}
			//release semaphore
			up(&meta_config.format_lock);
		}
	}//END while

	//some kind of formating error
	if (meta_config.format_done == -1)
		return -2;

	return 0;
}


int format_single(int idx)
{
    struct erase_info ei;
    bucket *current_page, *next;

    ei.mtd = config.mtd;
    ei.len = ((uint64_t) config.block_size);
    ei.addr = 0x0;

	ei.callback = format_callback;
	config.format_done = 0;

	/* Call the MTD driver  */
	if (config.mtd->_erase(config.mtd, &ei) != 0)
		return -1;
	
    /* TODO change to a condwait here */
	while (1)
		if (!down_trylock(&config.format_lock)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
        }

	
    if (config.format_done == -1)
		return -1;

	//config.read_only = 0; // all
/*
    meta_config.blocks[idx/config.pages_per_block].state = BLK_FREE;
    //meta_config.blocks[idx].worn = 0;
    meta_config.blocks[idx/config.pages_per_block].nb_invalid = 0;
    meta_config.blocks[idx/config.pages_per_block].current_page_offset = 0;
  */  
    meta_config.blocks[idx].state = BLK_FREE;
    //meta_config.blocks[idx].worn = 0;
    meta_config.blocks[idx].nb_invalid = 0;
    meta_config.blocks[idx].current_page_offset = 0;

    // hashtable meta data
    // TODO: delete every thing in that valid_blk_list
	//for (i = 0; i < HASH_SIZE; i++) {
        //hashtable[idx].p_state = PG_FREE;
        //hashtable[idx].dirty = 0;
    //}

    //TODO list_del
    //list_for_each_entry_safe(current_page, next, meta_config.blocks[idx/config.pages_per_block].list, p_list) {
    list_for_each_entry_safe(current_page, next, meta_config.blocks[idx].list, p_list) {
        if(current_page) {
            //if (current_page->index == idx) {
                list_del(&current_page->p_list);
            //    break;
            //}
        }
    }


	printk(PRINT_PREF "Formating a single block done\n");

    // global meta data in nand
	//meta_config.blocks[0].state = BLK_USED;
	//meta_config.read_only = 0; // all

	/* format the metadata on the disk */
	//if (meta_on_disk_format() != 0)
	//	return -1;

    return 0;
}


/**
 * Format operation: we erase the entire flash partition
 */
int format()
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
	if (config.mtd->_erase(config.mtd, &ei) != 0)
		return -1;

	/* on attend la fin effective de l'operation avec un spinlock. 
	 * C'est la fonction callback qui mettra format_done a 1 */
	/* TODO change to a condwait here */
	while (1)
		if (!down_trylock(&config.format_lock)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
		}


	/* was there a driver issue related to the erase oepration? */
	if (config.format_done == -1)
		return -1;

	config.read_only = 0;

	/* format metadata in the memory */
	for (i = 0; i < config.nb_blocks; i++) {
		list_for_each_entry_safe(current_page, next, meta_config.blocks[i].list, p_list)
		{
			if(current_page)
			{
				list_del(&current_page->p_list);
			}
		}

		meta_config.blocks[i].state = BLK_FREE;
		meta_config.blocks[i].worn = 0;
		meta_config.blocks[i].nb_invalid = 0;
		meta_config.blocks[i].current_page_offset = 0;
	}

	for (i = 0; i < HASH_SIZE; i++)
	{
		hashtable[i].p_state = PG_FREE;
		hashtable[i].dirty = 0;
	}

	meta_config.blocks[0].state = BLK_USED;
	meta_config.read_only = 0;

	printk(PRINT_PREF "Format done\n");

	/* format the metadata on the disk */
	if (meta_on_disk_format() != 0)
		return -1;

	printk(PRINT_PREF "Metadata format done\n");
	
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
    int ret = 0;
	uint64_t addr;
	size_t retlen;
    unsigned long flags;
    spin_lock_irqsave(&one_lock, flags);

	/* if the flash partition is full, dont write */
	if (config.read_only) {
        ret = -1;
        goto exit;
    }
	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);

	/* call the NAND driver MTD to perform the write operation */
	if (config.mtd->_write(config.mtd, addr, config.page_size, &retlen, buf) != 0){
		ret = -2;
        goto exit;
    }

	meta_config.blocks[page_index/config.pages_per_block].current_page_offset++;

	/* if the flash partition is full, switch to read-only mode */
	if (get_healthy_block() == -1)
	{
		printk(PRINT_PREF "no free block left... swtiching to read-only mode\n");
		config.read_only = 1;
		ret = -1;
        goto exit;
	}

exit:
    spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}

/* write pages on metadata partition */
int write_meta_page(int page_index, const char *buf)
{
    int ret = 0;
	uint64_t addr;
	size_t retlen;
    unsigned long flags;
    spin_lock_irqsave(&one_lock, flags);

	/* if the flash partition is full, dont write */
	if (meta_config.read_only) {
        ret = -1;
        goto exit2;
    }
    
	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) meta_config.page_size);

	/* call the NAND driver MTD to perform the write operation */
	if (meta_config.mtd->_write(meta_config.mtd, addr, meta_config.page_size, &retlen, buf) != 0) {
		ret = -2;
        goto exit2;
    }
exit2:
    spin_unlock_irqrestore(&one_lock, flags);
	return ret;
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
    unsigned long flags;
    spin_lock_irqsave(&one_lock, flags);

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);
	
	/* call the NAND driver MTD to perform the read operation */
	ret = config.mtd->_read(config.mtd, addr, config.page_size, &retlen, buf);
    
    spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}

int read_meta_page(int page_index, char *buf)
{
    int ret;
	uint64_t addr;
	size_t retlen;
    unsigned long flags;
    spin_lock_irqsave(&one_lock, flags);

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) meta_config.page_size);
	
	/* call the NAND driver MTD to perform the read operation */
	ret = meta_config.mtd->_read(meta_config.mtd, addr, meta_config.page_size, &retlen, buf);
    spin_unlock_irqrestore(&one_lock, flags);
	return ret; 
}

/*****************************************************************************/
/* Timer Interrupt Functions                                                 */
/*****************************************************************************/
unsigned long delay = 1000L*1e6L;
unsigned long w_delay = 100L*1e6L;
static int init_flush_timer(void){
	ktime_t ktime;
	printk(KERN_INFO ">>[%s]: timer being setup\n",__func__);

	//ktime_set(seconds, nanoseconds)
	ktime = ktime_set(0, delay);
	hrtimer_init( &f_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	f_timer.function = &flush_timer_callback;
	printk(KERN_INFO ">> RAM FLUSH Timer start\n");
	printk(KERN_INFO ">> Delay is:%lums %llu\n", delay, get_jiffies_64());

	hrtimer_start( &f_timer, ktime, HRTIMER_MODE_REL);
	return 0;
}

static void clear_flush_timer(void){
	int cancelled;
	printk(KERN_INFO ">>%s\n",__func__);

	cancelled = hrtimer_cancel( &f_timer);
	if(cancelled){
		printk(KERN_ERR ">>ERR: Timer still running !!!\n");
	}else{
		printk(KERN_INFO ">>Timer cancelled\n");
	}
}

//flush callback working now
static enum hrtimer_restart flush_timer_callback( struct hrtimer *f_timer)
{
	ktime_t currtime, interval;
	//printk(KERN_INFO ">>[%s]\n",__func__);
	currtime = ktime_get();
	interval = ktime_set(0, delay);
	hrtimer_forward(f_timer, currtime, interval);

	//Initiate Flushing
	//flush_metadata();

	//What else?
	return HRTIMER_RESTART;
}

static int init_wear_timer(void){
	ktime_t ktime;
	JDBG(KERN_INFO ">>%s: timer being setup~\n",__func__);

	ktime = ktime_set(0, w_delay);
	hrtimer_init( &w_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	w_timer.function = &wear_timer_callback;
	printk(KERN_INFO ">>WEAR LEVELING Timer start\n");
	printk(KERN_INFO ">> In:%lums %llu\n", w_delay, get_jiffies_64());

	hrtimer_start( &w_timer, ktime, HRTIMER_MODE_REL);
	return 0;
}
	
static void clear_wear_timer(void)
{
	int cancelled;
	printk(KERN_INFO ">>%s\n",__func__);

	cancelled = hrtimer_cancel( &w_timer);
	if(cancelled){
		printk(KERN_ERR ">>ERR: Timer still running !!!\n");
	}else{
		printk(KERN_INFO ">>Timer cancelled\n");
	}
}
	
static enum hrtimer_restart wear_timer_callback( struct hrtimer *w_timer)
{
	ktime_t currtime, interval;
	//JDBG(KERN_INFO ">>%s\n",__func__);

	currtime = ktime_get();
	interval = ktime_set(0, w_delay);
	hrtimer_forward(w_timer, currtime, interval);
	//Need to call Wear leveling functions here to shuffle data to different blocks
    //gbtest();
    
	return HRTIMER_RESTART;	
}


///////////////////////////////////////////////////////////////////////////////
/**
 * Print some statistics on the kernel log
 */
void print_config()
{
	printk(PRINT_PREF "Data partition's config : \n");
	printk(PRINT_PREF "=========\n");

	printk(PRINT_PREF "mtd_index: %d\n", config.mtd_index);
	printk(PRINT_PREF "nb_blocks: %d\n", config.nb_blocks);
	printk(PRINT_PREF "block_size: %d\n", config.block_size);
	printk(PRINT_PREF "page_size: %d\n", config.page_size);
	printk(PRINT_PREF "pages_per_block: %d\n", config.pages_per_block);
	printk(PRINT_PREF "read_only: %d\n", config.read_only);
}

void print_meta_config()
{
	printk(PRINT_PREF "Metadata partition's config : \n");
	printk(PRINT_PREF "=========\n");
	
	printk(PRINT_PREF "mtd_index: %d\n", meta_config.mtd_index);
	printk(PRINT_PREF "nb_blocks: %d\n", meta_config.nb_blocks);
	printk(PRINT_PREF "block_size: %d\n", meta_config.block_size);
	printk(PRINT_PREF "page_size: %d\n", meta_config.page_size);
	printk(PRINT_PREF "pages_per_block: %d\n", meta_config.pages_per_block);
	printk(PRINT_PREF "read_only: %d\n", meta_config.read_only);
	printk(PRINT_PREF "hashtable size: %d\n", meta_config.hashtable_size);
	printk(PRINT_PREF "Blocks table size: %d\n", meta_config.block_info_size);
	printk(PRINT_PREF "metadata size: %d\n",meta_config.metadata_size);
	//printk(PRINT_PREF "number of valid pages: %d\n", meta_config.number_of_valid_pages);
}

#if 0
void gbtest_merge(void)
{
    int pg_index, ret, ret2;
    int hash_index, valid_cnt = 0;
    int i, j, target_blk1 = -1, target_blk2 = -1,victom_blk = -1;
	int tmp_key_len, tmp_val_len;
    int key_len[config.pages_per_block], val_len[config.pages_per_block];
    char *buffer;
    char *tmp_key;
	char *cur_key[config.pages_per_block], *cur_val[config.pages_per_block];
    int merged_blk1 = -1, merged_blk2 = -1;

    // single victom
	for (i = 0; i < config.nb_blocks; i++) {
        if(meta_config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
            target_blk1 = i;
            break;
        }
	}  
    if(target_blk1 == -1) {
        printk("No need to do GB\n");
        return;
    }

    // This ability need us to alway reserve a free block
    // Before write, if only one free blk, do GC
    // two victoms (merge)
    for (i = 0; i < config.nb_blocks; i++) {
        if (meta_config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
            if (meta_config.blocks[i].current_page_offset < config.pages_per_block/2) {
                if (target_blk1 == -1)
                    target_blk1 = i; 
                else {
                    targer_blk2 = i;
                    break;
                }
            }
        }
    }
    if( target_blk1 == -1) {
        printk("No need to do GB\n");
        return;
    }
    
    if (target_blk2 == -1) {
        prinktk("Cannot find two candidate blocks to merge\n");
        return;
    }

    printk("GBing......\n");
    // look for a victom blk (the least worn)
	for (i = 0; i < config.nb_blocks; i++) {
        if (BLK_FREE) {
            if (victom_blk == -1)
                victom_blk = i;
            else if (meta_config.blocks[i].worn < 
                        meta_config.blocks[victom_blk].worn)
                victom_blk = i;
        }
    }
    if(victom_blk == -1) { //no BLK_FREE, write it to itself
        printk(KERN_ERR "write should garauntee there will always have a free blk (victom_blk)\n");
        BUG();
    }
    
    printk("(merge) target_blk1 %d target_blk2 %d victom_blk %d \n", target_blk1, target_blk2, victom_blk);

    /* 1. read pages */
	buffer = vmalloc(config.page_size * sizeof(char));

    // copy target_blk1 to victom
    // walk hashtable, store all target_blk info
    struct jack1 *current_page, *next;
    list_for_each_entry_safe(current_page, next, &LIST_HEAD_JACK, list) {
        for (j = 0; j < config.page_size; j++)
            buffer[j] = 0x0;

        //if (read_page((target_blk*config.pages_per_block)+i, buffer) != 0) {
        current_page->pg_idx = hashtable[current_page->hash_idx].index;   // redundant!?
        if (read_page(current_page->pg_idx, buffer) != 0) {
            printk(KERN_ERR "%s(): read_page failed\n", __func__);
            vfree(buffer);
            BUG();
        }

        memcpy(&key_len[valid_cnt], buffer, sizeof(int));
        memcpy(&val_len[valid_cnt], buffer + sizeof(int), sizeof(int));
        cur_key[valid_cnt] = buffer + 2 * sizeof(int);
        cur_val[valid_cnt] = buffer + 2 * sizeof(int) + key_len[valid_cnt];
           
        // invalide in hashtable
        hashtable[current_page->hash_idx].p_state = PG_FREE;   // doens't matter (set/update)
        
        // record
        valid_cnt++;

        // remove from list
        list_del(&current_page->list);
        kfree(current_page); /* if this was dynamically allocated through kmalloc */
    }

    // do the same thing to targetblk2 
    // copy target_blk2 to victom
    if(target_blk2 == -1) {
        printk(KERN_ERR "STH IS WRONG!!\n");
        BUG();
    }
    
    
    walk_hash(&LIST_HEAD_JACK2);
    // copy target_blk1 to victom
    // walk hashtable, store all target_blk info
    struct jack1 *current_page, *next;
    list_for_each_entry_safe(current_page, next, &LIST_HEAD_JACK2, list) {
        for (j = 0; j < config.page_size; j++)
            buffer[j] = 0x0;

        //if (read_page((target_blk*config.pages_per_block)+i, buffer) != 0) {
        current_page->pg_idx = hashtable[current_page->hash_idx].index;   // redundant!?
        if (read_page(current_page->pg_idx, buffer) != 0) {
            printk(KERN_ERR "%s(): read_page failed\n", __func__);
            vfree(buffer);
            BUG();
        }

        memcpy(&key_len[valid_cnt], buffer, sizeof(int));
        memcpy(&val_len[valid_cnt], buffer + sizeof(int), sizeof(int));
        cur_key[valid_cnt] = buffer + 2 * sizeof(int);
        cur_val[valid_cnt] = buffer + 2 * sizeof(int) + key_len[valid_cnt];
           
        // invalide in hashtable
        hashtable[current_page->hash_idx].p_state = PG_FREE;   // doens't matter (set/update)
        
        // record
        valid_cnt++;

        // remove from list
        list_del(&current_page->list);
        kfree(current_page); /* if this was dynamically allocated through kmalloc */
    }
 


    // double check ( for two blocks)
    /*
    if ( meta_config.blocks[target_blk].current_page_offset - valid_cnt ==
                meta_config.blocks[target_blk].nb_invalid) {
        printk("GOOD cnt - valid_cnt %d invalided %d\n",
                        valid_cnt, meta_config.blocks[target_blk].nb_invalid);
    } else {
        printk("BAD cnt - valid_cnt %d cur_ofs %d invalided %d\n",
                                    valid_cnt,
                                    meta_config.blocks[target_blk].current_page_offset,
                                    meta_config.blocks[target_blk].nb_invalid);

    }
    */
    if( valid_cnt > config.pages_per_block) {
        printk("Merge: too many pages\n");
        BUG();
    }

    /* 2. erase victom_blk */
    format_single(victom_blk);

    /* 3. write */
    for (i=0; i<valid_cnt; i++) {
        for (j = 0; j < config.page_size; j++)
            buffer[j] = 0x0;
        memcpy(buffer, &key_len[i], sizeof(int));
        memcpy(buffer + sizeof(int), &val_len[i], sizeof(int));
        memcpy(buffer + 2 * sizeof(int), cur_key[i], key_len[i]);
        memcpy(buffer + 2 * sizeof(int) + key_len[i], cur_val[i], val_len[i]);
        
        printk("debug: %s\n", buffer);
    
        // int target_block = get_next_block_to_write();
        printk("meta_config.blocks[victom_blk].current_page_offset %d (should be 0~valid_cnt)\n",
                                    meta_config.blocks[victom_blk].current_page_offset);
	    pg_index = (victom_blk * config.pages_per_block)
                            + meta_config.blocks[victom_blk].current_page_offset;
        // write to disk&ram
	    ret = write_page(pg_index, buffer);    // update .current_page_offset
        ret2 = hash_add(hashtable, cur_key[i], pg_index); // PG_VALID
        if (ret<0 || ret2<0) {
            printk("%s: failed to write back to ram/disk\n", __func__);
            return;
        }
    }
    
    /* update blk info */
    meta_config.blocks[victom_blk].state = BLK_USED;
	meta_config.blocks[victom_blk].worn++; // real wearleveing cnt

	vfree(buffer);
    printk("\n\n\n");
    return;
}
#endif 


void gbtest(void)
{
    int pg_index, ret, ret2;
    int hash_index;
    int valid_cnt = 0;
    int i, j, target_blk1 = -1, victom_blk = -1;
    int key_len[config.pages_per_block], val_len[config.pages_per_block];
    char *buffer;
	char *cur_key[config.pages_per_block], *cur_val[config.pages_per_block];
    bucket *current_page, *next;

    /* find a victom */
	for (i = 0; i < config.nb_blocks; i++) {
        if(meta_config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
            target_blk1 = i;
            break;
        }
	}  
    if(target_blk1 == -1) {
        printk("No need to do GB\n");
        return;
    }

    JDBG("GBing......\n");
    // look for a victom blk (the least worn)
	for (i = 0; i < config.nb_blocks; i++) {
        if (BLK_FREE) {
            if (victom_blk == -1)
                victom_blk = i;
            else if (meta_config.blocks[i].worn < 
                        meta_config.blocks[victom_blk].worn)
                victom_blk = i;
        }
    }

    if(victom_blk == -1) { //no BLK_FREE, write it to itself
        victom_blk = target_blk1;
    }

    JDBG("target_blk %d victom_blk %d \n", target_blk1, victom_blk);

    /* 1. read pages */
	buffer = kmalloc(config.page_size * sizeof(char) * config.pages_per_block, GFP_ATOMIC);
    if(!buffer) {
        printk(KERN_ERR "kmalloc failed\n");
        BUG();
    }
    for (j = 0; j < config.page_size * sizeof(char) * config.pages_per_block; j++)
        *(buffer+j) = 0x0;

#if 1
    // walk hashtable, store all target_blk info
    list_for_each_entry_safe(current_page, next, meta_config.blocks[target_blk1].list, p_list)
    {
        char* true_key;
        if (!current_page) {
            printk(KERN_ERR "%s(): STH WRONG HAPPENED\n", __func__);
            break;
        }
        
        JDBG(PRINT_PREF "iterating pg_idx %d\n", current_page->index);
        if ( current_page->index/config.pages_per_block != target_blk1 ) {
            printk(KERN_WARNING "WARN: valid_list info is wrong\n");
            break;
        }
        //for (j = 0; j < config.page_size; j++)
        //    *(buffer+(valid_cnt*config.pages_per_block)+j) = 0x0;


        //if (read_page((target_blk*config.pages_per_block)+i, buffer) != 0) {
        //current_page->pg_idx = hashtable[current_page->hash_idx].index;   // redundant!?
        if (read_page(current_page->index, buffer+(valid_cnt*config.pages_per_block)) != 0) {
            printk(KERN_ERR "%s(): read_page failed\n", __func__);
            kfree(buffer);
            BUG();
        }
        memcpy(&key_len[valid_cnt], buffer+(valid_cnt*config.pages_per_block), sizeof(int));
        memcpy(&val_len[valid_cnt], buffer+(valid_cnt*config.pages_per_block) + sizeof(int), sizeof(int));
        cur_key[valid_cnt] = buffer+(valid_cnt*config.pages_per_block) + 2 * sizeof(int);
        cur_val[valid_cnt] = buffer+(valid_cnt*config.pages_per_block) + 2 * sizeof(int) + key_len[valid_cnt];

        JDBG("(R) len: %d %d\n", key_len[valid_cnt], val_len[valid_cnt]);
        JDBG("(R) data: %s \n", cur_key[valid_cnt]);

        // invalide in hashtable
        true_key = kmalloc(key_len[valid_cnt], GFP_ATOMIC);
        if(!true_key) {
            printk(KERN_ERR "vallocation failed\n");
            BUG();
        }
        strncpy(true_key, cur_key[valid_cnt], key_len[valid_cnt]);
        hash_index = hash_search(hashtable, true_key);
        hashtable[hash_index].p_state = PG_FREE;   // doens't matter (set/update)
        kfree(true_key);

        // record
        valid_cnt++;

        // remove from list
        list_del(&current_page->p_list);
        //kfree(current_page); /* if this was dynamically allocated through kmalloc */
    }
#endif

#if 0
    // walk pages among a blk
    for (i=0; i<config.pages_per_block; i++) {
        char* true_key;
        if (read_page((target_blk*config.pages_per_block)+i, buffer) != 0) {
            printk(KERN_ERR "%s(): read_page failed\n", __func__);
            kfree(buffer);
            BUG();
            //return;
            //return -2;
        }
    
        if(*buffer == -1) {
            //printk("This is a empty page\n");
            continue;
            //break;
        }

        // key
        memcpy(&tmp_key_len, buffer, sizeof(int));
        memcpy(&tmp_val_len, buffer + sizeof(int), sizeof(int));
        tmp_key = buffer + 2 * sizeof(int);
        //tmp_val = buffer + 2 * sizeof(int) + key_len;
        
        true_key = kmalloc(tmp_key_len);
        strncpy(true_key, tmp_key, tmp_key_len);

        hash_index = hash_search(hashtable, true_key);
        //hash_index = hash_search(hashtable, tmp_key);
        if(hash_index >= 0) { // exist
            printk("tmp_key %s\n", true_key);
            //for (j = 0; j < config.page_size; j++)
            //    buffer[j] = 0x0;
            memcpy(&key_len[valid_cnt], buffer, sizeof(int));
            memcpy(&val_len[valid_cnt], buffer + sizeof(int), sizeof(int));
            cur_key[valid_cnt] = buffer + 2 * sizeof(int);
            cur_val[valid_cnt] = buffer + 2 * sizeof(int) + key_len[valid_cnt];
           
            // invalide in hashtable
            //hashtable[hash_index].p_state = PG_FREE;
            //hashtable[hash_index].p_state = PG_VALID;
		    //meta_config.blocks[page_index/config.pages_per_block].nb_invalid++;

            // record
            valid_cnt++;
         } else { printk("tmp_key NULL (invalidated)\n"); }

         kfree(true_key);
    }
#endif

    // double check
    if ( meta_config.blocks[target_blk1].current_page_offset-valid_cnt == meta_config.blocks[target_blk1].nb_invalid) {
        JDBG("GOOD cnt - valid_cnt %d invalided %d\n",
                        valid_cnt, meta_config.blocks[target_blk1].nb_invalid);
    } else {
        JDBG("BAD cnt - valid_cnt %d cur_ofs %d invalided %d\n",
                                    valid_cnt,
                                    meta_config.blocks[target_blk1].current_page_offset,
                                    meta_config.blocks[target_blk1].nb_invalid);

    }

    /* 2. erase victom_blk */
    format_single(victom_blk);
   
    //for (j = 0; j < config.page_size * sizeof(char) * config.pages_per_block; j++)
    //    *(buffer+j) = 0x0;
    /* 3. write */
    for (i=0; i<valid_cnt; i++) {
        //char* true_key;
        char* shown_key;
        
        memcpy(buffer+(i*config.pages_per_block), &key_len[i], sizeof(int));
        memcpy(buffer+(i*config.pages_per_block) + sizeof(int), &val_len[i], sizeof(int));
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int), cur_key[i], key_len[i]);
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int) + key_len[i], cur_val[i], val_len[i]);
        JDBG("(W) len: %d %d\n", key_len[i], val_len[i]);
        JDBG("(W) data: %s \n", cur_key[i]);
        JDBG("(Wbuf) len: %d %d\n", *(buffer+(i*config.pages_per_block)), *(buffer+(i*config.pages_per_block) + sizeof(int)));
        JDBG("(Wbuf) data: %s \n", (buffer+(i*config.pages_per_block) + 2 * sizeof(int)));
        //JDBG("meta_config.blocks[victom_blk].current_page_offset %d (should be 0~valid_cnt)\n",
        //                            meta_config.blocks[victom_blk].current_page_offset);
	    pg_index = (victom_blk * config.pages_per_block)
                            + meta_config.blocks[victom_blk].current_page_offset;
        
        //true_key = kmalloc(key_len[i]);
        shown_key = kmalloc(key_len[i]+1, GFP_ATOMIC);
        /*
        int k;
        for(k=0; k<key_len[i]; k++) {
            *true_key = '\0';
            *shown_key = '\0';
        }
        *(shown_key+key_len[i]) = '\0';
        */
        if(!shown_key) {
            printk(KERN_ERR "vallocation failed\n");
            BUG();
        }
        //strncpy(true_key, cur_key[i], key_len[i]);
        strncpy(shown_key, cur_key[i], key_len[i]);
        strncat(shown_key, "\0", 1);
        JDBG("(SHOWN KEY): keylen %d key %s\n", key_len[i], shown_key);
        //JDBG("(TRUE KEY): keylen %d key %s\n", key_len[i], true_key);
        JDBG("JACK: *shown_key+0 \"%c\"\n", *(shown_key+0));
        JDBG("JACK: *shown_key+1 \"%c\"\n", *(shown_key+1));
        JDBG("JACK: *shown_key+2 \"%c\"\n", *(shown_key+2));
        JDBG("JACK: *shown_key+3 \"%c\"\n", *(shown_key+3));
        JDBG("JACK: *shown_key+4 \"%c\"\n", *(shown_key+4));
        /*
        JDBG("JACK: *true_key+0 \"%c\"\n", *(true_key+0));
        JDBG("JACK: *true_key+1 \"%c\"\n", *(true_key+1));
        JDBG("JACK: *true_key+2 \"%c\"\n", *(true_key+2));
        JDBG("JACK: *true_key+3 \"%c\"\n", *(true_key+3));
        JDBG("JACK: *true_key+4 \"%c\"\n", *(true_key+4));
        */
        *(shown_key+key_len[i]) = '\0';

        JDBG("JACK: shown %lu\n", strlen(shown_key));
        //JDBG("JACK: true %lu\n", strlen(true_key));
        /* write to disk&ram */
	    ret = write_page(pg_index, buffer+(i*config.pages_per_block));    // update .current_page_offset
        ret2 = hash_add(hashtable, shown_key, pg_index); // PG_VALID
        //ret2 = hash_add(hashtable, cur_key[i], pg_index); // PG_VALID
        list_add(&hashtable[ret2].p_list, meta_config.blocks[victom_blk].list);
        if (ret<0 || ret2<0) {
            printk("%s: failed to write back to ram/disk\n", __func__);
            return;
        }
    
        // self testing
#if 0
        char *val;
        val = (char *)kmalloc(val_len[i] * sizeof(char));
        ret = get_keyval(shown_key, val); /* appel au coeur du module */
        if (ret >= 0) { 
            /* write the result to userspace */
            JDBG("Verify: PASS %s %s (ignore this result)\n", shown_key, val);
        } else {
            JDBG("Verify: FAILED %s %s (ignore this result)\n", shown_key, val);
        }
        kfree(val);
#endif
        //kfree(true_key);
        kfree(shown_key);
    }
    
    /* update blk info */
    meta_config.blocks[victom_blk].state = BLK_USED;
	meta_config.blocks[victom_blk].worn++; // real wearleveing cnt

	kfree(buffer);
    JDBG("\n\n\n");
    return;
}

void print_hash()
{
	int i;

	for (i = 0; i < HASH_SIZE; i++)
	{
		if(hashtable[i].p_state == PG_VALID)
			printk(PRINT_PREF "%d: dirty: %d, p_state: %d, index: %d, key:%s\n", i, \
                                                                hashtable[i].dirty, \
                                                                hashtable[i].p_state, \
                                                                hashtable[i].index, \
                                                                hashtable[i].key);
	}
	
#if 0
	for (i = 0; i < config.nb_blocks; i++) 
	{
		printk(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                            i, config.blocks[i].state,
                                            config.blocks[i].worn,
                                            config.blocks[i].nb_invalid,
                                            config.blocks[i].current_page_offset);
#if 0
		printk(PRINT_PREF "block %d's valid pages: \n", i);
		list_for_each_entry(current_page, config.blocks[i].list, p_list)
		{
			bucket *current_page;
			if (current_page)
				printk(PRINT_PREF "%d\n", current_page->index);
		}
#endif
    }
#endif


#if 1
    for (i = 0; i < config.nb_blocks; i++) {
		printk(PRINT_PREF "(meta) %d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                            i, meta_config.blocks[i].state,
                                            meta_config.blocks[i].worn,
                                            meta_config.blocks[i].nb_invalid,
                                            meta_config.blocks[i].current_page_offset);
    }
#endif
    JDBG("\n\n");
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
