/**
 * This file contains the prototype core functionalities.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/hrtimer.h>
#include <asm/atomic.h>

#include <linux/delay.h>
#include "core.h"
#include "device.h"
#include "hash.h"

/* Thresholds */
#define INVALID_THRESHOLD 20
#define FLUSH_THRESHOLD 100

/* Debug */
#define DEBUG_P6 0
#define JACK_DBG_LOG 1
#define JACK_DBG_LOG2 0
#if JACK_DBG_LOG
#define JDBG(...) printk(__VA_ARGS__) 
#else
#define JDBG(...) ;
#endif
#if JACK_DBG_LOG2
#define JDBG2(...) printk(__VA_ARGS__) 
#else
#define JDBG2(...) ;
#endif

//Flush Interrupt Delay Check Interval
unsigned long delay = 1000L*1e6L; //1 second
//GC Delay check interval
unsigned long w_delay = 100L*1e6L; //10 mili-sec
#define INVALID_THRESHOLD2 50
#define FLUSH_THRESHOLD2 1000

/* other features */
#define MERGE 1
#define RESERVED_PG_CNT 1

//Metadata Header Magic Number
#define META_HDR_BASE  "9487940"
//#define META_HDR_BASE  "9487940\0"
#define PRINT_PREF KERN_INFO "[LKP_KV]: "
#define NAND_DATA 123
#define NAND_META_DATA 456

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
int flush_metadata(bool force);
void gc(void);
void print_hash(void);
int meta_on_disk_format(void);
int is_read_only(void);

/* Timer Interrupt Prototypes & Globals */
static int init_flush_timer(void);
static void clear_flush_timer(void);
static enum hrtimer_restart flush_timer_callback( struct hrtimer *f_timer);
static struct hrtimer f_timer;

static int init_wear_timer(void);
static void clear_wear_timer(void);
static enum hrtimer_restart wear_timer_callback( struct hrtimer *w_timer);
static struct hrtimer w_timer;

/* locks & atomic variables */
atomic_t is_gb;
atomic_t is_flush;
spinlock_t one_lock;
spinlock_t list_lock;
spinlock_t erase_lock;

/* Global Config Variables */
lkp_kv_cfg config;
lkp_meta_cfg meta_config;

bucket* hashtable;
int HASH_SIZE;

//Specify ordering of metadata blocks
#define MAX_META_BLK 100
int meta_blkordr[MAX_META_BLK];

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
	int i, s_meta_blkordr;
	printk(PRINT_PREF "Loading... \n");
	
    spin_lock_init(&one_lock);
	spin_lock_init(&list_lock);
	spin_lock_init(&erase_lock);

    /*Initialize array for holding metadata block index's */
	s_meta_blkordr = sizeof(meta_blkordr)/sizeof(int);
	printk(KERN_INFO "Size meta_blk_ordering:%d (should b 1024)\n",s_meta_blkordr);
	for(i = 0 ; i < s_meta_blkordr ; ++i){
		meta_blkordr[i] = -1;
	}

	if (init_config(MTD_INDEX, META_INDEX) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -2;
	}

	is_gb.counter = 0;
	is_flush.counter = 0;
	meta_config.recent_update.counter = 0;
	
    // Initialize Periodic flushing of RAM metadata to disk //
    if( init_flush_timer() != 0){
		printk(KERN_ERR "Metadata flush-to-disk interrupt creation Error\n");
		return -3;
	}

	// Initialize Periodic Wear Leveling shuffling interrupt //
	if( init_wear_timer() != 0){
		printk(KERN_ERR "Wear leveling interrupt creation Error\n");
		return -4;
	}
    printk("----- Program start!!!!!!!!!!!!! (Don't put printk below if u r geting perf data) -----\n");
    return 0;
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	/* TODO */

	printk(PRINT_PREF "Exiting ... \n");
    JDBG("\n\n\n\n\n\n\n\n\n\n\n");
	
    //Disable RAM-2-disk timer interrupt
	clear_flush_timer();
	//Disable wear leveling timer interrupt
	clear_wear_timer();
	//Device Drive exit Virtual Device
	
	//Flush metadata to disk one last time before exit
    if (flush_metadata(true) == 0)
		printk(PRINT_PREF "Flush success ... \n");
	else
		printk(PRINT_PREF "Flush failed ... \n");

    device_exit();
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
 */
