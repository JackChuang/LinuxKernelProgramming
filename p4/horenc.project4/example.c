#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h> 	/* PAGE_CACHE_SIZE */
#include <linux/fs.h>     	/* This is where libfs stuff is declared */
#include <asm/atomic.h>
#include <asm/uaccess.h>	/* copy_to_user */

#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/slab.h>

/*
 * Boilerplate stuff.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jack Zhang & Jack Chuang");

#define LFS_MAGIC 0x19980122
//#define EXAMPLE_MODE 0755
#define EXAMPLE_MODE 0775
#define TMPSIZE 20
#define BUFSIZE 4*4096

/*
 *  Note: ps -C p4 -O RSS
 */

/* utilities */
#define for_each_children(pos,head) \
    list_for_each_entry(pos, &(head -> children), sibling)
#define for_each_sibling(pos,head) \
    for_each_children(pos, (head->parent) )

#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"

/*
 * Anytime we make a file or directory in our filesystem we need to
 * come up with an inode to represent it internally.  This is
 * the function that does that job.  All that's really interesting
 * is the "mode" parameter, which says whether this is a directory
 * or file, and gives the permissions.
 */
static struct inode *lfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_ino = get_next_ino();
		ret->i_mode = mode;
		i_uid_write(ret, 0); // 16bit user id associated with the file
		i_gid_write(ret, 0); // 16bit value of the POSIX group having access to this file.
		ret->i_blocks = 0;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
	}
	return ret;
}


/*
 * The operations on our "files".
 */

/*
 * Open a file.  All we have to do here is to copy over a
 * copy of the counter pointer so it's easier to get at.
 */
static int lfs_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}


int cascade_output(size_t *count, int *len, loff_t *offset)
{

    return 0;
}

void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
                    sigset_t *catch)
{
    struct k_sigaction *k;
    int i;

    k = p->sighand->action;
    for (i = 1; i <= _NSIG; ++i, ++k) {
        if (k->sa.sa_handler == SIG_IGN)
            sigaddset(ign, i);
        else if (k->sa.sa_handler != SIG_DFL)
            sigaddset(catch, i);
    }
}

/*
 * Read a file.  Here we increment and read the counter, then pass it
 * back to the caller.  The increment only happens if the read is done
 * at the beginning of the file (offset = 0); otherwise we end up counting
 * by twos.
 */
