/* Lottery Scheduler Class File
 *
 * 
 *
 */
#include <linux/sched.h>
#include <linux/random.h>
static const struct sched_class lottery_sched_class;

//GLOBALS
unsigned long long total_ticket = 0;
struct lottery_event_log lottery_event_log;
unsigned long long lucky_ticket = 0;
unsigned long long lucky_ticket2 = 0;

//PROC EVENT LOG FUNCTIONALITY
struct lottery_event_log *get_lottery_event_log(void){
	return &lottery_event_log;
}//END get_lottery_event_log

void init_lottery_event_log(void){
	char msg[LOTTERY_MSG_SIZE];
	lottery_event_log.lines = lottery_event_log.cursor=0;

	
    snprintf( msg, LOTTERY_MSG_SIZE, "init_lottery_event_log:(%lu:%lu)",
                            lottery_event_log.lines, lottery_event_log.cursor);
	register_lottery_event(sched_clock(), msg, LOTTERY_MSG);

}//END init_lottery_event_log

void register_lottery_event(unsigned long long time, char *message, int EVENT){
	if(lottery_event_log.lines < LOTTERY_MAX_EVENT_LINES){
		lottery_event_log.lottery_event[lottery_event_log.lines].action=EVENT;
		lottery_event_log.lottery_event[lottery_event_log.lines].timestamp=time;
		strncpy(lottery_event_log.lottery_event[lottery_event_log.lines].msg, message, LOTTERY_MSG_SIZE-1);
		++lottery_event_log.lines;
	}else{
		printk(KERN_ALERT "register_lottery_event: full!\n");
	}
}//END register_lottery_event


// LOTTERY SCHED CLASS FUNCTIONS
/*
 *lottery tasks and lottery rq
 */
void init_lottery_rq(struct lottery_rq *lottery_rq)
{
#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY 
    lottery_rq->lottery_rb_root = RB_ROOT;
#else
	INIT_LIST_HEAD(&lottery_rq->lottery_list_head);
#endif
	atomic_set(&lottery_rq->nr_running,0);
}

#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY                                                                                                          
void update_postorder(struct rb_node *node)
{
    struct lottery_task *p = NULL;
    struct lottery_task *left = NULL, *right = NULL;
    if (node) {
        p = rb_entry(node, struct lottery_task, lottery_rb_node);
        update_postorder(node->rb_left);
        update_postorder(node->rb_right);
        if(node->rb_left && node->rb_right) {
            left = rb_entry(node->rb_left, struct lottery_task, lottery_rb_node);
            right = rb_entry(node->rb_right, struct lottery_task, lottery_rb_node);
            p->accu_ticket = left->accu_ticket + right->accu_ticket + p->ticket; 
        }
        else if(node->rb_left) {
            left = rb_entry(node->rb_left, struct lottery_task, lottery_rb_node);
            p->accu_ticket = left->accu_ticket + p->ticket; 
        }    
        else if (node->rb_right) {
            right = rb_entry(node->rb_right, struct lottery_task, lottery_rb_node);
            p->accu_ticket = right->accu_ticket + p->ticket; 
        }
        else {
            p->accu_ticket = p->ticket;
        }
        //printk("%llu ", p->ticket);
    }
}

void recalc_accu_lottery(struct lottery_rq *rq)
{
    struct rb_node **node=NULL;
    node = &rq->lottery_rb_root.rb_node;

    //printk("postorder: ");
    update_postorder(*node);
    //printk("\n");
}

void remove_lottery_rb_task(struct lottery_rq *rq, struct lottery_task *p)
{
    rb_erase(&p->lottery_rb_node, &rq->lottery_rb_root);
    p->lottery_rb_node.rb_left = p->lottery_rb_node.rb_right = NULL;
    recalc_accu_lottery(rq);
}

