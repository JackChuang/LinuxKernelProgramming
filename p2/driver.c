/*
 * driver.c
 * Copyright (C) 2017 jackchuang <jackchuang@Jack-desktop>
 */
#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>   /* KERN_INFO */
#include <linux/init.h>     /* Init and exit macros */
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/random.h>
#include <linux/idr.h>

/* para from user */
static int dstruct_size = 10;
module_param(dstruct_size, int, 0);
MODULE_PARM_DESC(dstruct_size, "A sample integer kernel module parameter");

static int range; // searching range for RBtree
static int *g_rand_int_arry; 
static bool abs = false;

/* list */
struct car {
    int num;
    int price_in_dollars;
    struct list_head list;
};

/* queue */
struct car2
{
    int num;
    int price_in_dollars;
};

/* tree entry */
struct zswap_entry 
{
    struct rb_node rbnode;
    int offset; // node number
};

int LinkedList(void){
    int i;
    struct car *current_car, *next;
    LIST_HEAD(my_car_list); /* my_car_list is a struct list_head */
    printk(" ---------------- %s() ---------------- \n", __func__);
    
    for (i=0; i<dstruct_size; i++){
        struct car *my_car = kmalloc(sizeof(struct car), GFP_KERNEL);
        if (!my_car) { 
            pr_err("%s(): Allocation failed\n", __func__); 
            return -1; 
        } 
        my_car->num=i;
        get_random_bytes(&my_car->price_in_dollars, sizeof(int));
        if(abs)
            my_car->price_in_dollars = abs(my_car->price_in_dollars % dstruct_size);

        INIT_LIST_HEAD(&my_car->list); 

        list_add_tail(&my_car->list, &my_car_list);
    }

    if(list_empty(&my_car_list))
        return -1;
    list_for_each_entry_safe(current_car, next, &my_car_list, list)
    {
        printk(KERN_INFO "%s(): num %d rand_int: %d\n", __func__, current_car->num, current_car->price_in_dollars);
        list_del(&current_car->list);
        kfree(current_car); /* if this was dynamically allocated through kmalloc */
    }
    
    printk("%s(): [ released and checking... ]\n", __func__);
    printk("%s(): is_list_empty %d \n", __func__, list_empty(&my_car_list));
    printk("%s(): [ Done!! ]\n\n\n", __func__);
    return 0;
}

int LinkedList_1(struct list_head *my_car_list_head)
{
    int i;
    printk(" ---------------- %s() ---------------- \n", __func__);
    printk(" %s(): allocating... \n", __func__);
    
    for (i=0; i<dstruct_size; i++){ 
        struct car *my_car = kmalloc(sizeof(struct car), GFP_KERNEL);
        if (!my_car) { 
            pr_err("%s(): Allocation failed\n", __func__); 
            return -1; 
        } 
        my_car->num=i;
        get_random_bytes(&my_car->price_in_dollars, sizeof(int));
        if(abs) my_car->price_in_dollars = abs(my_car->price_in_dollars % dstruct_size);
        
        INIT_LIST_HEAD(&my_car->list); 

        list_add_tail(&my_car->list, my_car_list_head);
    }
    return 0;
}

int LinkedList_2(struct list_head *my_car_list_head)
{
    struct car *current_car, *next;
    printk(" ---------------- %s() ---------------- \n", __func__);
    
    if(list_empty(my_car_list_head))
        return -1;
    
    list_for_each_entry_safe(current_car, next, my_car_list_head, list)
    {
        printk(KERN_INFO "%s(): num %d rand_int: %d\n", __func__, current_car->num, current_car->price_in_dollars);
    }

    return 0;
}

int LinkedList_3(struct list_head  *my_car_list_head)
{
    struct car *current_car, *next;
    printk(" ---------------- %s() ---------------- \n", __func__);

    if(list_empty(my_car_list_head))
        return -1;
    
    list_for_each_entry_safe(current_car, next, my_car_list_head, list)
    {
        list_del(&current_car->list);
        kfree(current_car); /* if this was dynamically allocated through kmalloc */
    }
    
    printk("%s(): [ released and checking... ]\n", __func__);
    printk("%s(): is_list_empty %d \n", __func__, list_empty(my_car_list_head));
    printk("%s(): [ Done!! ]\n\n\n", __func__);
    return 0;
}

