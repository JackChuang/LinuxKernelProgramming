/* JPROBE used for KLP Project5
 * Author: 
 * 	Ho-Ren Chuang
 * 	Chris Jelesnianski
 * 	Mincheol Sung 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/types.h>
#include <linux/irqreturn.h>
#include <linux/hrtimer.h>

#define FUNC_NAME "??"

const char *name = "cyclictest";

bool is_cyclitest(struct task_struct *p)
{
    if(p) {
        if(!memcmp(p->comm, name, strlen(name))) {
            if(p->pid == p->tgid+1) {
                return true;
            }
            else {
                return false;
            }
        }
    }
    return false;
}

static char func_name_fair[] = "pick_next_task_fair";
static char func_name_rt[] = "pick_next_task_rt";
struct my_data {
    ktime_t entry_stamp;
};

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{   
    return 0;
}

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{   
	long long time_stamp;
    struct task_struct *retval;
    retval = (struct task_struct*)regs_return_value(regs);
    if(is_cyclitest(retval)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): pid %d return time: %lld\n", func_name_fair, retval->pid, time_stamp);
    }
    return 0;
}

static struct kretprobe my_kretprobe = {
    .handler        = ret_handler,
    .entry_handler  = entry_handler,
    .data_size      = sizeof(struct my_data),
    .maxactive      = 20,
};


static int rt_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{   
    return 0;
}

static int rt_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{ 
	long long time_stamp;
    struct task_struct *retval;
    retval = (struct task_struct*)regs_return_value(regs);
    if(is_cyclitest(retval)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): pid %d return time: %lld\n", func_name_rt, retval->pid, time_stamp);
    }
    return 0;
}

static struct kretprobe rt_kretprobe = {
    .handler        = rt_ret_handler,
    .entry_handler  = rt_entry_handler,
    .data_size      = sizeof(struct my_data),
    .maxactive      = 20,
};


#if 0
// The jprobe
// handler routine should have the same signature (arg list and return
// type) as the function being probed!!!!
static long jtemplate_handler(){
	
	//nano seconds!!!
	long long time_stamp = ktime_to_ns(ktime_get());
	
	printk(">> %s time: %lld\n",func_name,time_stamp);

	/* always end with call to jprobe_return(). */
	jprobe_return();
	return 0;
}
static struct jprobe t_jprobe = {
	.entry			= jtemplate_handler,
	.kp = {
		.symbol_name	= FUNC_NAME,
	},
};
#endif


// DEQUEUE
static void jtemplate_handler_dequeue_task(struct rq *rq, struct task_struct *p, int flags)
{
	long long time_stamp;
    if(is_cyclitest(p)) {
	    time_stamp = ktime_to_ns(ktime_get());
	    printk("%s(): pid %d enter time: %lld\n\n", "deactivate_task", p->pid, time_stamp);
    }
	
	jprobe_return();
    return;
}
static struct jprobe t_jprobe_dequeue_task = {
	.entry			= jtemplate_handler_dequeue_task,
	.kp = {
		.symbol_name	= "deactivate_task",
	},
};

#if 1
/////////////////////////////////////////////////////////////////////////////////////
static enum hrtimer_restart jtemplate_handler_hrtimer_wakeup(struct hrtimer *timer)
{
	long long time_stamp;
    
    struct hrtimer_sleeper *t =
        container_of(timer, struct hrtimer_sleeper, timer);
    struct task_struct *task = t->task;

    if(is_cyclitest(task)) {
	    time_stamp = ktime_to_ns(ktime_get());
	    printk("%s(): pid %d time: %lld\n", "hrtimer_wakeup", task->pid, time_stamp);
    }

	jprobe_return();
	return 0;
}
static struct jprobe t_jprobe_hrtimer_wakeup = {
	.entry			= jtemplate_handler_hrtimer_wakeup,
	.kp = {
		.symbol_name	= "hrtimer_wakeup",
	},
};

#endif

///////////////////////////////////////////////////////////////////////////////////////
static int __sched jtemplate_handler_nanosleep_entry(struct hrtimer_sleeper *t, enum hrtimer_mode mode)
{
	long long time_stamp;
    if(is_cyclitest(current)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): pid %d enter time: %lld\n", "do_nanosleep", current->pid, time_stamp);
    }
	
	jprobe_return();
	return 0;
}

static struct jprobe t_jprobe_nanosleep_entry = {
	.entry			= jtemplate_handler_nanosleep_entry,
	.kp = {
		.symbol_name	= "do_nanosleep",
	},
};