int init_config(int mtd_index, int meta_index)
{
	uint64_t tmp_blk_num;
    int blk_info_roundup;
    int hdr_per_blk = 1;
    int jack_size, i;
	
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

	//Disk Config
	config.format_done = 0;
	config.read_only = 0;
	config.mtd_index = mtd_index;

	//Metadata Config
	meta_config.format_done = 0;
	meta_config.read_only = 0;
	meta_config.mtd_index = meta_index;

	/* The flash partition is manipulated by calling the driver, through the
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
	config.nb_blocks = (int)tmp_blk_num; //Defined by flash simulator

	tmp_blk_num = meta_config.mtd->size;
	do_div(tmp_blk_num, (uint64_t) meta_config.mtd->erasesize);
	meta_config.nb_blocks = (int)tmp_blk_num; //Defined by flash simulator

	/* Semaphore initialized to 1 (available) */
	sema_init(&config.format_lock, 1);
	sema_init(&meta_config.format_lock, 1);

	//Allocates a chunk of memory according to size of disk config
    blk_info_roundup =(( sizeof(blk_info)*config.nb_blocks /config.page_size)+1) * config.page_size;
    printk("blk_info original size %lu rounded-up size %d %d pgs\n", 
            sizeof(blk_info)*config.nb_blocks,
            blk_info_roundup, blk_info_roundup/config.page_size);

	meta_config.blocks = kzalloc(blk_info_roundup, GFP_KERNEL);
    if(!meta_config.blocks)
        BUG();
	config.blocks = meta_config.blocks;
    
    HASH_SIZE = (config.pages_per_block - hdr_per_blk)*config.nb_blocks;
    printk("HASH_SIZE = max_buckets %d \n", HASH_SIZE);
    
    jack_size =(((sizeof(bucket) * HASH_SIZE)/config.page_size)+1) * config.page_size;
    printk("hash original size lu rounded-up size %lu %d pgs %d\n", 
            (sizeof(bucket) * HASH_SIZE),
            jack_size, jack_size/config.page_size);

    hashtable = kzalloc(jack_size, GFP_KERNEL);
    if(!hashtable)
        BUG();
    for(i=0; i<HASH_SIZE; i++) {
        hashtable[i].index = -1;
        hashtable[i].p_state = PG_FREE;
        hashtable[i].key[0] = '\0';
    }

	meta_config.hashtable_size =  jack_size;
	meta_config.block_info_size = blk_info_roundup;
	meta_config.metadata_size = meta_config.hashtable_size + meta_config.block_info_size;

	/* Flash scan for metadata creation: which flash blocks and pages are 
	 * free/occupied */
	if (init_scan() != 0) {
		printk(PRINT_PREF "init_scan() error\n");
		return -1;
	}

	printk(KERN_INFO "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
	print_config();
	printk(KERN_INFO "************************************************************\n");
	print_meta_config();
	printk(KERN_INFO "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");

	return 0;
}

/**
 * Launch time metadata creation: flash is scanned to determine which flash 
 * blocs and pages are free/occupied. 
 *
 * Return
 *  0: OK
 * -1: Error
 */
int init_scan()
{
	char *buf, *tmp;
	unsigned long eflags;
    int nb_meta_pages, nb_meta_blocks;
	int blk_pgs, hs_pgs;
    int i, jack_ofs = 0;
    int head = 1, total_ram_pg_cnt = 0;
    
    spin_lock_irqsave(&erase_lock, eflags);
    JDBG("\n\n\n\n\n\n\n\n\n\n\n");

	//Set more metadata sizes
	nb_meta_pages = (meta_config.metadata_size / meta_config.page_size);
    nb_meta_blocks = (nb_meta_pages / config.pages_per_block) + 1;

    for(i=0;i<10;i++) {
        JDBG("%s(): meta_total_size %d  meta_block_info_size %d meta_hashtable_size %d\n",
                __func__, meta_config.metadata_size,
                meta_config.block_info_size, meta_config.hashtable_size);
    }

	//buf = vmalloc(meta_config.page_size);
	buf = kzalloc(meta_config.page_size, GFP_KERNEL);
    if(!buf)
        BUG();

    meta_config.metadata_size = meta_config.hashtable_size + meta_config.block_info_size;
	blk_pgs = (meta_config.block_info_size / meta_config.page_size);
    hs_pgs = (meta_config.hashtable_size / meta_config.page_size);
    JDBG("blk_pgs %d hs_pgs %d\n", blk_pgs, hs_pgs);

    for (i = 0; i < config.nb_blocks; i++) {
        int j, idx_num = -1;
        
        if ( read_page(i*config.pages_per_block, buf) !=0 ) {
            printk(KERN_ERR "%s(): read_page failed\n", __func__);
            BUG();
        } 

        if (memcmp(buf, &META_HDR_BASE, (size_t)strlen((char*)&META_HDR_BASE))) {
            //JDBG("%s(); block %d: DATA block\n", __func__, i);
            continue;
        }
        JDBG("\n\n%s(); block %d: MEDA_DATA block\n", __func__, i);
        tmp = kzalloc(sizeof(char)*100, GFP_KERNEL); // TODO: page_size
        JDBG("strlen buf %d strlen hdr %d\n", strlen(buf), (size_t)strlen((char*)&META_HDR_BASE));
        for (j = 0; j < strlen(buf)-(size_t)strlen((char*)&META_HDR_BASE); j++) {
            *(tmp + j) = *(buf + (size_t)strlen((char*)&META_HDR_BASE) + j);
        }
        idx_num = simple_strtol(tmp, NULL, 10);
        JDBG2("%s(); idx_num %d\n", __func__, idx_num);
        kfree(tmp);
        meta_blkordr[idx_num] = i;
        
        for(j=1; j<config.pages_per_block; j++) {
            total_ram_pg_cnt++;
            if( total_ram_pg_cnt > hs_pgs + blk_pgs)
                goto construct_done;
            
            // from DISK
            JDBG2("%s(): read pg_idx(DISK) %d\n", __func__, (i*config.pages_per_block) + j);
            if ( read_page((i*config.pages_per_block) + j, buf) !=0 ) {
                printk(KERN_ERR "%s(): read_page failed\n", __func__);
                BUG(); //vfree(buf);
            }
            
            // to RAM// idx_num (0~3)
            if( (idx_num*config.pages_per_block) + (j - (head)) < blk_pgs ) { //blkinfo
                JDBG2("%s():\tBLK_INFO ofs %d (0~%d)\n", __func__, (idx_num*config.pages_per_block + (j - head)), blk_pgs-1);
                memcpy(((void*)meta_config.blocks) + ( (idx_num*config.pages_per_block + (j - head)) * meta_config.page_size),
                        buf, meta_config.page_size);
            } else { //hash
                JDBG2("%s():\tHASH ofs %d (0~%d) JJ\n", __func__, jack_ofs, hs_pgs-1);
                memcpy( ((void*)hashtable) + ( jack_ofs * meta_config.page_size), 
                                                    buf, meta_config.page_size);
                        jack_ofs++;
            }
        } // a blk done 
    } // all victm done
    // if ( total_ram_pg_cnt <= hs_pgs+ blk_pgs)
    //      ; // the very first time
construct_done:

	kfree(buf);

    //current victims
    for (i = 0 ; i < nb_meta_blocks+3 ; ++i) {
        JDBG("current victims: %d blk %d\n", i, meta_blkordr[i]);
    }
   
	//For all config blocks
	for (i = 0; i < config.nb_blocks; i++)
	{
		meta_config.blocks[i].list = (struct list_head *)vmalloc(sizeof(struct list_head));
		INIT_LIST_HEAD(meta_config.blocks[i].list);

		//If block is empty
		if(meta_config.blocks[i].state == 0xFFFFFFFF) // very first time
		{
			meta_config.blocks[i].state = BLK_FREE;
			meta_config.blocks[i].worn = 0;
			meta_config.blocks[i].nb_invalid = 0;
			meta_config.blocks[i].current_page_offset = 0;
		}
		
		//TODO What if block is full from last time and not formated yet?
	}

	//For all hashtable entries
	for (i = 0; i < HASH_SIZE; i++)
	{
		//if hashtable entry is valid add to that block's LL
		if (hashtable[i].p_state == PG_VALID)
			list_add(&hashtable[i].p_list, meta_config.blocks[hashtable[i].index / config.pages_per_block].list); 
	}

    // TODO: Jack list_lock
    
	if (is_read_only())
	{
		//No healthy blocks left!
		printk(PRINT_PREF "read only mode\n");

		//Switch Disk to READ-ONLY
		config.read_only = 1;
		meta_config.read_only = 1;
	}
 
    spin_unlock_irqrestore(&erase_lock, eflags);
	return 0;
}



/* flush_metadata
 * Flushes Metadata partition from RAM-to-disk
 *
 * Return
 * 0  : Success
 * -2 : meta_on_disk_format failed
 * -1 : write_meta_page failed
 */
int flush_metadata(bool force)
{
	char *buffer;
    static int cnt = 0;
	int nb_pages, nb_blocks, buffer_size;
	int enough_blocks = 0, i, ret = 0;
	int blk_pgs, hs_pgs, jack_ofs = 0, head = 1;
	unsigned long lflags;
    int meta_blkordr_pre[MAX_META_BLK];
	//unsigned long lflags, eflags;
	
    if(force)
        goto force_flush;
    if(!atomic_cmpxchg(&is_flush, 0, 1)) // succ new 1, fail old 0
        goto flush_meta_exit2;
    if (cnt++ < FLUSH_THRESHOLD)
        goto  flush_meta_exit2;
    cnt = 0;
    
force_flush:
    if(!spin_trylock(&erase_lock)) {
        if(force)
            goto force_flush;
        else
            goto flush_meta_exit2;
    }
    
    JDBG("%s(): FLUSH: FLUSH: FLUSH: FLUSH: FLUSH\n", __func__);
    JDBG("%s(): FLUSH: FLUSH: FLUSH: FLUSH: FLUSH\n", __func__);
    JDBG("%s(): FLUSH: FLUSH: FLUSH: FLUSH: FLUSH\n", __func__);
    JDBG("%s(): FLUSH: FLUSH: FLUSH: FLUSH: FLUSH\n", __func__);
    JDBG("%s(): FLUSH: FLUSH: FLUSH: FLUSH: FLUSH\n", __func__);

	//Can be more than 64
	//TODO need to remove all instances of meta_config (merge into config)
    nb_pages = (meta_config.metadata_size / meta_config.page_size);
	nb_blocks = (nb_pages / config.pages_per_block) + 1;

#if DEBUG_P6
    JDBG("nb_pages %d nb_blocks %d\n", nb_pages, nb_blocks);
    for (i = 0 ; i < nb_blocks+3 ; ++i) {
        JDBG("previous victims: %d blk %d\n", i, meta_blkordr[i]);
    }
#endif

    //Find If we have enough free blocks to update
    for(i = 0 ; i < nb_blocks ; ++i) {
        if( meta_blkordr[i] != -1 ) {
            ; //format_single(meta_blkordr[i]);
        }
        JDBG("cmp1: old of victims: %d blk %d\n", i, meta_blkordr[i]);
    }
   
    memcpy(meta_blkordr_pre, meta_blkordr, sizeof(int)*MAX_META_BLK);
    
    for(i=0; i<nb_blocks; i++) {
       meta_blkordr[i]=-2;
    }
    
	for(i = 0 ; i < config.nb_blocks ; ++i){
        int dd, is_used1=0;
        for(dd=0; dd<nb_blocks; dd++)
        //for(dd=0; dd<enough_blocks; dd++)
            if( i == meta_blkordr[dd])
                is_used1=1;
        if(is_used1==1)
            continue;
        is_used1=0;
        for(dd=0; dd<nb_blocks; dd++)
        //for(dd=0; dd<enough_blocks; dd++)
            if( i==meta_blkordr_pre[dd])
                is_used1=1;
        if(is_used1==1)
            continue;

        if( meta_config.blocks[i].current_page_offset == 0 && meta_config.blocks[i].state == BLK_FREE) {
            int z, winner = -1;
            for(z=i+1; z < config.nb_blocks; z++) {// look til to end, find the least number // wear leveling
                int ee, is_used2;
                if( meta_config.blocks[z].current_page_offset != 0 || meta_config.blocks[z].state != BLK_FREE)
                    continue;
                is_used2=0;
                for(ee=0; ee<nb_blocks; ee++)
                //for(ee=0; ee<enough_blocks; ee++)
                    if( z == meta_blkordr[ee])
                        is_used2=1;
                if(is_used2==1)
                    continue;
                is_used2=0;
                for(ee=0; ee<nb_blocks; ee++)
                //for(ee=0; ee<enough_blocks; ee++)
                    if(z == meta_blkordr_pre[ee])
                        is_used2=1;
                if(is_used2==1)
                    continue;
                
                if( winner == -1)
                    winner = z;
                else if (meta_config.blocks[z].worn < meta_config.blocks[winner].worn)
                    winner = z;
            }
            if(winner == -1) // not found a good pg after i
                winner = i;
            meta_config.blocks[winner].state = BLK_USED;
		    JDBG("flush previous META victim %d, new META victim is %d\n", meta_blkordr_pre[enough_blocks], i);
			if(meta_blkordr_pre[enough_blocks] != -1)
                format_single(meta_blkordr_pre[enough_blocks]);
			meta_blkordr[enough_blocks] = winner;
			++enough_blocks;
            if(enough_blocks >= nb_blocks) 
                break;
		}
	}
	JDBG("enough_blocks %d >=? nb_blocks %d break\n", enough_blocks, nb_blocks);
	
    for (i = 0 ; i < nb_blocks ; ++i) {
        JDBG("cmp2: parts of victims: %d blk %d\n", i, meta_blkordr[i]);
    }
    
	while(enough_blocks < nb_blocks){
        JDBG("enough_blocks %d nb_blocks %d\n", enough_blocks, nb_blocks);
		if(meta_blkordr_pre[enough_blocks] != -1) { // - recycle - //
            JDBG("%s(): flush meta_blkordr[%d] META blk %d\n",
                    __func__, enough_blocks, meta_blkordr_pre[enough_blocks]);
			format_single(meta_blkordr_pre[enough_blocks]);
            meta_config.blocks[meta_blkordr_pre[enough_blocks]].state = BLK_USED;
			++enough_blocks;
		}
		else {
		    printk(KERN_ERR "%s(): WRONG CALCULATION\n", __func__);
			BUG();
        }
	}

	//Have enough blocks now, keep going
	buffer_size = meta_config.page_size;
	buffer = kzalloc(buffer_size, GFP_ATOMIC);
	if(!buffer) {
		printk(KERN_ERR "kmalloc failed\n");
		BUG();
	}

    // current victims
    spin_lock_irqsave(&list_lock, lflags);
    for (i = 0 ; i < nb_blocks ; ++i) {
        meta_config.blocks[meta_blkordr[i]].state = BLK_USED;
        JDBG("current victims: %d blk %d\n", i, meta_blkordr[i]);
    }
    spin_unlock_irqrestore(&list_lock, lflags);

#if DEBUG_P6
	JDBG("#blk meta pages %d\n", (meta_config.block_info_size/meta_config.page_size)+1);
	JDBG("#hash meta pages %d\n", (meta_config.hashtable_size/meta_config.page_size)+1);
#endif

    /* flush to DISK*/
    meta_config.metadata_size = meta_config.hashtable_size + meta_config.block_info_size;
	blk_pgs = (meta_config.block_info_size / meta_config.page_size);
    hs_pgs = (meta_config.hashtable_size / meta_config.page_size);
    JDBG2("blk_pgs %d hs_pgs %d\n", blk_pgs, hs_pgs);

	for (i = 0; i < nb_pages; i++) {
        JDBG2("%s(): Global_RAM pg_num (i) %d/%d (0~max) jack_ofs %d\n", __func__, i, nb_pages-1, jack_ofs);
    
        // Read from RAM and compose data
        if( i <= ((meta_config.block_info_size/meta_config.page_size)-1) ) { // only 0
            memcpy( buffer,
                    ((void*)meta_config.blocks) + (i*meta_config.page_size),
                    meta_config.page_size);
#if 0
            if(meta_config.block_info_size!= meta_config.page_size) BUG(); //sef-test
#endif       
            JDBG2("%s(); JJ BLK_INFO (i) %d/%d (0~max)\n", __func__,  i, blk_pgs-1);
            
        } else { // hash
            memcpy(buffer, 
                        ((void*)hashtable) + ((i-blk_pgs) * meta_config.page_size), 
                        meta_config.page_size);
            JDBG2("%s(): JJ HASH (i-blk_pgs) %d/%d (0~max)\n", __func__, i-blk_pgs, hs_pgs-1);
        } 
        
        if( (i%(config.pages_per_block-1)) == 0) { // i=0, i=63, i=63*n
		    JDBG2("\n\n%s(): ||| write_hdr(META) |||  ### pg_idx %d #### blk %d (\% 64 == 0)\n", __func__, 
                            (meta_blkordr[jack_ofs]*config.pages_per_block), meta_blkordr[jack_ofs]);
            ret = write_hdr(meta_blkordr[jack_ofs]*config.pages_per_block, NAND_META_DATA, jack_ofs);
			jack_ofs++;
		}
        JDBG2("%s(): write_data(META) ### pg_idx %d ### = disk_base_ofs %d + ram %d + head %d\n\n", __func__,
                    (meta_blkordr[jack_ofs-1] * config.pages_per_block) + (i%(config.pages_per_block-1)) + (head),
                    (meta_blkordr[jack_ofs-1] * config.pages_per_block) , (i%(config.pages_per_block-1)) , (head));
        // head: manually offset
        if(write_page((meta_blkordr[jack_ofs-1]*config.pages_per_block) 
                                    + (i%(config.pages_per_block-1)) + (head),
                                                                buffer ) != 0){
            printk(KERN_ERR "%s(): ERR ERR ERR\n", __func__);
            BUG(); //ret = -1;
		}
	}
    
	kfree(buffer);
	//spin_unlock_irqrestore(&erase_lock, eflags);
	spin_unlock(&erase_lock);
flush_meta_exit2:
    atomic_set(&meta_config.recent_update, 0);
    atomic_set(&is_flush, 0);
	return ret;
}

/**
 * Freeing structures on exit of module
 */
void destroy_config(void)
{
	//Free all meta_config blocks & hashtable
	kfree(meta_config.blocks);
    kfree(hashtable);

	//Unlock config
	put_mtd_device(config.mtd);
	//Unlock meta_config
	put_mtd_device(meta_config.mtd);
}

/* invalid_pg( int hashtable_index)
 * Takes a hashtable index and updates page data at index to be INVALIDATED
 *
 * Return
 * VOID
 */
void invalid_pg(int hash_idx)
{
    int pg_idx;
    
    hashtable[hash_idx].p_state = PG_FREE;
    pg_idx = hashtable[hash_idx].index;
	JDBG("%s(): hash_idx %d pg_idx %d\n", __func__, hash_idx, pg_idx);
    //meta_config.blocks[pg_idx/config.pages_per_block].nb_invalid++;
    config.blocks[pg_idx/config.pages_per_block].nb_invalid++;
	/* delete the page metadata from the valid-page-list */
    list_del(&hashtable[hash_idx].p_list);
    
	JDBG("invalidated a page in blk %d\n", pg_idx/config.pages_per_block);
}

/* gc_check( void)
 * If too many invalid pages, don't wait until timmer interrupt handler
 *
 * Return
 * VOID
 */
void gc_check(void)
{
    int i;
    static int write_cnt = 0;
    for (i = 0; i < config.nb_blocks; i++) {
        if(meta_config.blocks[i].nb_invalid >= INVALID_THRESHOLD2) {
            gc();
            //print_hash();
            break;
        }
	}
    if(write_cnt++ >= FLUSH_THRESHOLD2) {
        write_cnt=0;
        flush_metadata(true);
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
	unsigned long eflags, lflags;
	int target_block, key_len, val_len, ret, ret2, index, hash_idx;
	spin_lock_irqsave(&erase_lock, eflags);

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
		ret = -3;
		goto set_exit;
	}
	
	/* the buffer that we are going to write on flash */
	buffer = (char *)kzalloc(config.page_size * sizeof(char), GFP_ATOMIC);
	if(!buffer) {
		printk(KERN_ERR "kmalloc failed\n");
		BUG();
	}
	
	/* if the key already exists: Invalidate curr page, & write new one!! */
    spin_lock_irqsave(&list_lock, lflags);
	hash_idx = hash_search(hashtable, key);
	if (hash_idx >= 0) {
		JDBG(PRINT_PREF "Key \"%s\" already exists in page %d. Replacing it\n", key, hashtable[hash_idx].index);
		invalid_pg(hash_idx);
	}
    spin_unlock_irqrestore(&list_lock, lflags);

	/* key size ... */
	memcpy(buffer, &key_len, sizeof(int));
	/* ... value size ... */
	memcpy(buffer + sizeof(int), &val_len, sizeof(int));
	/* ... the key itself ... */
	memcpy(buffer + 2 * sizeof(int), key, key_len);
	/* ... then the value itself. */
	memcpy(buffer + 2 * sizeof(int) + key_len, val, val_len);

	target_block = get_next_block_to_write();

    if(target_block == -1)
        return -3;
	//Get Index of page we are writing to
	index = target_block * config.pages_per_block + meta_config.blocks[target_block].current_page_offset;

	/* actual write on flash */
	ret = write_page(index, buffer);
    
	//Add Key value to RAM hashtable
	spin_lock_irqsave(&list_lock, lflags);
	ret2 = hash_add(hashtable, key, index);
	/* add page metadata to the valid-page-list */
	list_add(&hashtable[ret2].p_list, meta_config.blocks[target_block].list); 
	spin_unlock_irqrestore(&list_lock, lflags);
	kfree(buffer);

	spin_unlock_irqrestore(&erase_lock, eflags);
	
	/* Metadata Update Flag */
    atomic_set(&meta_config.recent_update, 1);

	gc_check();
	if (ret == -1)		/* read-only */
		return -3;
	else if (ret == -2)	/* write error */
		return -4;

	if (ret2 < 0)
		return -5; /* hash_add error */

	return 0;
set_exit:
    spin_unlock_irqrestore(&erase_lock, eflags);
    return ret;
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
	unsigned long eflags, lflags;
	int page_index = -1;
	int hash_index, ret = -3;
   
    buffer = (char *)kmalloc(config.page_size * sizeof(char), GFP_KERNEL);
	if(!buffer) {
		printk(KERN_ERR "kmalloc failed\n");
		BUG();
	}
	
    spin_lock_irqsave(&erase_lock, eflags);
    
    spin_lock_irqsave(&list_lock, lflags);
	hash_index = hash_search(hashtable, key);
	if (hash_index >= 0)
	{		
		page_index = hashtable[hash_index].index; 
		if(meta_config.blocks[page_index / config.pages_per_block].state == BLK_USED)
		{
			if (hashtable[hash_index].p_state == PG_FREE)
			{
				ret = -1;
                printk(KERN_ERR "%s(): ERR ERR ERR\n", __func__);
                goto get_exit;
			}

			if (read_page(page_index, buffer) != 0) 
			{
				ret = -2;
                printk("pg idx %d blk %d\n", page_index, page_index/config.pages_per_block);
                BUG();
                goto get_exit;
			}

			memcpy(&key_len, buffer, sizeof(int));
			memcpy(&val_len, buffer + sizeof(int), sizeof(int));

			cur_key = buffer + 2 * sizeof(int);
			cur_val = buffer + 2 * sizeof(int) + key_len;
			if (!strncmp(cur_key, key, strlen(key)))
			{
				memcpy(val, cur_val, val_len);
				val[val_len] = '\0';
				ret = page_index;
                goto get_exit;
			}
		}
	} else {
        JDBG("JACK: key %s not found\n", key);
        ret = -1; // **** the reason is... check kvlib.c in userspace
    }



get_exit:
	spin_unlock_irqrestore(&list_lock, lflags);
    spin_unlock_irqrestore(&erase_lock, eflags); 
	/* key not found */
	kfree(buffer);
	return ret;
}

/* del_key( const char *key)
 * Searches hashtable for given key, deletes and returns index of key deleted
 *
 * Return
 * -1: Key not found in hashtable OR deleting empty Key
 * 0 < x < MAX_HASH_INDEX: returns hashtable index of key that was deleted
 */
int del_key(const char *key)
{
	int hash_index, page_index = -1, ret;
	unsigned long eflags, lflags;

    spin_lock_irqsave(&erase_lock, eflags);

	spin_lock_irqsave(&list_lock, lflags);
	hash_index = hash_search(hashtable, key);
	if (hash_index >= 0) {
		page_index = hashtable[hash_index].index;
		if(meta_config.blocks[page_index/config.pages_per_block].state == BLK_USED) {
			if (hashtable[hash_index].p_state == PG_FREE) {
				//If we reach here, that means we ran invalid_pg on it
				//but still hasnt been garbage collected
				//GC is nondeterministic so this is safety

				printk("tryed to delete a PG_FREE key\n");
				ret = -1;
				goto out;
			}
			invalid_pg(hash_index);
			//printk("deleting key \"%s\"\n", key);
			ret = page_index;
			goto out;
		}
	}
	/* key not found */
	ret = -1;
	JDBG("%s(): key not found\n", __func__);

out:
	spin_unlock_irqrestore(&list_lock, lflags);
	spin_unlock_irqrestore(&erase_lock, eflags);
	return ret;
}

/* is_read_only( void)
 * Iterates through DISK and checks if all blocks and pages therein are Valid
 * and NOT_FREE
 *
 * Return
 * 1: IS READ-ONLY, All blocks used
 * 0: IS NOT READ-ONLY, A block is free
 * TODO WHAT IF offset is at end BUT BLOCK has some free pages but 
 * hasn't been merged yet?? This function will need to be updated...
 */
int is_read_only()
{
	int ret = 0;
	int i;
    int nb_pages = (meta_config.metadata_size / meta_config.page_size);
	int nb_blocks = (nb_pages / config.pages_per_block) + 1;

	for (i = 0; i < config.nb_blocks; i++)
	{
		if (meta_config.blocks[i].current_page_offset >= config.pages_per_block)
			ret++;
	}

	if (ret >= (config.nb_blocks-(nb_blocks-1)))
		return 1;
	else
		return 0;
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
int write_hdr(int pg_idx, int data, int meta_blk_num)
{
	int ret;
	char *buf, *tmp;
    
	if (meta_config.read_only) {
		return -1;
	}

	buf = kzalloc(meta_config.page_size, GFP_ATOMIC);
	if(!buf)
		BUG();
    
    if(data == NAND_DATA) {
#if DEBUG_P6
        JDBG("DATA: strlen(META_HDR_BASE) %lu\n", strlen(META_HDR_BASE));
        JDBG("DATA: META_HDR_BASE %s\n", META_HDR_BASE);
        JDBG("DATA: strlen(&META_HDR_BASE) %lu\n", strlen(&META_HDR_BASE));
        JDBG("DATA: &META_HDR_BASE %s\n", &META_HDR_BASE);
        JDBG("DATA: strlen((char*)&META_HDR_BASE) %lu\n", strlen((char*)&META_HDR_BASE));
        JDBG("DATA: (char*)&META_HDR_BASE) %s\n", (char*)&META_HDR_BASE);
#endif
		JDBG("%s(): (DATA): DATA\n", __func__);
        ret = write_page(pg_idx, buf);
    }
    else if(data == NAND_META_DATA) {
        tmp = kzalloc(sizeof(char)*10, GFP_ATOMIC);
        memcpy(buf, &META_HDR_BASE, (size_t)strlen((char*)&META_HDR_BASE));
#if DEBUG_P6
        JDBG("META: strlen(META_HDR_BASE)\n", strlen(META_HDR_BASE));
        JDBG("META: strlen(&META_HDR_BASE)\n", strlen(&META_HDR_BASE));
        JDBG("META: strlen((char*)META_HDR_BASE)\n", strlen((char*)&META_HDR_BASE));
#endif
        snprintf(tmp, 10, "%d", meta_blk_num);
        strcat(buf, tmp);
		JDBG("%s(): (META): 2 pg_idx %d blk %d\n", __func__, pg_idx, pg_idx/config.pages_per_block);
        JDBG("%s(): (META): @@@@@ META hdr %s @@@@@\n",__func__, buf);
        ret = write_page(pg_idx, buf);
        kfree(tmp);
    }
    else {
        printk(KERN_ERR "WRONG!\n");
    }

//	spin_lock_irqsave(&list_lock, lflags);
	meta_config.blocks[pg_idx/config.pages_per_block].state = BLK_USED;
//	spin_unlock_irqrestore(&list_lock, lflags);

	kfree(buf);
    return ret;
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
int get_next_block_to_write() // TODO: change name to next_page
{
	int target_block, pg_idx;
    unsigned long lflags;
    
	//Returns block index
	spin_lock_irqsave(&list_lock, lflags);
	target_block = get_healthy_block();
	spin_unlock_irqrestore(&list_lock, lflags);

	if (target_block == -1)
		return -1;

	//Set block flag to used if not already set
	meta_config.blocks[target_block].state = BLK_USED;

	//If current_page_offset = 0, need to reserve to first page for Metadata flag
	if (meta_config.blocks[target_block].current_page_offset < RESERVED_PG_CNT) {
		pg_idx = (target_block*config.pages_per_block) + 
				meta_config.blocks[target_block].current_page_offset;
		//Specify flag that this block is data block
        JDBG("write_hdr(get_next_block_to_write) pg_idx %d blk %d (pg should \% 64 == 0)\n", 
                            pg_idx, pg_idx/config.pages_per_block);
		return write_hdr(pg_idx, NAND_DATA, 0);
	}
	
	return target_block;
}

/* get_healthy_block(void)
 * Iterates through disk blocks looking for healthy, free block to use.
 * Return
 * 0<x< config.nb_blocks: Index of healthy, free block to use
 * -1: No healthy, free blocks available, Disk is now in READ-ONLY Mode
 */
int get_healthy_block()
{
	int i, j;
	int ret = -1;
	int minimum = 0x7FFFFFFF;
    int nb_pages = (meta_config.metadata_size / meta_config.page_size) + 1;
	int nb_blocks = (nb_pages / config.pages_per_block) + 1;
	
    //For all blocks on disk
	for (i = 0; i < config.nb_blocks; i++) {
jack:		
        //If offset is not at the end of the block
        for (j = 0 ; j < nb_blocks ; ++j) {
            if(i == meta_blkordr[j]) {
                //JDBG("%s(): This is a blk %d in victim[%d]\n", __func__, i, j);
                i++;
                if (i >=config.nb_blocks) {
                    return ret;
                } else {
                    goto jack;
                }
            }
        }
        
        if (meta_config.blocks[i].current_page_offset < config.pages_per_block) {
			//If block worn threshhold is within limits
			if (meta_config.blocks[i].worn < minimum) {
				minimum = meta_config.blocks[i].worn;
				ret = i;
				//TODO shouldn't return be here??
			}
            //JDBG("%s(): blk %d is not full\n", __func__, i);
		}
        else { 
            //JDBG("%s(): blk %d is full\n", __func__, i);
        }
	}//END for
	
	/* If we get there, no free block left... */
	//if (minimum == config.pages_per_block - 1)
	//	return -1;

    /*
    JDBG("%s(): return %d\n", __func__, ret);
    JDBG(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                ret, config.blocks[ret].state,
                                config.blocks[ret].worn,
                                config.blocks[ret].nb_invalid,
                                config.blocks[ret].current_page_offset);
    */
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
	unsigned long lflags;
	struct erase_info ei;
	bucket *current_page, *next;

    if(idx < 0) {
        //JDBG(KERN_WARNING "FLUSH -1\n");
        return -1;
    }
	//Block Location & length
	ei.mtd = config.mtd;
	ei.len = ((uint64_t) config.block_size);
	ei.addr = idx * ((uint64_t) config.block_size);

	ei.callback = format_callback;
	config.format_done = 0;

	/* Call the MTD driver  */
	if (config.mtd->_erase(config.mtd, &ei) != 0)
		return -1;
	
	//Wait while _erase happens
	while (1) {
		//Get lock for formating disk
		if (!down_trylock(&config.format_lock)) {
			//TODO Not sure, but think we also need -1, according to format_callback
			//if (config.format_done || (config.format_done == -1)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
        	}
	}
	
	if (config.format_done == -1)
		return -1;

	//Reset target block metadata state info
	meta_config.blocks[idx].state = BLK_FREE;
	meta_config.blocks[idx].nb_invalid = 0;
	meta_config.blocks[idx].current_page_offset = 0;
	meta_config.blocks[idx].worn++;
	
    //Clear all valid pages associated from target block
	spin_lock_irqsave(&list_lock, lflags);
	list_for_each_entry_safe(current_page, next, meta_config.blocks[idx].list, p_list) {
		if(current_page) {
			list_del(&current_page->p_list);
		}
	}
    spin_unlock_irqrestore(&list_lock, lflags);

	JDBG(PRINT_PREF "Formating blk %d done\n", idx);

	// global meta data in nand
	//Reset that there is room on Disk for writing
	config.read_only = 0;       // nandflash-wide
	meta_config.read_only = 0;  // nandflash-wide

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
	unsigned long lflags;
	struct erase_info ei;
	bucket *current_page, *next;
    unsigned long eflags;
    int ret=0;

    spin_lock_irqsave(&erase_lock, eflags);
    JDBG("%s():\n\n\n\n\n", __func__);

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
        printk(KERN_ERR "%s(): _erase\n", __func__);
		ret = -1;
        BUG();
        goto format_exit;
    }

	/* on attend la fin effective de l'operation avec un spinlock. 
	 * C'est la fonction callback qui mettra format_done a 1 */
	/* TODO change to a condwait here */
	while (1)
		if (!down_trylock(&config.format_lock)) {
			//if (config.format_done || (config.format_done == -1)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
		}

	/* was there a driver issue related to the erase oepration? */
	if (config.format_done == -1) {
        printk(KERN_ERR "%s(): erase not done\n", __func__);
		ret = -1;
        BUG();
        goto format_exit;
    }

	config.read_only = 0;

    spin_lock_irqsave(&list_lock, lflags); //TODO
	/* format metadata in the memory */
	for (i = 0; i < config.nb_blocks; i++) {

        // remove valid list elements
		list_for_each_entry_safe(current_page, next, meta_config.blocks[i].list, p_list) {
			if(current_page) {
				list_del(&current_page->p_list);
			}
		}

		meta_config.blocks[i].state = BLK_FREE;
		meta_config.blocks[i].worn = 0;
		meta_config.blocks[i].nb_invalid = 0;
		meta_config.blocks[i].current_page_offset = 0;
	}
    spin_unlock_irqrestore(&list_lock, lflags); // TODO

	for (i = 0; i < HASH_SIZE; i++)
	{
		hashtable[i].p_state = PG_FREE;
		hashtable[i].dirty = 0;
	}
    
	meta_config.read_only = 0;

	JDBG(PRINT_PREF "Format done\n");

format_exit:
    spin_unlock_irqrestore(&erase_lock, eflags);
	return ret;
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
	unsigned long flags, lflags;

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
        BUG();
		goto exit;
	}
#if 0
    if( retlen != config.page_size) //self-check
        BUG();
#endif
//
	spin_lock_irqsave(&list_lock, lflags);
	meta_config.blocks[page_index/config.pages_per_block].current_page_offset++;
#if 0
    if( meta_config.blocks[page_index/config.pages_per_block].current_page_offset > config.pages_per_block) //self-check
        printk(KERN_ERR "ERROR: Over a single page offset in blk %d pg %d cnt %d!!!!!!!!!!!!!!!\n",
                            page_index/config.pages_per_block, page_index,
                            meta_config.blocks[page_index/config.pages_per_block].current_page_offset);
#endif
	spin_unlock_irqrestore(&list_lock, lflags);
//
exit:
	/* if the flash partition is full, switch to read-only mode */
	if (is_read_only())
	{
		printk(PRINT_PREF "no free block left... swtiching to read-only mode\n");
		config.read_only = 1;
		ret = -1;
		//goto exit;
	}

	spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}

/* write_meta_page( int page_index, const char *buf)
 * Write pages on metadata partition
 *
 * Return
 * 0: Success
 * -1: Failed to Write, Disk FULL
 * -2: Failed to Write, Error with Driver _write
 */
int write_meta_page(int page_index, const char *buf)
{
	int ret = 0;
	uint64_t addr;
	size_t retlen;
	//unsigned long flags;

	//spin_lock_irqsave(&one_lock, flags);

	/* if the flash partition is full, dont write */
	if (meta_config.read_only) {
		ret = -1;
		goto exit2;
	}

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) meta_config.page_size);

	/* call the NAND driver MTD to perform the write operation */
	if(meta_config.mtd->_write(meta_config.mtd, addr, meta_config.page_size, &retlen, buf)!= 0)
	{
		ret = -2;
		goto exit2;
	}