static ssize_t lfs_read_file(struct file *filp, char *buf,
		size_t count, loff_t *offset)
{
	int counter = (int)filp->private_data;
	int len = 0, i;
    struct task_struct *t;
	char tmp2[NAME_MAX];
	char *tmp;
    tmp = kmalloc(BUFSIZE, GFP_KERNEL);
    if(!tmp)
        BUG();

    if (*offset > 0)
        return 0;

    len += snprintf(tmp + strlen(tmp), NAME_MAX, "pid %d\n", counter);
    
    if(counter > 0)
        t = pid_task(find_vpid(counter), PIDTYPE_PID);
    else
        t = &init_task;
    if(!t) {
        printk(KERN_ERR "%s(): cannot find pid %d\n", __func__, counter);
        return len;
    }
    printk("\n\ncat pid %d\n\n", counter);

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- Project4 minimun requirements --\n");
    switch(t->state) {
    case TASK_RUNNING:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_RUNNING") <= 0)
            BUG();
        break;
    case TASK_INTERRUPTIBLE:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_INTERRUPTIBLE") <= 0)
            BUG();
        break; 
    case TASK_UNINTERRUPTIBLE:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_UNINTERRUPTIBLE:") <= 0)
            BUG();
        break;
    case __TASK_STOPPED:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "__TASK_STOPPED") <= 0)
            BUG();
        break;
    case __TASK_TRACED:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "__TASK_TRACED") <= 0)
            BUG();
        break;
    case EXIT_ZOMBIE:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "EXIT_ZOMBIE") <= 0)
            BUG();
        break;
    case EXIT_DEAD:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "EXIT_DEAD") <= 0)
            BUG();
        break;
    case TASK_DEAD:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_DEAD") <= 0)
            BUG();
        break;
    case TASK_WAKEKILL:
        if(  snprintf(tmp2, sizeof(tmp2), "%s", "TASK_WAKEKILL") <= 0)
            BUG();
        break;
    case TASK_WAKING: 
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_WAKING") <= 0)
            BUG();
        break;
    case TASK_PARKED:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_PARKED") <= 0 )
            BUG();
        break;
    case TASK_STATE_MAX:
        if( snprintf(tmp2, sizeof(tmp2), "%s", "TASK_STATE_MAX") <= 0 )
            BUG();
        break;
    }
    
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "1. state %ld %s\n", t->state, tmp2);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "2. on CPU %u\n", task_thread_info(t)->cpu);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "3. I'm a %s\n", t->mm?"USER thread":"KERNEL thread");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "4. start_time at %llu ns, "
                                                    "real_start_time at %llu ns\n", 
                                                    t->start_time, // monotonic time
                                                    t->real_start_time); // boot based time
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "5. comm \"%s\"\n", t->comm);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "6. stack pointer & mem usages \n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\tUser sp \t %p\n", (void*)t->thread.usersp);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\tKernel sp \t %p\n", (void*)t->thread.sp);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\tKernel sp0 \t %p\n", (void*)t->thread.sp0);
    if(t->mm) {
        struct mm_struct *mm = t->mm;
        unsigned long data, text, lib, swap;
        unsigned long hiwater_vm, total_vm, hiwater_rss, total_rss;
        hiwater_vm = total_vm = mm->total_vm;
        if (hiwater_vm < mm->hiwater_vm)
            hiwater_vm = mm->hiwater_vm;
        hiwater_rss = total_rss = get_mm_rss(mm);
        if (hiwater_rss < mm->hiwater_rss)
            hiwater_rss = mm->hiwater_rss;

        data = mm->total_vm - mm->shared_vm - mm->stack_vm;
        text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK)) >> 10;
        lib = (mm->exec_vm << (PAGE_SHIFT-10)) - text;
        swap = get_mm_counter(mm, MM_SWAPENTS);
        len += snprintf(tmp + strlen(tmp), BUFSIZE,
                        "\tVmPeak:\t%8lu kB\n"
                        "\tVmSize:\t%8lu kB\n"
                        "\tVmLck:\t%8lu kB\n"
                        "\tVmPin:\t%8lu kB\n"
                        "\tVmHWM:\t%8lu kB\n"
                        "\tVmRSS:\t%8lu kB\n"
                        "\tVmData:\t%8lu kB\n"
                        "\tVmStk:\t%8lu kB\n"
                        "\tVmExe:\t%8lu kB\n"
                        "\tVmLib:\t%8lu kB\n"
                        "\tVmPTE:\t%8lu kB\n"
                        "\tVmSwap:\t%8lu kB\n",
                        hiwater_vm << (PAGE_SHIFT-10),
                        total_vm << (PAGE_SHIFT-10),
                        mm->locked_vm << (PAGE_SHIFT-10),
                        mm->pinned_vm << (PAGE_SHIFT-10),
                        hiwater_rss << (PAGE_SHIFT-10),
                        total_rss << (PAGE_SHIFT-10),
                        data << (PAGE_SHIFT-10),
                        mm->stack_vm << (PAGE_SHIFT-10), text, lib,
                        (PTRS_PER_PTE*sizeof(pte_t) * atomic_long_read(&mm->nr_ptes)) >> 10,
                        swap << (PAGE_SHIFT-10));
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t # of pages %lu\n", atomic_long_read(&t->mm->nr_ptes)); /* Page table pages */
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
    }
    else {
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t kernel thread doen't suppor t->mm\n");
    }
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
   

   
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- Thread --\n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "ip(x32) fs(x64) %p\n", (void*)t->thread.fs);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "es \t %p\n", (void*)t->thread.es);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "ds \t %p\n", (void*)t->thread.ds);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "gs \t %p\n", (void*)t->thread.gs);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "cr2 \t %p\n", (void*)t->thread.cr2);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "pid \t\t\t %d\n", t->pid);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "tgid \t\t\t %d\n", t->tgid);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "User time \t\t %lu\n", (unsigned long)t->utime);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "System time \t\t %lu\n", (unsigned long)t->stime);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Last updat stime + utime %lu\n", (unsigned long)t->acct_timexpd);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Priority \t\t %d\n", t->prio);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "RT priority \t\t %d\n", t->rt_priority);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Task properties \t %d (check process flags)\n", t->flags);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Group leader pid \t %d\n", t->group_leader->pid);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Self_exec_id \t\t %d\n", t->self_exec_id);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Original group process id %d\n", t->parent_exec_id);
    //printk("average sleep time %lu\n", t->sleep_avg);
    //printk("interactive_credit %ld\n", t->interactive_credit);