///////////////////////////////////////////////////////////////////////////////////////////
int jtemplate_handler_wakeup_entry(struct task_struct *p)
{
	long long time_stamp;
    if(is_cyclitest(p)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): pid %d enter time: %lld\n", "wake_up_process", p->pid, time_stamp);
    }
	jprobe_return();
    return 0;
}
static struct jprobe t_jprobe_wakeup_entry = {
	.entry			= jtemplate_handler_wakeup_entry,
	.kp = {
		.symbol_name	= "wake_up_process",
	},
};


///////////////////////////////////////////////////////////////////////////////////////////
static void jtemplate_handler_scheduler_tick_entry(struct pt_regs *regs)
{
	long long time_stamp;
    if(is_cyclitest(NULL)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): enter time: %lld\n", "scheduler_tick", time_stamp);
    }
	jprobe_return();
}
static struct jprobe t_jprobe_scheduler_tick_entry = {
	.entry			= jtemplate_handler_scheduler_tick_entry,
	.kp = {
		.symbol_name	= "scheduler_tick",
	},
};

///////////////////////////////////////////////////////////////////////////////////////////
static struct rq *jtemplate_handler_cs_entry(
                                        struct rq *rq, 
                                        struct task_struct *prev,
                                        struct task_struct *next)
{
	long long time_stamp;
    if(is_cyclitest(next)) {
	    time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): pid %d enter time: %lld\n", "context_switch", next->pid, time_stamp);
    }
	jprobe_return();
    return rq;
}
static struct jprobe t_jprobe_cs_entry = {
	.entry			= jtemplate_handler_cs_entry,
	.kp = {
		.symbol_name	= "context_switch",
	},
};
#if 1
///////////////////////////////////////////////////////////////////////////////////////////
static void jtemplate_handler_timer_entry(struct pt_regs *regs)
{
	long long time_stamp;
    	time_stamp = ktime_to_ns(ktime_get());
        printk("%s(): enter time: %lld\n", "smp_apic_timer_interrupt", time_stamp);
	jprobe_return();
}
static struct jprobe t_jprobe_timer_entry = {
	.entry			= jtemplate_handler_timer_entry,
	.kp = {
		.symbol_name	= "smp_apic_timer_interrupt",
	},
};
#endif
///////////////////////////////////////////////////////////////////////////////
static void jtemplate_handler_enqueue_task(struct rq *rq, struct task_struct *p, int flags){
	
	long long time_stamp;
    if(is_cyclitest(p)) {
	    time_stamp = ktime_to_ns(ktime_get());
	    printk("%s(): pid %d enter time: %lld\n","activate_task", p->pid, time_stamp);
    }
	
	jprobe_return();
}

static struct jprobe t_jprobe_enqueue_task = {
	.entry			= jtemplate_handler_enqueue_task,
	.kp = {
		.symbol_name	= "activate_task",
	},
};

/*****************************************************************************/
/* MODULE FUNCTIONS - INIT TEMPLATE                                          */
/*****************************************************************************/
static int __init jprobe_init(void)
{
	int ret;
    my_kretprobe.kp.symbol_name = func_name_fair;
    ret = register_kretprobe(&my_kretprobe);
    if (ret < 0) {
        printk(KERN_INFO "register_kretprobe failed, returned %d\n",
                ret);
        return -1;
    }
    printk(KERN_INFO "Planted return probe at %s: %p\n",
            my_kretprobe.kp.symbol_name, my_kretprobe.kp.addr);


    rt_kretprobe.kp.symbol_name = func_name_rt;
    ret = register_kretprobe(&rt_kretprobe);
    if (ret < 0) {
        printk(KERN_INFO "register_kretprobe failed, returned %d\n",
                ret);
        return -1;
    }
    printk(KERN_INFO "Planted return probe at %s: %p\n",
            rt_kretprobe.kp.symbol_name, rt_kretprobe.kp.addr);

//ENQUEUE_TASK
	ret = register_jprobe(&t_jprobe_enqueue_task);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}

	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"enqueue_task", t_jprobe_enqueue_task.kp.addr, t_jprobe_enqueue_task.entry);

//DEQUEUE_TASK
	ret = register_jprobe(&t_jprobe_dequeue_task);
	if(ret < 0) {
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}

	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"dequeue_task", t_jprobe_dequeue_task.kp.addr, t_jprobe_dequeue_task.entry);

//IRQ_HANDLER_ENTRY
	//ret = register_jprobe(&t_jprobe_hrtimer_wakeup);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}

	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"hrtimer_wakeup", t_jprobe_hrtimer_wakeup.kp.addr, t_jprobe_hrtimer_wakeup.entry);