int Queue(void)
{
    int i, ret;
    char *buffer;
    struct kfifo my_queue;
    struct car2 *car_to_add;
    struct car2 amazing_car;
    
    printk(" ---------------- %s() ---------------- \n", __func__);
    printk("%s(): [ allocating... ]\n", __func__); 
    buffer = kmalloc(dstruct_size*sizeof(struct car2), GFP_KERNEL);
    if (!buffer) { 
        pr_err("%s(): Allocation failed\n", __func__); 
        return -1; 
    } 
    
    ret = kfifo_init(&my_queue, buffer, dstruct_size*sizeof(struct car2)); // bumped up to 2^x
    if(ret)
        return ret;
    
    printk("%s(): [ total %d used %d free %d ]\n", __func__, 
                    kfifo_size(&my_queue),kfifo_len(&my_queue), kfifo_avail(&my_queue));
    
    for(i=0; i<dstruct_size; i++){
        ret=kfifo_is_full(&my_queue);
        if(ret)
            return ret;
        
        car_to_add = kmalloc(sizeof(struct car2), GFP_KERNEL);
        if (!car_to_add) { 
            pr_err("%s(): Allocation failed\n", __func__); 
            return -1; 
        } 
        car_to_add->num = i;
        get_random_bytes(&car_to_add->price_in_dollars, sizeof(int));
        if(abs) 
            car_to_add->price_in_dollars = abs(car_to_add->price_in_dollars % dstruct_size);
        
        ret = kfifo_in(&my_queue, car_to_add, sizeof(struct car2));
        if(ret != sizeof(struct car2))
            return ret;     /* Not enough space left in the queue */
        
        printk("%s(): total %d used %d free %d \n", __func__, 
                    kfifo_size(&my_queue),kfifo_len(&my_queue), kfifo_avail(&my_queue));
    }

    printk("%s(): [ total %d used %d free %d ]\n", __func__, 
                    kfifo_size(&my_queue),kfifo_len(&my_queue), kfifo_avail(&my_queue));
    
    printk("\n%s(): [ printing and deleting... ]\n", __func__); 
    while(!kfifo_is_empty(&my_queue)){ 
        ret = kfifo_out(&my_queue, &amazing_car, sizeof(struct car2));
        printk("%s(): num  %d  rand_int %d -  total %d used %d free %d\n", __func__, 
                    amazing_car.num, amazing_car.price_in_dollars,
                    kfifo_size(&my_queue),kfifo_len(&my_queue), kfifo_avail(&my_queue));
    }  
    printk("%s(): kfifo_is_empty() %d\n", __func__, kfifo_is_empty(&my_queue));
    
    kfree(buffer);
    printk("%s(): [ Done!! ]\n\n\n", __func__);
    return 0;
}
   