#ifdef CONFIG_LOCKDEP
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Lock deep level %lu when doing contex switch\n", t->lockdep_depth);
#else
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Locksep_depth is not turned on\n");
#endif
#if defined(CONFIG_TASK_XACCT)
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Accumulated resident memory usage %llu B\n"
                                                "Accumulated virtual memory usage %llu B\n", 
                                                            t->acct_rss_mem1, t->acct_vm_mem1);
#else
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t not support mm usage \n", (void*)t->thread.sp);
#endif
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
    


    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- Schedule --\n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Timeout %lu ns\n", t->rt.timeout);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Sched_entity on_rq %u\n", t->se.on_rq);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Last start time %llu ns\n", t->se.exec_start);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Sum of total execution real run time %llu ns\n", t->se.sum_exec_runtime);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Virtual runtime %llu ns\n", t->se.vruntime);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Previous sum execution time %llu ns\n", t->se.prev_sum_exec_runtime);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Number of migrations %llu\n", t->se.nr_migrations);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Time slice you can run %u ns\n", t->rt.time_slice);
#ifdef CONFIG_SCHEDSTATS
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- schedule statistic --\n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "wait_start %llu ns\n", t->se.statistics.wait_start);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "wait_max %llu ns\n", t->se.statistics.wait_max);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "wait_count %llu\n", t->se.statistics.wait_count);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "wait_sum %llu ns\n", t->se.statistics.wait_sum);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "iowait_count %llu\n", t->se.statistics.iowait_count);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "iowait_sum %llu ns\n", t->se.statistics.iowait_sum);

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "sleep_start %llu ns\n", t->se.statistics.sleep_start);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "sleep_max %llu ns\n", t->se.statistics.sleep_max);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "sum_sleep_runtime %llu ns\n", t->se.statistics.sum_sleep_runtime);

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "block_start %llu ns\n", t->se.statistics.block_start);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "block_max %llu ns\n", t->se.statistics.block_max);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "exec_max %llu ns\n", t->se.statistics.exec_max);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "slice_max %llu ns\n", t->se.statistics.slice_max);

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_migrations_cold %llu\n", t->se.statistics.nr_migrations_cold);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_failed_migrations_affine %llu\n", t->se.statistics.nr_failed_migrations_affine);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_failed_migrations_running %llu\n", t->se.statistics.nr_failed_migrations_running);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_failed_migrations_hot %llu\n", t->se.statistics.nr_failed_migrations_hot);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_forced_migrations %llu\n", t->se.statistics.nr_forced_migrations);

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups %llu\n", t->se.statistics.nr_wakeups);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_sync %llu\n", t->se.statistics.nr_wakeups_sync);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_migrate %llu\n", t->se.statistics.nr_wakeups_migrate);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_local %llu\n", t->se.statistics.nr_wakeups_local);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_remote %llu\n", t->se.statistics.nr_wakeups_remote);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_affine %llu\n", t->se.statistics.nr_wakeups_affine);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_affine_attempts %llu\n", t->se.statistics.nr_wakeups_affine_attempts);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_passive %llu\n", t->se.statistics.nr_wakeups_passive);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "nr_wakeups_idle %llu\n", t->se.statistics.nr_wakeups_idle);
#endif
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");



    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- MM --\n");
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "My adress space %p\n", (void*)t->mm); //pgd points to page table
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Current address space being used %p\n", (void*)t->active_mm);
    if(t->mm) { 
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Physical memory is being used (no swap included) %lu KB\n", 
                                                                                            get_mm_rss(t->mm)); // FILPAGES+ANONPAGES
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Total vm pages mapped %lu KB\n", t->mm->total_vm); // Total pages mapped
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "# of VMAs %d\n", t->mm->map_count);
    
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "highest_vm_end %p\n", (void*)t->mm->highest_vm_end);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Page global directory pointer %p\n", (void*)t->mm->pgd); //pt ptr
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "virtual memory areas \t start %p end %p\n", 
                                                        (void*)t->mm->mmap->vm_start, (void*)t->mm->mmap->vm_end);
        //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Physical memory is being used (no swap included) "
        //                                                        "(hiwater) %lu KB\n", get_mm_hiwater_rss(t->mm));
        //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Virtual memory is being used (no swap included) "
        //                                                          "(hiwater) %lu KB\n", get_mm_hiwater_vm(t->mm));
        //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t  Physical memory is being used (no swap included) %lu KB\n", t->mm->hiwater_rss);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Virtual memory (high-watermark) %lu KB\n", t->mm->hiwater_vm);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Physical memory (high-watermark) %lu KB\n", t->mm->hiwater_rss);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "locked_vm pages %lu KB\n", t->mm->locked_vm); /* Pages that have PG_mlocked set */
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "pinned vm pages %lu KB\n", t->mm->pinned_vm);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "shared vm pages (files) %lu KB\n", t->mm->shared_vm);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "exec vm pages & ~vm write pages  %lu KB\n", t->mm->exec_vm);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "stack vm pages %lu KB\n", t->mm->stack_vm);
        /* RES = mm_struct->_file_rss + mm_struct->_anon_rss
         * SHR = mm_struct->file_rss */
        


        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n---segments---\n");
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Stack segment (downward) \tstart %p\n", (void*)t->mm->start_stack);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Mmap segment (downward) \tstart %p\n", (void*)t->mm->mmap_base);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Heap segment (upward) \tstart %p end %p\n", (void*)t->mm->start_brk, (void*)t->mm->brk);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Data segment (upward) \tstart %p end %p\n", (void*)t->mm->start_data, (void*)t->mm->end_data);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Text segment (upward) \tstart %p end %p\n", (void*)t->mm->start_code, (void*)t->mm->end_code);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Env segment (upward) \tstart %p end %p\n", (void*)t->mm->env_start, (void*)t->mm->env_end);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "Arg segment (upward) \tstart %p end %p\n", (void*)t->mm->arg_start, (void*)t->mm->arg_end);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "top %p\n", (void*)t->thread.sp);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "bottom %p\n", (void*)task_thread_info(t));
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "t->stackk %p\n", (void*)t->stack);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");

        len += snprintf(tmp + strlen(tmp), BUFSIZE, "VM property %lu (flags)\n", t->mm->def_flags);
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "# of users are using this mm %d \n", (int)atomic_read(&t->mm->mm_users));
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "# of kernel threads are using this mm %d (users=1)\n", (int)atomic_read(&t->mm->mm_count));
        len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");
        


        len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- File --\n");
        char path[NAME_MAX] = { '\0' };
        char* rpath;
        rpath = d_path(&t->mm->exe_file->f_path, path, NAME_MAX);
        if(rpath) {
            len += snprintf(tmp + strlen(tmp), BUFSIZE, "Executable binary name \"%s\"\n", rpath);
        }
        else {
            len += snprintf(tmp + strlen(tmp), BUFSIZE, "No executable binary\n");
        }
       
        struct fdtable *fdt_tmp;
        int loop;
        fdt_tmp = files_fdtable(t->files);
        for (loop=0; loop<fdt_tmp->max_fds; loop++) {
            if (fdt_tmp->fd[loop]!=NULL) {
                len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- File %d existing --\n", loop);
                len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Cursor position %d\n",  (int)fdt_tmp->fd[loop]->f_pos);
                //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Owner's pid %d\n",    (int)(fdt_tmp->fd[loop]->f_owner.pid));
                //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Owner's uid %d \n",   (int)fdt_tmp->fd[loop]->f_owner.uid.val);
                //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Owner's euid %d \n",  (int)fdt_tmp->fd[loop]->f_owner.euid.val);
                //len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t Inode uid %d \n",     (int)fdt_tmp->fd[loop]->f_inode->i_uid.val);
                len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t File permission %d \n", fdt_tmp->fd[loop]->f_mode);
                len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t File flags %d \n",      fdt_tmp->fd[loop]->f_flags);
                char path[NAME_MAX] = {'\0'};
                char *ppath = path; 
                ppath = d_path(&fdt_tmp->fd[loop]->f_path, ppath, NAME_MAX);
                len += snprintf(tmp + strlen(tmp), BUFSIZE, "\t File name \"%s\"\n", ppath);
            }
        }
    }
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");



    len += snprintf(tmp + strlen(tmp), BUFSIZE, "-- Signal --\n");
    for(i=1; i<(_NSIG-32); i++) {
        if( i!=SIGUSR1 && i!=SIGUSR2 ) {
            len += snprintf(tmp + strlen(tmp), BUFSIZE, "Signal %d handler %p\n", i, (void*)t->sighand->action[i-1].sa.sa_handler);
        }
        else {
            len += snprintf(tmp + strlen(tmp), BUFSIZE, "Signal %d handler %p (%s handler) \n", i,
                    (void*)t->sighand->action[i-1].sa.sa_handler, i==SIGUSR1?"SIGUSR1":"SIGUSR2");
        }
    }
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "\n");

    sigset_t pending, shpending, blocked, ignored, caught;
    int num_threads = 0;
    unsigned long qsize = 0;
    unsigned long qlim = 0;
    struct task_struct *p = t;

    sigemptyset(&pending);
    sigemptyset(&shpending);
    sigemptyset(&blocked);
    sigemptyset(&ignored);
    sigemptyset(&caught);

    // No lock protecting this now!!
    //if (lock_task_sighand(p, &flags)) {
        pending = p->pending.signal;
        shpending = p->signal->shared_pending.signal;
        blocked = p->blocked;
        collect_sigign_sigcatch(p, &ignored, &caught);
        num_threads = get_nr_threads(p);
        rcu_read_lock();  /* FIXME: is this correct? */
        qsize = atomic_read(&__task_cred(p)->user->sigpending);
        rcu_read_unlock();
        qlim = task_rlimit(p, RLIMIT_SIGPENDING);
        //unlock_task_sighand(p, &flags);
    //}

    len += snprintf(tmp + strlen(tmp), BUFSIZE, "Threads:\t%d\n", num_threads);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "SigQ:\t%lu/%lu\n", qsize, qlim);

    /* render them all */
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "SigPnd: %p\n", &pending);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "ShdPnd: %p\n", &shpending);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "SigBlk: %p\n", &blocked);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "SigIgn: %p\n", &ignored);
    len += snprintf(tmp + strlen(tmp), BUFSIZE, "SigCgt: %p\n", &caught);

    if( len > BUFSIZE )
        len = BUFSIZE;
    
    if (copy_to_user(buf, tmp + *offset, len ))
		return -EFAULT;
	*offset += len;

    printk("count %lu len %d *offset %llu\n", count, len, *offset);
    kfree(tmp);
    return len; 
}