#if 0
//IRQ_HANDLER_EXIT
	ret = register_jprobe(&t_jprobe_nanosleep_entry);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}

	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"nano_sleep", t_jprobe_nanosleep_entry.kp.addr, t_jprobe_nanosleep_entry.entry);
#endif



#if 1
//TIMER_ENTRY //INT
	ret = register_jprobe(&t_jprobe_timer_entry);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}
	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"timer_entry", t_jprobe_timer_entry.kp.addr, t_jprobe_timer_entry.entry);
#endif

#if 0
//CS_ENTRY
	ret = register_jprobe(&t_jprobe_cs_entry);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}
	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"cs_entry", (kprobe_opcode_t*)kallsyms_lookup_name("context_switch"), t_jprobe_cs_entry.entry);
#endif

//scheduler_tick_ENTRY
	ret = register_jprobe(&t_jprobe_scheduler_tick_entry);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}
	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"scheduler_tick_entry", t_jprobe_scheduler_tick_entry.kp.addr, t_jprobe_scheduler_tick_entry.entry);

#if 0
//wakeup_ENTRY
	ret = register_jprobe(&t_jprobe_wakeup_entry);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}
	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
		"wakeup_entry", t_jprobe_wakeup_entry.kp.addr, t_jprobe_wakeup_entry.entry);
#endif
///////////// END JPROBE
	return 0;
}

static void __exit jprobe_exit(void)
{

    unregister_kretprobe(&my_kretprobe);
    printk(KERN_INFO "kretprobe at %p unregistered\n",
            my_kretprobe.kp.addr);
    printk(KERN_INFO "Missed probing %d instances of %s\n",
        my_kretprobe.nmissed, my_kretprobe.kp.symbol_name);
	
    unregister_kretprobe(&rt_kretprobe);
    printk(KERN_INFO "kretprobe at %p unregistered\n",
            rt_kretprobe.kp.addr);
    printk(KERN_INFO "Missed probing %d instances of %s\n",
        rt_kretprobe.nmissed, rt_kretprobe.kp.symbol_name);
	
//enqueue_task
	unregister_jprobe(&t_jprobe_enqueue_task);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"enqueue_task", t_jprobe_enqueue_task.kp.addr);

//dequeue_task
	unregister_jprobe(&t_jprobe_dequeue_task);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n", 			
        "dequeue_task",t_jprobe_dequeue_task.kp.addr);

//hrtimer_wakeup
	unregister_jprobe(&t_jprobe_hrtimer_wakeup);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"hrtimer_wakeup", t_jprobe_hrtimer_wakeup.kp.addr);

//nanosleep
	unregister_jprobe(&t_jprobe_nanosleep_entry);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"nanosleep", t_jprobe_nanosleep_entry.kp.addr);

#if 1
//timer_entry
	unregister_jprobe(&t_jprobe_timer_entry);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"timer_entry",t_jprobe_timer_entry.kp.addr);
#endif
//cs_entry
	unregister_jprobe(&t_jprobe_cs_entry);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"cs_entry",t_jprobe_cs_entry.kp.addr);

//scheduler_tick_entry
	unregister_jprobe(&t_jprobe_scheduler_tick_entry);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"scheduler_tick_entry",t_jprobe_scheduler_tick_entry.kp.addr);

//wakeup_entry
	unregister_jprobe(&t_jprobe_wakeup_entry);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",
		"wakeup_entry",t_jprobe_wakeup_entry.kp.addr);
}

/*****************************************************************************/
/* MODULE FUNCTIONS - INIT TEMPLATE                                          */
/*****************************************************************************/
/*static int __init jprobe_init(void){
	int ret;
	
	ret = register_jprobe(&t_jprobe);
	if( ret < 0 ){
		printk(KERN_INFO ">> Registering jprobe FAILED, returned %d\n", ret);
		return -1;
	}

	printk(KERN_INFO ">> JProbe planted (REGISTERED) for %s, Address: %p, handler Addr: %p\n",
			FUNC_NAME, t_jprobe.kp.addr, t_jprobe.entry);
	return 0;
}

static void __exit jprobe_exit(void){
	unregister_jprobe(&t_jprobe);
	printk(KERN_INFO ">> JProbe %s, Addr: %p UNREGISTERED\n",FUNC_NAME,t_jprobe.kp.addr);
}
*/
module_init(jprobe_init);
module_exit(jprobe_exit);
MODULE_LICENSE("GPL");