void insert_lottery_rb_task(struct lottery_rq *rq, struct lottery_task *p)
{
    struct rb_node **node=NULL;
    struct rb_node *parent=NULL;
    struct lottery_task *entry=NULL;
    node=&rq->lottery_rb_root.rb_node;
    while(*node!=NULL){
        parent=*node;
        entry=rb_entry(parent, struct lottery_task, lottery_rb_node);
        if(entry){
            if(p->ticket < entry->ticket){
                node=&parent->rb_left;
            }else{
                node=&parent->rb_right;
            }
        }
    }
    rb_link_node(&p->lottery_rb_node, parent,node);
    rb_insert_color(&p->lottery_rb_node, &rq->lottery_rb_root);
    recalc_accu_lottery(rq);
}

struct lottery_task* pick_winner_rb_task(struct lottery_rq *rq)
{
    struct rb_node *node = NULL;
    struct lottery_task *p = NULL;
    struct lottery_task *left = NULL, *right = NULL;
    unsigned long long total_so_far = 0, left_total = 0;
	
    char msg[LOTTERY_MSG_SIZE];
	unsigned long long start = sched_clock();
    if(!total_ticket)
        return NULL;
    lucky_ticket2 = (get_random_int() % total_ticket)+1;
    
    node = rq->lottery_rb_root.rb_node;
    while(node) {
        // check subtree
        total_so_far = 0;
        p = rb_entry(node, struct lottery_task, lottery_rb_node);

        // check left
        if(node->rb_left) {
            left = rb_entry(node->rb_left, struct lottery_task, lottery_rb_node);
            if( lucky_ticket2 <= (left->accu_ticket + left_total)) { // jump to left sub-tree
                node = node->rb_left;
                continue;
            }
            total_so_far += left->accu_ticket;
        }

        // check sub-root
        total_so_far += p->ticket;
        if( lucky_ticket2 <= total_so_far + left_total) {
            snprintf( msg, LOTTERY_MSG_SIZE, "WinnerPid:%d luckt:%llu t:%llu  tt:%llu TIME:%llu", 
                    p->task->pid, lucky_ticket2, p->task->ticket, total_ticket, sched_clock()-start);
            register_lottery_event(sched_clock(), msg, LOTTERY_WIN);
            return p;
        }

        // jump to right sub-tree
        if(node->rb_right) {
            right = rb_entry(node->rb_right, struct lottery_task, lottery_rb_node);
            left_total += total_so_far;
            node = node->rb_right;
        }
        else {
            // not found
            return NULL;
        }
    }
    return NULL;
}
#endif


#ifndef CONFIG_SCHED_LOTTERY_RB_POLICY 
void remove_lottery_task(struct lottery_task *p)
{
	list_del(&p->lottery_list_node);
}

void insert_lottery_task(struct lottery_rq *rq, struct lottery_task *p)
{
	list_add_tail(&p->lottery_list_node, &rq->lottery_list_head);
}

struct lottery_task* pick_winner_task(struct lottery_rq *_lottery_rq)
{
    struct lottery_task *t=NULL;
	unsigned long long total_so_far = 0;
	char msg[LOTTERY_MSG_SIZE];
	unsigned long long start = sched_clock();
    if(!total_ticket)
        return NULL;
    lucky_ticket = (get_random_int() % total_ticket)+1;
    list_for_each_entry(t, &_lottery_rq->lottery_list_head, lottery_list_node) {
        if(t){              
            total_so_far += t->task->ticket;
            if(total_so_far >= lucky_ticket){
                snprintf( msg, LOTTERY_MSG_SIZE, "WinnerPid:%d luckt:%llu t:%llu tt:%llu TIME:%llu", 
                    t->task->pid, lucky_ticket, t->task->ticket, total_ticket, sched_clock()-start);
                register_lottery_event(sched_clock(), msg, LOTTERY_WIN);
                return t;
            }
        }

    }
    return NULL;
}
#endif

struct lottery_task * who_is_winner(struct lottery_rq *lottery_rq)
{
#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY 
	lucky_ticket2 = 0;
    return pick_winner_rb_task(lottery_rq); 
#else
	lucky_ticket = 0;
    return pick_winner_task(lottery_rq); 
#endif    
}