/*
static ssize_t lfs_write_file(struct file *filp, const char *buf,
		size_t count, loff_t *offset)
{
	//atomic_t *counter = (atomic_t *) filp->private_data;
	int counter = (int) filp->private_data;
	char tmp[TMPSIZE];
	
    if (*offset != 0)
		return -EINVAL;
	
    if (count >= TMPSIZE)
		return -EINVAL;
	memset(tmp, 0, TMPSIZE);
	
    if (copy_from_user(tmp, buf, count))
		return -EFAULT;
    counter = simple_strtol(tmp, NULL, 10);
    
    printk("%s(): count %lu returned\n\n\n", __func__, count);
	return count;
}
*/

static ssize_t lfs_write_sig_file(struct file *filp, const char *buf,
        size_t count, loff_t *offset)
{
    int sig_num;
	char tmp2[TMPSIZE];
    struct task_struct *t;
    int target_pid = (int)filp->private_data;

	if (*offset != 0)
		return -EINVAL;
	
    if (count >= TMPSIZE)
		return -EINVAL;
	memset(tmp2, 0, TMPSIZE);
	if (copy_from_user(tmp2, buf, count))
		return -EFAULT;

    /* p4 */
    sig_num = simple_strtol(tmp2, NULL, 10);

    if(target_pid > 0)
        t = pid_task(find_vpid(target_pid), PIDTYPE_PID);
    else
        t = &init_task;
    if(!t) {
        printk(KERN_ERR "%s(): cannot find pid %d\n", __func__, target_pid);
        return 0;
    }  

    printk("signal !!!!!!!!!!!!!!!!!!!!\n");
    printk("echo %d > pid %d\n\n\n", sig_num, target_pid);

    if(t->state==TASK_DEAD) {
        printk("This process has been terminated !! Check the process state with $ps aux\n\n\n");
    }
    send_sig(sig_num, t, 1); // ? SEND_SIG_PRIV : SEND_SIG_NOINFO)
   
    return count;
}