int Map(void)
{
    int i, id;
    int id_list[dstruct_size+1];
    struct idr *my_map_ptr;

    printk(" ---------------- %s() ---------------- \n", __func__);
    
    my_map_ptr = kmalloc(sizeof(struct idr), GFP_KERNEL);
    if (!my_map_ptr) { 
        pr_err("%s(): Allocation failed\n", __func__); 
        return -1; 
    } 
    idr_init(my_map_ptr);
   
    for(i=0; i<dstruct_size; i++)
    {
        struct car2 *car_to_add = kmalloc(sizeof(struct car2), GFP_KERNEL);
        if (!car_to_add) { 
            pr_err("%s(): Allocation failed\n", __func__); 
            return -1; 
        } 
        car_to_add->num = i;
        get_random_bytes(&car_to_add->price_in_dollars, sizeof(int));
        if(abs) 
            car_to_add->price_in_dollars =  abs(car_to_add->price_in_dollars % dstruct_size);

        idr_preload(GFP_KERNEL);
        id = idr_alloc(my_map_ptr, car_to_add, 100, 100+dstruct_size, GFP_KERNEL);
        idr_preload_end();
        if(id == -ENOSPC)
            return id;  /* error, no id available in the requested range */
        else if(id == -ENOMEM)
            return id;  /* error, could not allocate memory */
        id_list[i] = id;
    }
    
    printk("%s(): [ print, detach, free objects ]\n", __func__);
    for(i=0; i<dstruct_size; i++)
    {
        struct car2 *my_car = idr_find(my_map_ptr, id_list[i]); // returns NULL on error //
        printk("%s(): num %d map_id %d rand_int %d\n", 
                            __func__, my_car->num, id_list[i], my_car->price_in_dollars);
        idr_remove(my_map_ptr, id_list[i]);
        kfree(my_car);
    }

    printk("\n%s(): [ released and checking...]\n", __func__);
    for(i=0; i<dstruct_size; i++)
    {
        struct car2 *my_car = idr_find(my_map_ptr, id_list[i]);
        printk("%s(): num %d map_id %d testing the pointer... - should be null %p\n", 
                                                    __func__, i, id_list[i], my_car);
    }
    
    printk("%s(): [ Done!! ]\n\n\n", __func__);
    return 0;
}   

static int get_independant_num(void)
{
    int cnt=0;
    int i, rand_int=-99;

    while(cnt<=dstruct_size){
        if(g_rand_int_arry[cnt]!=-1){   // if not empty
            if(cnt>dstruct_size)
                return -1;
            cnt++;                      // look at next
            continue;               
        }
        // got an empty spot
retry:
        get_random_bytes(&i, sizeof(i));
        rand_int = abs(i % (dstruct_size*2)); // rand_int range = 0 ~ dstruct_size*2
        for(i=0; i<=cnt; i++){
            if(g_rand_int_arry[i]==rand_int){
                goto retry;
            }
        }
        break; // good
    }
    g_rand_int_arry[cnt] = rand_int;
    smp_mb();
    return rand_int;
}

static int zswap_rb_insert(struct rb_root *root, struct zswap_entry *entry,
                                    struct zswap_entry **dupentry)
{
    struct rb_node **link = &root->rb_node, *parent = NULL;
    struct zswap_entry *myentry;
    while (*link) {
        parent = *link;
        myentry = rb_entry(parent, struct zswap_entry, rbnode);
        if (myentry->offset > entry->offset)
            link = &(*link)->rb_left;
        else if (myentry->offset < entry->offset)
            link = &(*link)->rb_right;
        else {
            *dupentry = myentry;
            return -EEXIST;
        }
    }
    rb_link_node(&entry->rbnode, parent, link);
    rb_insert_color(&entry->rbnode, root);
    return 0;
}

static struct zswap_entry *zswap_rb_search(struct rb_root *root, pgoff_t offset)
{
    struct rb_node *node = root->rb_node;
    struct zswap_entry *entry;
    while (node) {
            entry = rb_entry(node, struct zswap_entry, rbnode);
        if (entry->offset > offset)
            node = node->rb_left;
        else if (entry->offset < offset)
            node = node->rb_right;
        else
            return entry;
    }
    return NULL;
}

/* utility - return number of tree nodes 
 * usage: do printk() before and after this function to make it easier to read*/
static int print_tree(struct rb_node *node)
{
    int ret=0;
    struct zswap_entry *entry;
    
    if (node) { // prevent from all exceptions
        entry = rb_entry(node, struct zswap_entry, rbnode); // get and put into the container
        printk("%d ", entry->offset);
        ret++;
        ret+=print_tree(node->rb_right);
        ret+=print_tree(node->rb_left);
    } 
    return ret;
}