static void check_preempt_curr_lottery(struct rq *rq, struct task_struct *p, int flags)
{
	struct lottery_task *t=NULL;
	if(rq->curr->policy!=SCHED_LOTTERY){
		resched_task(rq->curr);
	}
	else {
		t = who_is_winner(&rq->lottery_rq);
		if(t) {
			if(p != t->task) {
				resched_task(t->task);
			}
		} else {
            // happened frequently
			//printk(KERN_ALERT "WARN check_preempt_curr_lottery pid %d \n", p->pid);
		}
	}
}

static struct task_struct *pick_next_task_lottery(struct rq *rq)
{
	char msg[LOTTERY_MSG_SIZE];
	struct lottery_task *t=NULL;
	
	if(total_ticket == 0)
		return NULL;
    
	t = who_is_winner(&rq->lottery_rq);
	if(t) {
		snprintf( msg, LOTTERY_MSG_SIZE, "PICK prevPID:(%d:%llu) nextPID(%d:%llu) LUCK:%llu",
                                                rq->curr->pid, rq->curr->ticket,
#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY 
                                                t->task->pid, t->task->ticket, lucky_ticket);
#else
                                                t->task->pid, t->task->ticket, lucky_ticket2);
#endif
		register_lottery_event(sched_clock(), msg, LOTTERY_MSG);
		return t->task;
	}
	return NULL;
}

static void enqueue_task_lottery(struct rq *rq, struct task_struct *p, int wakeup, bool head)
{
	char msg[LOTTERY_MSG_SIZE];
	unsigned long long start = sched_clock();

	if(p) {
#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY 
        insert_lottery_rb_task(&rq->lottery_rq, &p->lottery_task);
#else
        insert_lottery_task(&rq->lottery_rq, &p->lottery_task);
#endif
        total_ticket += p->ticket;
		atomic_inc(&rq->lottery_rq.nr_running);

		snprintf( msg, LOTTERY_MSG_SIZE, "EN pid:%d t:%llu tt:%llu TIME:%llu",
                                p->pid,p->ticket,total_ticket, sched_clock()-start);
		register_lottery_event(sched_clock(), msg, LOTTERY_ENQUEUE);
	} else {
		printk(KERN_ALERT "NULL enqueue_task_lottery\n");
	}
}

static void dequeue_task_lottery(struct rq *rq, struct task_struct *p, int sleep)
{
	char msg[LOTTERY_MSG_SIZE];
	unsigned long long start = sched_clock();

	if(p){
#ifdef CONFIG_SCHED_LOTTERY_RB_POLICY 
        remove_lottery_rb_task(&rq->lottery_rq, &p->lottery_task);
#else
        remove_lottery_task(&p->lottery_task);
#endif
        total_ticket -= p->ticket;
		atomic_dec(&rq->lottery_rq.nr_running);
		snprintf( msg, LOTTERY_MSG_SIZE, "DE pid:%d t:%llu tt:%llu TIME:%llu",p->pid,p->ticket,total_ticket,sched_clock()-start);
		register_lottery_event(sched_clock(), msg, LOTTERY_DEQUEUE);
		
		if(p->state==TASK_DEAD || p->state==EXIT_DEAD || p->state==EXIT_ZOMBIE) {
		    //printk("DONE pid:%d t:%llu\n", p->pid, p->ticket);
		    //	rem_lottery_task_list(&rq->lottery_rq,t->task);
			snprintf( msg, LOTTERY_MSG_SIZE, "DONE pid:%d t:%llu", p->pid, p->ticket);
			register_lottery_event(sched_clock(), msg, LOTTERY_TASK_DONE);
		}
	} else {
		printk(KERN_ALERT "NULL dequeue_task_lottery\n");
	}

}

static void put_prev_task_lottery(struct rq *rq, struct task_struct *prev)
{

}

#ifdef CONFIG_SMP
static unsigned long load_balance_lottery(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	return 0;
}

static int move_one_task_lottery(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	return 0;
}
#endif

static void task_tick_lottery(struct rq *rq, struct task_struct *p, int queued)
{
	check_preempt_curr_lottery(rq, p, 0);
}

static void set_curr_task_lottery(struct rq *rq)
{

}

/*
 * When switching a task to RT, we may overload the runqueue
 * with RT tasks. In this case we try to push them off to
 * other runqueues.
 */