/*
 * Now we can put together our file operations structure.
 */
/* status */
static struct file_operations lfs_file_ops = {
	.open	= lfs_open,
	.read 	= lfs_read_file,
	        //.write  = lfs_write_file,
};

/* signal */
static struct file_operations lfs_file_ops2 = {
	.open	= lfs_open,
	        //.read 	= lfs_read_file,
	.write  = lfs_write_sig_file,
};

/*
 * Create a file mapping a name to a counter.
 */
static struct dentry *lfs_create_file (struct super_block *sb,
		struct dentry *dir, const char *name,
		atomic_t *counter, int pid, bool is_signal)
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

/*
 * Make a hashed version of the name to go with the dentry.
 */
	qname.name = name;
	qname.len = strlen (name);
	qname.hash = full_name_hash(name, qname.len);
/*
 * Now we can create our dentry and the inode to go with it.
 */
	dentry = d_alloc(dir, &qname);
	if (! dentry)
		goto out;
	
    inode = lfs_make_inode(sb, S_IFREG | EXAMPLE_MODE); // S_IFREG: file
	
    if (! inode)
		goto out_dput;
    
    if(!is_signal)
	    inode->i_fop = &lfs_file_ops; // status
    else
	    inode->i_fop = &lfs_file_ops2; // sig
	
    inode->i_private = (void*)pid;
    //inode->i_private = counter;