exit2:
	//spin_unlock_irqrestore(&one_lock, flags);
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
   
#if 0 // self-check
    if( retlen != config.page_size)
        BUG();
#endif	
    spin_unlock_irqrestore(&one_lock, flags);
	return ret;
}

/* read_meta_page( int page_index, char *buf)
 * Function takes in page_index and data is placed into buf
 *
 * Return
 * 0: Sucess
 * Anything else: Error
 */
int read_meta_page(int page_index, char *buf)
{
	int ret;
	uint64_t addr;
	size_t retlen;
	unsigned long flags;

	//Attempt to spin Lock
	spin_lock_irqsave(&one_lock, flags);
	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) meta_config.page_size);
	
	/* call the NAND driver MTD to perform the read operation */
	ret = meta_config.mtd->_read(meta_config.mtd, addr, meta_config.page_size, &retlen, buf);

	//Unlock	
	spin_unlock_irqrestore(&one_lock, flags);

	return ret; 
}

/*****************************************************************************/
/* Timer Interrupt Functions                                                 */
/*****************************************************************************/

/* init_flush_timer
 * Initializes hrtimer interrupt for flushing RAM-to-disk
 *
 * Return
 * 0: Success
 * 1: Timer was already active
 */
static int init_flush_timer(void){
	int ret;
	ktime_t ktime;
	//printk(KERN_INFO ">>[%s]: timer being setup\n",__func__);

	//ktime_set(seconds, nanoseconds)
	ktime = ktime_set(0, delay);
	hrtimer_init( &f_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	f_timer.function = &flush_timer_callback;
	printk(KERN_INFO ">> RAM FLUSH Timer start\n");
	printk(KERN_INFO ">> Delay is:%lums %llu\n", delay, get_jiffies_64());

	ret = hrtimer_start( &f_timer, ktime, HRTIMER_MODE_REL);
	return ret;
}

/* clear_flush_timer
 * Disables hrtimer interrupt for flushing RAM-to-disk
 * Return: VOID
 * But prints status to DMESG
 */
static void clear_flush_timer(void){
	int cancelled;
	printk(KERN_INFO ">>%s\n",__func__);

	cancelled = hrtimer_cancel( &f_timer);
	if(cancelled){
		printk(KERN_ERR ">>ERR: Timer still running !!!\n");
	}else{
		printk(KERN_INFO ">>FLUSH Timer cancelled\n");
	}
}

/* flush_timer_callback
 * Function registered as the callback to when the hrtimer interrupt expires
 * Return: 
 * Flag to reset timer interrupt HRTIMER_RESTART
 */
static enum hrtimer_restart flush_timer_callback( struct hrtimer *f_timer)
{
	//DO NOT TOUCH
	ktime_t currtime, interval;
	currtime = ktime_get();
	interval = ktime_set(0, delay);
	hrtimer_forward(f_timer, currtime, interval);
	//printk(KERN_INFO ">>%s\n",__func__);
	//Do work below...

	//check if new data has been written (flag)
	if(atomic_read(&meta_config.recent_update)){
		//Something new is in RAM (need to flush to Disk)
		//Initiate Flushing
		//printk(KERN_INFO ">>[%s] Need to flush!!\n",__func__);
        //atomic_set(&meta_config.recent_update, 0);
		flush_metadata(false);
		//printk(KERN_INFO ">>[%s] FLUSH Complete!!\n",__func__);

		//Clear recent_update flag
	}

	//Return Flag to restart
	return HRTIMER_RESTART;
}

/*********** Wear Leveling Timer *********************************************/

/* init_wear_timer
 * Initializes hrtimer interrupt for performing periodic Garbage collection
 * wear-leveling routine shuffling of data
 *
 * Return
 * 0: Success
 * 1: Timer already active
 */
static int init_wear_timer(void){
	int ret;
	ktime_t ktime;
	// JDBG(KERN_INFO ">>%s: timer being setup~\n",__func__);

	ktime = ktime_set(0, w_delay);
	hrtimer_init( &w_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	w_timer.function = &wear_timer_callback;
	printk(KERN_INFO ">>WEAR LEVELING Timer start\n");
	printk(KERN_INFO ">> In:%lums %llu\n", w_delay, get_jiffies_64());

	ret = hrtimer_start( &w_timer, ktime, HRTIMER_MODE_REL);
	return ret;
}

/* clear_wear_timer
 * Disables hrtimer interrupt for periodic GC and wear-leveling
 * Return: VOID
 * But prints status to DMESG
 */
static void clear_wear_timer(void)
{
	int cancelled;
	printk(KERN_INFO ">>%s\n",__func__);

	cancelled = hrtimer_cancel( &w_timer);
	if(cancelled){
		printk(KERN_ERR ">>ERR: Timer still running !!!\n");
	}else{
		printk(KERN_INFO ">>WEARLEVEL Timer cancelled\n");
	}
}

/* wear_timer_callback
 * Function registered as the callback to when the hrtimer interrupt expires
 *
 * Return: 
 * Flag to reset timer interrupt HRTIMER_RESTART
 */
static enum hrtimer_restart wear_timer_callback( struct hrtimer *w_timer){
	//DO NOT TOUCH
	ktime_t currtime, interval;
	currtime = ktime_get();
	interval = ktime_set(0, w_delay);
	hrtimer_forward(w_timer, currtime, interval);
	//JDBG(KERN_INFO ">>%s\n",__func__);
	//Do work below...

	//Need to call Wear leveling functions here to shuffle data to 
	// different blocks at each interval
	gc();
	
	//return flag to restart timer interrupt
	return HRTIMER_RESTART;	
}
///////////////////////////////////////////////////////////////////////////////
/*****************************************************************************/
/* Print some statistics on the kernel log                                   */
/*****************************************************************************/
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
///////////////////////////////////////////////////////////////////////////////
/*****************************************************************************/
/* Update Meta_data Blocks                                                   */
/*****************************************************************************/

/* update_meta_blocks( void)
 * Metadata is spread among multiple blocks, we need to:
 * update until full, erase & find new place for ALL metadata blocks,
 * and finally rewrite those blocks with metadata info
 *
 * Return
 * TODO
 *
 * TODO Need to add this function to set/delete operations
 */
void update_meta_blocks( void){

	//Need to lock RAM structures
	// Hash and meta_config

	//Find first metadata block index

	//FOR ALL metadata blocks
		//Check: IF !FULL metadata block
			//OK Add new metadata page
			//Update curr_page_offset
			//return
		//Else
			//try next block

	//No empty metadata blocks if we get here
	
	//Get number of victims needed to hold all metadata, update as needed
	//TODO should this be global int?

	//CHECK: if we have enough victime blocks
		//Good continue below***
	//else
		//TODO TRY MERGE
		//CHECK AGAIN: if we have enough victim blocks
			//Good: continue below ***
		//else
			//switch to read-only mode
			//return

	//***
	//get their index's

	//FOR ALL VICTIMS
		//format_single (since the blocks will not necessarily be contigueous)
		// write_hdr NAND_META_HDR mode (To make sure this block does not get turned
		// into data block potentially)

	//Make temp buffer

	//FOR ALL VICTIMS
		//memcpy all pages in first metadata block
		//map into block (via write_meta_page?)

		//If reached curr_page_offset == pages_per_block
			//Need to move on to next block
			//WHAT NOW FUCK to find next block??!?!?!?
			//I think we can just have this index's stored before we enter loop
			//In that case we are OK and dont need to worry 

}

///////////////////////////////////////////////////////////////////////////////

#if 0
void gc_merge(void)
{
    int merged_blk1 = -1, merged_blk2 = -1;

    // single victim
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
    // two victims (merge)
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
    // look for a victim blk (the least worn)
	for (i = 0; i < config.nb_blocks; i++) {
        if (BLK_FREE) {
            if (victim_blk == -1)
                victim_blk = i;
            else if (meta_config.blocks[i].worn < 
                        meta_config.blocks[victim_blk].worn)
                victim_blk = i;
        }
    }
    if(victim_blk == -1) { //no BLK_FREE, write it to itself
        printk(KERN_ERR "write should garauntee there will always have a free blk (victim_blk)\n");
        BUG();
    }
    
    printk("(merge) target_blk1 %d target_blk2 %d victim_blk %d \n", target_blk1, target_blk2, victim_blk);

    /* 1. read pages */
}
#endif 

/* gc( void)
 * TODO shouldn't this be called gctest? Garbage Collection....
 * Action: ? TODO
 *
 * Return
 * VOID
 */
void gc(void)
{
	unsigned long lflags;
//	unsigned long eflags, lflags;
	int pg_index, ret, ret2;
	int hash_index;
	int valid_cnt = 0, head = 1;
	int i, target_blk1 = -1, victim_blk = -1;
	int target_blk2 = -1; // target_blk2 == 
	int target_blk1_valid_cnt = -1;
	char *buffer;
	bucket *current_page, *next;
    int nb_pages = (meta_config.metadata_size / meta_config.page_size) + 1;
    int nb_blocks = (nb_pages / config.pages_per_block) + 1;
	int key_len[config.pages_per_block], val_len[config.pages_per_block];
	char *cur_key[config.pages_per_block], *cur_val[config.pages_per_block];
    if( config.pages_per_block <64) 
        return;

    if(!atomic_cmpxchg(&is_gb, 0, 1)) // succ new 1, fail old 0
        return; 
    if(!spin_trylock(&erase_lock))
        return;

	/* find target: Will be read from */
    // find one target > invalid_threshold
	for (i = 0; i < config.nb_blocks; i++) {
        int y, is_metablk2 = 0;
        for (y = 0 ; y < nb_blocks ; ++y)
            if(i == meta_blkordr[y])
                is_metablk2 = 1;
        if (is_metablk2==1)
            continue;
		if(meta_config.blocks[i].nb_invalid >= INVALID_THRESHOLD) {
			target_blk1 = i; // can be mered from
	        target_blk1_valid_cnt = config.blocks[i].current_page_offset - meta_config.blocks[i].nb_invalid - head;
			break;
		}
	}

	//No target found
	if(target_blk1 == -1) {
		//JDBG("No need to do GC\n");
		goto gcexit2;
	}
   
#if MERGE
    // TODO: the order would harm performance
    // find one < invalid_threshold
    for (i = 0; i < config.nb_blocks; i++) {
        int x, is_metablk = 0;
	    if(i == target_blk1)
            continue;
        
        for (x = 0 ; x < nb_blocks ; ++x)
            if(i == meta_blkordr[x])
                is_metablk = 1;
        if (is_metablk==1)
            continue;
        
        //printk("%s(): blk %d free pgs %d >? target_valid_pg_cnt %d\n", __func__, 
        //    i, config.pages_per_block - config.blocks[i].current_page_offset, target_blk1_valid_cnt);
		if( config.pages_per_block - config.blocks[i].current_page_offset
                                                    > target_blk1_valid_cnt) {
			if(target_blk2 == -1)
                target_blk2 = i;    // can be mered to
			else
                if( config.blocks[target_blk2].worn > config.blocks[i].worn) // wear leveling
                    target_blk2 = i;
		}
	}
#endif

    if(target_blk2 != -1) { // merge
        victim_blk = target_blk2;
    } else {
        for (i = 0; i < config.nb_blocks; i++) {
            if( meta_config.blocks[i].current_page_offset == 0 && meta_config.blocks[i].state == BLK_FREE) {
                if (victim_blk == -1)
                    victim_blk = i;
                else if (meta_config.blocks[i].worn < 
                        meta_config.blocks[victim_blk].worn)
                    victim_blk = i;
            }
        }
        
        //No Free Block, we can still write back to ourself
        if(victim_blk == -1) {
            victim_blk = target_blk1;
        }
    }
    meta_config.blocks[victim_blk].state = BLK_USED;

	JDBG2("GCing......(%s) FROM target_blk1 %d (valid_cnt) --TO--> victim_blk %d (%d spots) valid %d\n",
            target_blk2==-1?"ORIGINAL(ITSELF)":"MERGING", target_blk1, victim_blk, 
#if MERGE
            config.pages_per_block - config.blocks[victim_blk].current_page_offset - head,
#else
            -999,
#endif
            meta_config.blocks[target_blk1].current_page_offset - meta_config.blocks[target_blk1].nb_invalid - head);


    JDBG(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                    target_blk1, config.blocks[target_blk1].state,
                                    config.blocks[target_blk1].worn,
                                    config.blocks[target_blk1].nb_invalid,
                                    config.blocks[target_blk1].current_page_offset);
    JDBG(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d\n",
                                    victim_blk, config.blocks[victim_blk].state,
                                    config.blocks[victim_blk].worn,
                                    config.blocks[victim_blk].nb_invalid,
                                    config.blocks[victim_blk].current_page_offset);


    /* 1. read pages */
	buffer = kzalloc(config.page_size * sizeof(char) * config.pages_per_block, GFP_ATOMIC);
    if(!buffer) {
        printk(KERN_ERR "kmalloc failed\n");
        BUG();
    }
    
    /* walk valid_list, store all target_blk data */
    spin_lock_irqsave(&list_lock, lflags);
    list_for_each_entry_safe(current_page, next, meta_config.blocks[target_blk1].list, p_list)
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
        JDBG(PRINT_PREF "iterating pg_idx %d (blk %d)\n", 
                    current_page->index, current_page->index/config.pages_per_block);

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

        /* invalide in hashtable */
        true_key = kmalloc(key_len[valid_cnt]+1, GFP_ATOMIC);
        if(!true_key) {
            printk(KERN_ERR "vallocation failed\n");
            BUG();
        }
        strncpy(true_key, cur_key[valid_cnt], key_len[valid_cnt]);
        *(true_key+key_len[valid_cnt]) = '\0';
        //for(j=0; j<key_len[valid_cnt] ;j++) {
        //    JDBG("JACK: *true_key+%d \"%c\"\n", j, *(true_key+j));
        //}
        JDBG("(GB R) true len %lu, %s\n", strlen(true_key), true_key);
        hash_index = hash_search(hashtable, true_key);

        JDBG("(GB R) hash_idx %d\n", hash_index);
        invalid_pg(hash_index); 

        kfree(true_key);

        /* record */
        valid_cnt++;
        JDBG("\n");
    }
    spin_unlock_irqrestore(&list_lock, lflags); 

#if DEBUG_P6
    /* double check */
    /*
    if ( meta_config.blocks[target_blk1].current_page_offset-valid_cnt == meta_config.blocks[target_blk1].nb_invalid) {
        JDBG("GOOD cnt - valid_cnt %d invalided %d\n",
                        valid_cnt, meta_config.blocks[target_blk1].nb_invalid);
    } else {
        JDBG("BAD cnt - valid_cnt %d cur_ofs %d invalided %d\n",
                                    valid_cnt,
                                    meta_config.blocks[target_blk1].current_page_offset,
                                    meta_config.blocks[target_blk1].nb_invalid);
    }
    */
#endif

    /* 2. erase victim_blk */
    //if(target_blk2 == -1) // !merge
    //    format_single(victim_blk);
    //else
        format_single(target_blk1);
    pg_index = (victim_blk * config.pages_per_block)
                        + meta_config.blocks[victim_blk].current_page_offset;
	
    JDBG("write_hdr(GC DATA) pg_idx %d blk %d (pg should \% 64 == 0)\n", 
                            pg_index, pg_index/config.pages_per_block);
    if(pg_index % config.pages_per_block == 0) {
        JDBG("(ORIGINAL(ITSELF)) must be here\n");
        write_hdr(pg_index, NAND_DATA, 0);
    }
    JDBG("valid_cnt: %d\n", valid_cnt);

    /* 3. write */
    for (i=0; i<valid_cnt; i++) {
        char* shown_key;
        
        memcpy(buffer+(i*config.pages_per_block), &key_len[i], sizeof(int));
        memcpy(buffer+(i*config.pages_per_block) + sizeof(int), &val_len[i], sizeof(int));
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int), cur_key[i], key_len[i]);
        strncpy(buffer+(i*config.pages_per_block) + 2 * sizeof(int) + key_len[i], cur_val[i], val_len[i]);
        JDBG("(W) len: %d %d\n", key_len[i], val_len[i]);
        JDBG("(W) data: %s \n", cur_key[i]);
        JDBG("(Wbuf) len: %d %d\n", *(buffer+(i*config.pages_per_block)), *(buffer+(i*config.pages_per_block) + sizeof(int)));
        JDBG("(Wbuf) data: %s \n", (buffer+(i*config.pages_per_block) + 2 * sizeof(int)));
	    pg_index = (victim_blk * config.pages_per_block)
                            + meta_config.blocks[victim_blk].current_page_offset;
        
        shown_key = kmalloc(key_len[i]+1, GFP_ATOMIC);
        if(!shown_key) {
            printk(KERN_ERR "vallocation failed\n");
            BUG();
        }
        strncpy(shown_key, cur_key[i], key_len[i]);
        *(shown_key+key_len[i]) = '\0';
        JDBG("(SHOWN KEY): keylen %d key %s\n", key_len[i], shown_key);
#if DEBUG_P6
        int j;
        for(j=0; j<key_len[i] ;j++) {
            JDBG("JACK: *shown_key+%d \"%c\"\n", j, *(shown_key+j));
        }
#endif
        JDBG("JACK: shown %lu\n", strlen(shown_key));
        
        /* write to disk and update metadata (blk info) on RAM */
	    ret = write_page(pg_index, buffer+(i*config.pages_per_block));  // update .current_page_offset
        spin_lock_irqsave(&list_lock, lflags);
        ret2 = hash_add(hashtable, shown_key, pg_index);                // PG_VALID
        if (ret<0 || ret2<0) {
            printk("%s: failed to write back to ram/disk\n", __func__);
            BUG();
        }
        list_add(&hashtable[ret2].p_list, meta_config.blocks[victim_blk].list);
        spin_unlock_irqrestore(&list_lock, lflags);
        
        JDBG("GB: wrote hash_idx %d pg_idx %d again\n", ret2, pg_index);

#if 0
        //slef-checking
        hash_index = hash_search(hashtable, shown_key);
        printk("self-checking (hash): hash_idx %d\n", hash_index);
        char* tmp_buf = kmalloc(config.page_size, GFP_ATOMIC);
        if (read_page(hashtable[hash_index].index, tmp_buf) != 0) {
            printk(KERN_ERR "%s(): read_page (checking) failed\n", __func__);
            kfree(buffer);
            BUG();
        }
        ret = memcpy(tmp_buf, buffer+(i*config.pages_per_block), config.page_size);
        printk("self-checking (disk): pg_idx %d %s\n", hashtable[hash_index].index, ret==0?"GOOD":"BAD");
        kfree(tmp_buf);
#endif
        kfree(shown_key);
        JDBG("\n");
    }
    
    /* update blk info */
    spin_lock_irqsave(&list_lock, lflags);
    meta_config.blocks[victim_blk].state = BLK_USED;
    spin_unlock_irqrestore(&list_lock, lflags);     

	kfree(buffer);

    JDBG("\n\n");