static void switched_to_lottery(struct rq *rq, struct task_struct *p,
                           int running)
{
        /*
         * If we are already running, then there's nothing
         * that needs to be done. But if we are not running
         * we may need to preempt the current running task.
         * If that current running task is also an RT task
         * then see if we can move to another run queue.
         */
}


unsigned int get_rr_interval_lottery(struct rq *rq, struct task_struct *task)
{
	/*
         * Time slice is 0 for SCHED_FIFO tasks
         */
        if (task->policy == SCHED_RR)
                return DEF_TIMESLICE;
        else
                return 0;
}

static void yield_task_lottery(struct rq *rq)
{

}


/*
 * Priority of the task has changed. This may cause
 * us to initiate a push or pull.
 */
static void prio_changed_lottery(struct rq *rq, struct task_struct *p,
			    int oldprio, int running)
{
    int diff;
#ifndef CONFIG_SCHED_LOTTERY_RB_POLICY 
	struct lottery_task *t=NULL;
#endif

    printk("%s(): Jack: p->pid %d ticket from %llu to %u", 
                        __func__, p->pid, p->ticket, p->rt_priority);

    diff = p->rt_priority - p->ticket;
    p->ticket = p->rt_priority;
#ifndef CONFIG_SCHED_LOTTERY_RB_POLICY 
	list_for_each_entry(t, &rq->lottery_rq.lottery_list_head, lottery_list_node) {
        if(t) {  
		    total_ticket += t->task->ticket;
		    if(p == t->task) {
                total_ticket += diff;
		    }
        }
    }
#endif

}

static int select_task_rq_lottery(struct rq *rq, struct task_struct *p, int sd_flag, int flags)
{
//	struct rq *rq = task_rq(p);
	if (sd_flag != SD_BALANCE_WAKE)
		return smp_processor_id();

	return task_cpu(p);
}


static void set_cpus_allowed_lottery(struct task_struct *p,
				const struct cpumask *new_mask)
{

}

/* Assumes rq->lock is held */
static void rq_online_lottery(struct rq *rq)
{

}

/* Assumes rq->lock is held */
static void rq_offline_lottery(struct rq *rq)
{

}

static void pre_schedule_lottery(struct rq *rq, struct task_struct *prev)
{

}

static void post_schedule_lottery(struct rq *rq)
{

}
/*
 * If we are not running and we are not going to reschedule soon, we should
 * try to push tasks away now
 */
static void task_woken_lottery(struct rq *rq, struct task_struct *p)
{
/*        if (!task_running(rq, p) &&
            !test_tsk_need_resched(rq->curr) &&
            has_pushable_tasks(rq) &&
            p->rt.nr_cpus_allowed > 1)
                push_rt_tasks(rq);
*/
}

/*
 * When switch from the rt queue, we bring ourselves to a position
 * that we might want to pull RT tasks from other runqueues.
 */
static void switched_from_lottery(struct rq *rq, struct task_struct *p,
			   int running)
{

}

/*
 * Simple, special scheduling class for the per-CPU lottery tasks:
 */
static const struct sched_class lottery_sched_class = {
    .next 			= &fair_sched_class,
	.enqueue_task		= enqueue_task_lottery,
	.dequeue_task		= dequeue_task_lottery,

	.check_preempt_curr	= check_preempt_curr_lottery,

	.pick_next_task		= pick_next_task_lottery,
	.put_prev_task		= put_prev_task_lottery,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_lottery,
	.move_one_task		= move_one_task_lottery,

	.select_task_rq		= select_task_rq_lottery,
	.set_cpus_allowed       = set_cpus_allowed_lottery,
	.rq_online              = rq_online_lottery,
	.rq_offline             = rq_offline_lottery,
	.pre_schedule		= pre_schedule_lottery,
	.post_schedule		= post_schedule_lottery,
	.task_woken		= task_woken_lottery,
	.switched_from		= switched_from_lottery,
#endif

	.set_curr_task          = set_curr_task_lottery,
	.task_tick		= task_tick_lottery,

	.switched_to		= switched_to_lottery,

	.yield_task		= yield_task_lottery,
	.get_rr_interval	= get_rr_interval_lottery,

	.prio_changed		= prio_changed_lottery,
};