/*
 * Put it all into the dentry cache and we're done.
 */
	d_add(dentry, inode);
	return dentry;
/*
 * Then again, maybe it didn't work.
 */
  out_dput:
	dput(dentry);
  out:
	return 0;
}

/*
 * Create a directory which can be used to hold files.  This code is
 * almost identical to the "create file" logic, except that we create
 * the inode with a different mode, and use the libfs "simple" operations.
 */
static struct dentry *lfs_create_dir (struct super_block *sb,
		struct dentry *parent, const char *name)
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;

	qname.name = name;
	qname.len = strlen (name);
	qname.hash = full_name_hash(name, qname.len);
	dentry = d_alloc(parent, &qname);
	if (! dentry)
		goto out;

	inode = lfs_make_inode(sb, S_IFDIR | EXAMPLE_MODE); // S_IFDIR: dir
	if (! inode)
		goto out_dput;

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	d_add(dentry, inode);
	return dentry;

  out_dput:
	dput(dentry);
  out:
	return 0;
}

/*
 * OK, create the files that we export.
 */
static atomic_t counter;//, subcounter;

/*
 * 1. create folder
 * 2. fillup status & signal
 */
struct dentry* sub_create_ff_real(struct super_block *sb, 
                                  struct task_struct *init_t, 
                                  struct dentry *dir)
{
    char *buf;
    struct dentry * ret;
	struct dentry *subdir;
    buf = kmalloc(NAME_MAX, GFP_KERNEL); //linux/limits.h
    if(!buf) {
        printk(KERN_ERR "%s(): cannot kmalloc", __func__);
        BUG();
   }