gcexit2:
    //spin_unlock_irqrestore(&erase_lock, eflags);
    spin_unlock(&erase_lock);
    atomic_set(&is_gb, 0);
    JDBG("\n\n");
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
    int j, is_victim;
    int nb_pages = (meta_config.metadata_size / meta_config.page_size) + 1;
    int nb_blocks = (nb_pages / config.pages_per_block) + 1;
    int valid_cnt = 0;
	//bucket *current_page;

	for (i = 0; i < HASH_SIZE; i++) {
		if(hashtable[i].p_state == PG_VALID) {
			printk(PRINT_PREF "%d: dirty: %d, p_state: %d, index: %d, key:%s in_pg:%lu \n", i, \
				hashtable[i].dirty, \
				hashtable[i].p_state, \
				hashtable[i].index, \
				hashtable[i].key, \
                ((&hashtable[i] - hashtable)*sizeof(bucket))/2048);
            valid_cnt++;
        }
#if 0 //self-test
        void* pre_ptr = NULL;
        if(pre_ptr) {
            if ( ((void*)&hashtable[i] - pre_ptr) != sizeof(bucket)) {
                BUG();
            }
        }
        pre_ptr = &hashtable[i];
#endif
	}
	
#if 1
	for (i=0, j=0; i < config.nb_blocks; i++) 
	{
        is_victim = 0;
        for (j = 0 ; j < nb_blocks ; ++j) {
            if(i == meta_blkordr[j]) {
                is_victim = 1;
                break; 
            }
        }
        printk(PRINT_PREF "%d: state: %d, worn: %d, nb_invalid: %d, current_page_offset: %d [%s]\n",
                                    i, config.blocks[i].state,
                                    config.blocks[i].worn,
                                    config.blocks[i].nb_invalid,
                                    config.blocks[i].current_page_offset,
                                    is_victim==1?"*":"");
        
#if 0
		printk(PRINT_PREF "block %d's valid pages: \n", i);
		list_for_each_entry(current_page, config.blocks[i].list, p_list)
		{
			if (current_page)
				printk(PRINT_PREF "%d\n", current_page->index);
		}
#endif
    }
#endif
    JDBG2("Valid key cnt: %d\n", valid_cnt);
    JDBG2("sizeof(bucket) %d\n", sizeof(bucket));
#if 0
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