int RBtree(void) 
{
    int i, j, ret, rand_int;
    struct rb_root rbroot;
    spinlock_t tree_lock, int_lock;
    struct zswap_entry *entry, *tmp_zswap, *dupentry; // dupentry is a tmp
    printk(" ---------------- %s() ---------------- \n", __func__);
    
    rbroot = RB_ROOT;
    spin_lock_init(&tree_lock);
    spin_lock_init(&int_lock);

    printk("%s(): insering node_num(rand_int) ", __func__);
    for(i=0; i<dstruct_size; i++){
        spin_lock(&int_lock);
        rand_int = get_independant_num();
        spin_unlock(&int_lock); 
        if (rand_int<0){
            printk("%s(): bad rand_int %d\n", __func__, rand_int);
            return rand_int;
        }
        
        /* insersion */
        entry = kmalloc(sizeof(struct zswap_entry), GFP_KERNEL);
        if (!entry) { 
            pr_err("%s(): Allocation failed\n", __func__); 
            return -1; 
        } 
        entry->offset = rand_int;   // node=data=rand_int
        spin_lock(&tree_lock);
        ret = zswap_rb_insert(&rbroot, entry, &dupentry);
        spin_unlock(&tree_lock); 
        printk("%d ", rand_int);
    }
    printk("\n");
    
    printk("\n%s(): [ insersion done, removing range=dstruct_size/2=%d... ]\n\n", __func__, range);
    for(i=0; i<dstruct_size*2; i+=range)
    {
        printk("%s(): [ try range %d - %d ]\n", __func__, i, i+range-1);
        for(j=i; j<i+range; j++) {
            tmp_zswap = zswap_rb_search(&rbroot, j);
            if(tmp_zswap){
                rb_erase(&tmp_zswap->rbnode, &rbroot);
                kfree(tmp_zswap);
                printk("%s(): try tree node %d - found and removed\n", __func__, j);
            }
            else {
                printk("%s(): try tree node %d - not found\n", __func__, j);
            }
        }
        printk("%s(): print_tree(): ", __func__);
        ret = print_tree(rbroot.rb_node);
        printk("\n");
        printk("%s(): num_tree_nodes %d\n\n", __func__, ret);
    }
    printk("%s(): [ Done!! ]\n\n\n", __func__);
    return 0;
}

/* Return 0 on success, something else on error */
static int __init lkp_init(void)
{
    int err;
    LIST_HEAD(my_car_list2); // for LinkedList 2nd implementation

    if(dstruct_size<1){
        printk(KERN_ERR " === dstruct_size %d should be larger than 0 === \n", dstruct_size); 
        return -1;
    }
    
    /* init for RBtree */
    range = dstruct_size/2;
    g_rand_int_arry = kmalloc(sizeof(int)*(dstruct_size+1), GFP_KERNEL);
    if (!g_rand_int_arry) { 
        pr_err("%s(): Allocation failed\n", __func__);                                                                             
        return -1; 
    }                                                                                                                                  
    memset(g_rand_int_arry, (int)-1, sizeof(int)*(dstruct_size+1));

    printk(KERN_INFO " ===================== Project 2 begins ==================== \n");
    printk(KERN_INFO "Int param: %d default(10)\n", dstruct_size);

    err = LinkedList();
    if(err) 
        return err;
    

    err = LinkedList_1(&my_car_list2);
    if(err) 
        return err;

    err = LinkedList_2(&my_car_list2);
    if(err) 
        return err;

    err = LinkedList_3(&my_car_list2);
    if(err) 
        return err;


    err=Queue();
    if(err) 
        return err;
    
    err=Map();
    if(err)
        return err;
    
    err=RBtree();
    if(err)
        return err;
    
    printk(KERN_INFO " ===================== Project 2 done ======================= \n");
    return err;
}

static void __exit lkp_exit(void)
{
    kfree(g_rand_int_arry);
    printk(KERN_INFO "Module exiting ...\n");
}

module_init(lkp_init);
module_exit(lkp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jack Chuang<horenc@vt.edu>");
MODULE_DESCRIPTION("Project2 - kernel module");