    printk("pid %d %s\n", init_t->pid, init_t->mm?"user":"kernel");
    sprintf(buf, "%d", init_t->pid);
    subdir = lfs_create_dir(sb, dir, buf);
    if(!subdir) {
        printk(KERN_ERR "%s(): cannot create subdir\n", __func__);
        BUG();
    }
    ret = lfs_create_file(sb, subdir, "signal", &counter, init_t->pid, true);
    if(!ret) {
        printk(KERN_ERR "%s(): cannot create signal file\n", __func__);
    }

    strcat(buf, ".status");
    ret = lfs_create_file(sb, subdir, buf, &counter, init_t->pid, false);
    if(!ret) {
        printk(KERN_ERR "%s(): cannot create status file\n", __func__);
    }
    kfree(buf);
    return subdir;
}

/*
 * 1. create
 * 2. do 1. for childs
 */
// first time dir = root , init_t = 1
void sub_create_subfiles(struct super_block *sb, struct task_struct *init_t, struct dentry *dir)
{
	struct dentry *subdir;
    struct task_struct *t2;
    
    if(!init_t)
        return;
    
    subdir = sub_create_ff_real(sb, init_t, dir);

    for_each_children(t2, init_t) { // struct list_head children;
        printk("\t\t\tinit_t %d mychild %d\n", init_t->pid, t2->pid);
        sub_create_subfiles(sb, t2, subdir);
    }
    
    return;
}

static void lfs_create_files (struct super_block *sb, struct dentry *root)
{
	struct dentry *subdir;
    struct task_struct *t, *t2;
    
    printk("init_task.pid %d\n", init_task.pid);
    t = &init_task;
    if(!t) {
        printk(KERN_ERR "%s(): cannot find pid 1\n", __func__);
        return;
    }
    
    /* 1st level */
    printk("debug t->pid %d\n", t->pid);
    subdir = sub_create_ff_real(sb, t, root);


    /* recursively create */
    for_each_sibling(t2, t) { // struct list_head children;
        printk("\t\tsibling: %d\n", t2->pid);
        sub_create_subfiles(sb, t2, subdir);
    }
    printk("\n\n\n");
    return;
}



/*
 * Superblock stuff.  This is all boilerplate to give the vfs something
 * that looks like a filesystem to work with.
 */

/*
 * Our superblock operations, both of which are generic kernel ops
 * that we don't have to write ourselves.
 */
static struct super_operations lfs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

/*
 * "Fill" a superblock with mundane stuff.
 */
static int lfs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct dentry *root_dentry;
/*
 * Basic parameters.
 */
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = LFS_MAGIC;
	sb->s_op = &lfs_s_ops;
/*
 * We need to conjure up an inode to represent the root directory
 * of this filesystem.  Its operations all come from libfs, so we
 * don't have to mess with actually *doing* things inside this
 * directory.
 */
	root = lfs_make_inode (sb, S_IFDIR | EXAMPLE_MODE);
	if (! root)
		goto out;
	root->i_op = &simple_dir_inode_operations;
	root->i_fop = &simple_dir_operations;
/*
 * Get a dentry to represent the directory in core.
 */
	root_dentry = d_make_root(root);
	if (! root_dentry)
		goto out_iput;
	sb->s_root = root_dentry;
/*
 * Make up the files which will be in this filesystem, and we're done.
 */
	lfs_create_files (sb, root_dentry); // main()
	return 0;
	
  out_iput:
	iput(root);
  out:
	return -ENOMEM;
}

/*
 * Stuff to pass in when registering the filesystem.
 */
static struct dentry *lfs_get_super(struct file_system_type *fst,
		int flags, const char *devname, void *data)
{
	return mount_nodev(fst, flags, data, lfs_fill_super);
}

static struct file_system_type lfs_type = {
	.owner 		= THIS_MODULE,
	.name		= "lwnfs",
	.mount		= lfs_get_super,
	.kill_sb	= kill_litter_super,
};

/*
 * Get things set up.
 */
static int __init lfs_init(void)
{
	return register_filesystem(&lfs_type);
}

static void __exit lfs_exit(void)
{
	unregister_filesystem(&lfs_type);
}

module_init(lfs_init);
module_exit(lfs_exit);
