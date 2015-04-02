// Created by Cambridge and Xin Tong

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>

#include <trace/events/sched.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#ifdef CONFIG_HMP_FREQUENCY_INVARIANT_SCALE
/* Include cpufreq header to add a notifier so that cpu frequency
 * scaling can track the current CPU frequency
 */
#include <linux/cpufreq.h>
#endif /* CONFIG_HMP_FREQUENCY_INVARIANT_SCALE */
#ifdef CONFIG_SCHED_HMP
#include <linux/cpuidle.h>
#endif

#include "sched.h"
const struct sched_class mycfs_sched_class;
/*
 * mycfs-task scheduling class.
 *
 * (NOTE: these are not related to SCHED_MYCFS tasks which are
 *  handled in sched/fair.c)
 */

#if BITS_PER_LONG == 32
# define WMULT_CONST	(~0UL)
#else
# define WMULT_CONST	(1UL << 32)
#endif
#define WMULT_SHIFT	32
/*
 * Shift right and round:
 */
#define SRR(x, y) (((x) + (1UL << ((y) - 1))) >> (y))

#define TOTAL   100
#define RATIO   100
#define BASE    1000000ULL

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 10;

static inline struct task_struct *task_of(struct sched_entity *se);
static inline struct rq *rq_of(struct mycfs_rq *mycfs_rq);
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp);
static inline struct mycfs_rq *task_cfs_rq(struct task_struct *p);
static inline struct mycfs_rq *cfs_rq_of(struct sched_entity *se);
static inline struct sched_entity *parent_entity(struct sched_entity *se);
static void check_preempt_curr_mycfs(struct rq *rq, struct task_struct *p, int flags);
static unsigned long
calc_delta_mine(unsigned long delta_exec, unsigned long weight, struct load_weight *lw);
static inline unsigned long calc_delta_fair(unsigned long delta, struct sched_entity *se);
static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime);
static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime);
static void update_min_vruntime(struct mycfs_rq *mycfs_rq);
static u64 __sched_period(unsigned long nr_running);
static u64 sched_slice(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static u64 sched_vslice(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static inline void __update_curr(struct mycfs_rq *mycfs_rq, struct sched_entity *curr, unsigned long delta_exec);
static void update_curr(struct mycfs_rq *mycfs_rq);
static inline void update_stats_wait_start(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void update_stats_enqueue(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void account_entity_enqueue(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void place_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int initial);
static void __enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags);
static inline int cfs_rq_throttled(struct mycfs_rq *mycfs_rq);
static void enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags);
static inline void update_stats_dequeue(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void update_stats_wait_end(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void account_entity_dequeue(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags);
static void dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags);
static void put_prev_task_mycfs(struct rq *rq, struct task_struct *prev);
static void task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued);
static void set_curr_task_mycfs(struct rq *rq);
static void switched_to_mycfs(struct rq *rq, struct task_struct *p);
static void prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio);
static void task_fork_mycfs(struct task_struct *p);
static unsigned int get_rr_interval_mycfs(struct rq *rq, struct task_struct *task);
static struct sched_entity *pick_next_entity(struct mycfs_rq *mycfs_rq);
static void __dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static inline void update_stats_curr_start(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static void set_next_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se);
static struct task_struct *pick_next_task_mycfs(struct rq *rq);
static void check_preempt_tick(struct mycfs_rq *mycfs_rq, struct sched_entity *curr);
static void entity_tick(struct mycfs_rq *mycfs_rq, struct sched_entity *curr, int queued);
void init_mycfs_rq(struct mycfs_rq *mycfs_rq);
static void AutoSort(struct mycfs_rq *mycfs_rq);
void check_task_mycfs(struct rq *rq);
static struct sched_entity *__pick_next_entity(struct sched_entity *se);
static struct sched_entity *__pick_first_entity_mycfs(struct mycfs_rq *mycfs_rq);
static inline int entity_before(struct sched_entity *a, struct sched_entity *b);

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

static inline struct task_struct *task_of(struct sched_entity *se)
{
    return container_of(se, struct task_struct, se);
}

static inline struct rq *rq_of(struct mycfs_rq *mycfs_rq)
{
    return container_of(mycfs_rq, struct rq, mycfs);
}

#define entity_is_task(se)	1

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
    return NULL;
}

#define for_each_sched_entity(se) \
        for (; se; se = NULL)

static inline struct mycfs_rq *task_cfs_rq(struct task_struct *p)
{
    return &task_rq(p)->mycfs;
}

static inline struct mycfs_rq *cfs_rq_of(struct sched_entity *se)
{
    struct task_struct *p = task_of(se);
    struct rq *rq = task_rq(p);
    
    return &rq->mycfs;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
    return NULL;
}

/*
 * Idle tasks are unconditionally rescheduled:
 */
static void check_preempt_curr_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
    return;
}

/*
 * delta *= weight / lw
 */
static unsigned long
calc_delta_mine(unsigned long delta_exec, unsigned long weight,
                struct load_weight *lw)
{
    u64 tmp;
    
    /*
     * weight can be less than 2^SCHED_LOAD_RESOLUTION for task group sched
     * entities since MIN_SHARES = 2. Treat weight as 1 if less than
     * 2^SCHED_LOAD_RESOLUTION.
     */
    if (likely(weight > (1UL << SCHED_LOAD_RESOLUTION)))
        tmp = (u64)delta_exec * scale_load_down(weight);
    else
        tmp = (u64)delta_exec;
    
    if (!lw->inv_weight) {
        unsigned long w = scale_load_down(lw->weight);
        
        if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
            lw->inv_weight = 1;
        else if (unlikely(!w))
            lw->inv_weight = WMULT_CONST;
        else
            lw->inv_weight = WMULT_CONST / w;
    }
    
    /*
     * Check whether we'd overflow the 64-bit multiplication:
     */
    if (unlikely(tmp > WMULT_CONST))
        tmp = SRR(SRR(tmp, WMULT_SHIFT/2) * lw->inv_weight,
                  WMULT_SHIFT/2);
    else
        tmp = SRR(tmp * lw->inv_weight, WMULT_SHIFT);
    
    return (unsigned long)min(tmp, (u64)(unsigned long)LONG_MAX);
}

/*
 * delta /= w
 */
static inline unsigned long
calc_delta_fair(unsigned long delta, struct sched_entity *se)
{
    if (unlikely(se->load.weight != NICE_0_LOAD))
        delta = calc_delta_mine(delta, NICE_0_LOAD, &se->load);
    
    return delta;
}

static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
    s64 delta = (s64)(vruntime - max_vruntime);
    if (delta > 0)
        max_vruntime = vruntime;
    
    return max_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
    s64 delta = (s64)(vruntime - min_vruntime);
    if (delta < 0)
        min_vruntime = vruntime;
    
    return min_vruntime;
}

static void update_min_vruntime(struct mycfs_rq *mycfs_rq)
{
    u64 vruntime = mycfs_rq->min_vruntime;
    
    if (mycfs_rq->curr)
        vruntime = mycfs_rq->curr->vruntime;
    
// we replace this part with array scheduler
    if (mycfs_rq->rb_leftmost) {
        struct sched_entity *se = rb_entry(mycfs_rq->rb_leftmost,
                                           struct sched_entity,
                                           run_node);
        
        if (!mycfs_rq->curr)
            vruntime = se->vruntime;
        else
            vruntime = min_vruntime(vruntime, se->vruntime);
    }
    
    /* ensure we never gain time by being placed backwards. */
    mycfs_rq->min_vruntime = max_vruntime(mycfs_rq->min_vruntime, vruntime);
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static u64 __sched_period(unsigned long nr_running)
{
    u64 period = sysctl_sched_latency;
    unsigned long nr_latency = sched_nr_latency;
    
    if (unlikely(nr_running > nr_latency)) {
        period = sysctl_sched_min_granularity;
        period *= nr_running;
    }
    
    return period;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
static u64 sched_slice(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    u64 slice = __sched_period(mycfs_rq->nr_running + !se->on_rq);
    
    for_each_sched_entity(se) {
        struct load_weight *load;
        struct load_weight lw;
        
        mycfs_rq = cfs_rq_of(se);
        load = &mycfs_rq->load;
        
        if (unlikely(!se->on_rq)) {
            lw = mycfs_rq->load;
            
            update_load_add(&lw, se->load.weight);
            load = &lw;
        }
        slice = calc_delta_mine(slice, se->load.weight, load);
    }
    return slice;
}

/*
 * We calculate the vruntime slice of a to-be-inserted task.
 *
 * vs = s/w
 */
static u64 sched_vslice(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    return calc_delta_fair(sched_slice(mycfs_rq, se), se);
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr(struct mycfs_rq *mycfs_rq, struct sched_entity *curr,
              unsigned long delta_exec)
{
    unsigned long delta_exec_weighted;
    
    schedstat_set(curr->statistics.exec_max,
                  max((u64)delta_exec, curr->statistics.exec_max));
    
    curr->sum_exec_runtime += delta_exec;
    schedstat_add(mycfs_rq, exec_clock, delta_exec);
    delta_exec_weighted = calc_delta_fair(delta_exec, curr);
    
    curr->vruntime += delta_exec_weighted;
    update_min_vruntime(mycfs_rq);
}

static void update_curr(struct mycfs_rq *mycfs_rq)
{
    struct sched_entity *curr = mycfs_rq->curr;
    u64 now = rq_of(mycfs_rq)->clock_task;
    unsigned long delta_exec;
    
    if (unlikely(!curr))
        return;
    
    /*
     * Get the amount of time the current task was running
     * since the last time we changed load (this cannot
     * overflow on 32 bits):
     */
    delta_exec = (unsigned long)(now - curr->exec_start);
    if (!delta_exec)
        return;
    
    __update_curr(mycfs_rq, curr, delta_exec);
    curr->exec_start = now;
    
    if (entity_is_task(curr)) {
        struct task_struct *curtask = task_of(curr);
        
        trace_sched_stat_runtime(curtask, delta_exec, curr->vruntime);
        cpuacct_charge(curtask, delta_exec);
        account_group_exec_runtime(curtask, delta_exec);
    }
}

static inline void
update_stats_wait_start(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    schedstat_set(se->statistics.wait_start, rq_of(mycfs_rq)->clock);
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    /*
     * Are we enqueueing a waiting task? (for current tasks
     * a dequeue/enqueue event is a NOP)
     */
    if (se != mycfs_rq->curr)
        update_stats_wait_start(mycfs_rq, se);
}

static void
account_entity_enqueue(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    update_load_add(&mycfs_rq->load, se->load.weight);
    if (!parent_entity(se))
        update_load_add(&rq_of(mycfs_rq)->load, se->load.weight);

    mycfs_rq->nr_running++;
//    printk(KERN_EMERG "this from account_entity_enqueue, mycfs enqueue has %d tasks!\n", mycfs_rq->nr_running);
    
#ifdef CONFIG_SCHEDSTATS
    printk(KERN_EMERG "CONFIG_SCHEDSTATS!\n");
#endif
    
#ifdef CONFIG_SCHED_DEBUG
    printk(KERN_EMERG "CONFIG_SCHED_DEBUG!\n");
#endif
    
#ifdef CONFIG_FAIR_GROUP_SCHED
    printk(KERN_EMERG "CONFIG_FAIR_GROUP_SCHED!\n");
#endif
    
#ifdef CONFIG_CFS_BANDWIDTH
    printk(KERN_EMERG "CONFIG_CFS_BANDWIDTH!\n");
#endif
    
#ifdef CONFIG_SMP
    printk(KERN_EMERG "CONFIG_SMP!\n");
#endif
    
#ifdef CONFIG_SCHED_HMP
    printk(KERN_EMERG "CONFIG_HMP_FREQUENCY_INVARIANT_SCALE!\n");
#endif
    
#ifdef CONFIG_SCHED_HRTICK
    printk(KERN_EMERG "CONFIG_SCHED_HRTICK!\n");
#endif
    
#ifdef CONFIG_CPU_IDLE
    printk(KERN_EMERG "CONFIG_CPU_IDLE!\n");
#endif
    
#ifdef CONFIG_PREEMPT
    printk(KERN_EMERG "CONFIG_SCHED_HRTICK!\n");
#endif
}

static void
place_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int initial)
{
    u64 vruntime = mycfs_rq->min_vruntime;
    
    /*
     * The 'current' period is already promised to the current tasks,
     * however the extra weight of the new task will slow them down a
     * little, place the new task so that it fits in the slot that
     * stays open at the end.
     */
    if (initial && sched_feat(START_DEBIT))
        vruntime += sched_vslice(mycfs_rq, se);
    
    /* sleeps up to a single latency don't count. */
    if (!initial) {
        unsigned long thresh = sysctl_sched_latency;
        
        /*
         * Halve their sleep time's effect, to allow
         * for a gentler effect of sleepers:
         */
        if (sched_feat(GENTLE_FAIR_SLEEPERS))
            thresh >>= 1;
        
        vruntime -= thresh;
    }
    
    /* ensure we never gain time by being placed backwards. */
    se->vruntime = max_vruntime(se->vruntime, vruntime);
}

static inline int entity_before(struct sched_entity *a, struct sched_entity *b)
{
//    return (s64)(a->vruntime - b->vruntime) < 0;
    return (s64)(task_of(a)->quit_time - task_of(b)->quit_time) < 0;
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
//    printk(KERN_EMERG "__enqueue_entity!\n");
    
    struct rb_node **link = &mycfs_rq->tasks_timeline.rb_node;
    struct rb_node *parent = NULL;
    struct sched_entity *entry;
    int leftmost = 1;
   
    struct rb_node *next;
    if(mycfs_rq->rb_leftmost)
    {
        next = mycfs_rq->rb_leftmost;
        while(next)
        {
            entry = rb_entry(next, struct sched_entity, run_node);
            if (entry == se)
            {
                return;
            }
            next = rb_next(&entry->run_node);
        }
    }
    /*
     * Find the right place in the rbtree:
     */
    while (*link) {
        parent = *link;
        entry = rb_entry(parent, struct sched_entity, run_node);
        /*
         * We dont care about collisions. Nodes with
         * the same key stay together.
         */
        if (entity_before(se, entry)) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
            leftmost = 0;
        }
    }
    
    /*
     * Maintain a cache of leftmost tree entries (it is frequently
     * used):
     */
    if (leftmost)
    {
//        printk(KERN_EMERG "we have a leftmost!\n");
        mycfs_rq->rb_leftmost = &se->run_node;
    } else {
//        printk(KERN_EMERG "we do not have a leftmost!\n");
    }
    rb_link_node(&se->run_node, parent, link);
    rb_insert_color(&se->run_node, &mycfs_rq->tasks_timeline);
}

static void
enqueue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags)
{
    /*
     * Update the normalized vruntime before updating min_vruntime
     * through callig update_curr().
     */
    if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
        se->vruntime += mycfs_rq->min_vruntime;
    
    /*
     * Update run-time statistics of the 'current'.
     */
    update_curr(mycfs_rq);
    account_entity_enqueue(mycfs_rq, se);
    
    if (flags & ENQUEUE_WAKEUP) {
        place_entity(mycfs_rq, se, 0);
    }
    
    update_stats_enqueue(mycfs_rq, se);
    if (se != mycfs_rq->curr)
    {
//        printk(KERN_EMERG "enqueue_entity -> __enqueue_entity!\n");
        __enqueue_entity(mycfs_rq, se);
    }
    se->on_rq = 1;
}

static inline int cfs_rq_throttled(struct mycfs_rq *mycfs_rq)
{
    return 0;
}

static void
enqueue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
    struct mycfs_rq *mycfs_rq;
    struct sched_entity *se = &p->se;
    
    for_each_sched_entity(se) {
        if (se->on_rq)
            break;
        mycfs_rq = cfs_rq_of(se);
        enqueue_entity(mycfs_rq, se, flags);
        
        /*
         * end evaluation on encountering a throttled cfs_rq
         *
         * note: in the case of encountering a throttled cfs_rq we will
         * post the final 
         increment below.
         */
        if (cfs_rq_throttled(mycfs_rq))
            break;
        mycfs_rq->h_nr_running++;
        
        flags = ENQUEUE_WAKEUP;
    }
    
    for_each_sched_entity(se) {
        mycfs_rq = cfs_rq_of(se);
        mycfs_rq->h_nr_running++;
        
        if (cfs_rq_throttled(mycfs_rq))
            break;
    }
    
    if (!se) {
        inc_nr_running(rq);
    }
}

static inline void
update_stats_dequeue(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    /*
     * Mark the end of the wait period if dequeueing a
     * waiting task:
     */
    if (se != mycfs_rq->curr)
        update_stats_wait_end(mycfs_rq, se);
}

static void
update_stats_wait_end(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    schedstat_set(se->statistics.wait_max, max(se->statistics.wait_max,
                                               rq_of(mycfs_rq)->clock - se->statistics.wait_start));
    schedstat_set(se->statistics.wait_count, se->statistics.wait_count + 1);
    schedstat_set(se->statistics.wait_sum, se->statistics.wait_sum +
                  rq_of(mycfs_rq)->clock - se->statistics.wait_start);
    schedstat_set(se->statistics.wait_start, 0);
}

static void
account_entity_dequeue(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    update_load_sub(&mycfs_rq->load, se->load.weight);
    if (!parent_entity(se))
        update_load_sub(&rq_of(mycfs_rq)->load, se->load.weight);
    if (entity_is_task(se))
        list_del_init(&se->group_node);
    mycfs_rq->nr_running--;
//    printk(KERN_EMERG "this from account_entity_dequeue, mycfs enqueue has %d tasks!\n", mycfs_rq->nr_running);
}

static void
dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se, int flags)
{
    /*
     * Update run-time statistics of the 'current'.
     */
    update_curr(mycfs_rq);
    
    update_stats_dequeue(mycfs_rq, se);
    
//cam    clear_buddies(mycfs_rq, se);
    
    if (se != mycfs_rq->curr)
    {
        __dequeue_entity(mycfs_rq, se);
    }
    se->on_rq = 0;
    account_entity_dequeue(mycfs_rq, se);
    
    /*
     * Normalize the entity after updating the min_vruntime because the
     * update can refer to the ->curr item and we need to reflect this
     * movement in our normalized position.
     */
    if (!(flags & DEQUEUE_SLEEP))
        se->vruntime -= mycfs_rq->min_vruntime;
    
    update_min_vruntime(mycfs_rq);
}

/*
 * It is not legal to sleep in the idle task - print a warning
 * message if some code attempts to do it:
 */
static void
dequeue_task_mycfs(struct rq *rq, struct task_struct *p, int flags)
{
    struct mycfs_rq *mycfs_rq;
    struct sched_entity *se = &p->se;
    int task_sleep = flags & DEQUEUE_SLEEP;
    
    for_each_sched_entity(se) {
        mycfs_rq = cfs_rq_of(se);
        dequeue_entity(mycfs_rq, se, flags);
        
        /*
         * end evaluation on encountering a throttled cfs_rq
         *
         * note: in the case of encountering a throttled cfs_rq we will
         * post the final h_nr_running decrement below.
         */
        if (cfs_rq_throttled(mycfs_rq))
            break;
        mycfs_rq->h_nr_running--;
        
        /* Don't dequeue parent if it has other entities besides us */
        if (mycfs_rq->load.weight) {
            /*
             * Bias pick_next to pick a task from this cfs_rq, as
             * p is sleeping when it is within its sched_slice.
             */
//cam            if (task_sleep && parent_entity(se))
//cam                set_next_buddy(parent_entity(se));
            
            /* avoid re-evaluating load for this entity */
            se = parent_entity(se);
            break;
        }
        flags |= DEQUEUE_SLEEP;
    }
    
    for_each_sched_entity(se) {
        mycfs_rq = cfs_rq_of(se);
        mycfs_rq->h_nr_running--;
        
        if (cfs_rq_throttled(mycfs_rq))
            break;
    }
    
    if (!se) {
        dec_nr_running(rq);
    }
    dec_nr_running(rq);
}

static void put_prev_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *prev)
{
    /*
     * If still on the runqueue then deactivate_task()
     * was not called and update_curr() has to be done:
     */
    
    struct task_struct *p = task_of(prev);
    
    if (prev->on_rq)
        update_curr(mycfs_rq);
    
    if (prev->on_rq) {
        update_stats_wait_start(mycfs_rq, prev);
        /* Put 'current' back into the tree. */
//        printk(KERN_EMERG "put_prev_entity -> __enqueue_entity!\n");
        __enqueue_entity(mycfs_rq, prev);
        /* in !on_rq case, update occurred at dequeue */
//cam        update_entity_load_avg(prev, 1);
    }
    mycfs_rq->curr = NULL;
}

static void put_prev_task_mycfs(struct rq *rq, struct task_struct *prev)
{
    struct sched_entity *se = &prev->se;
    
    struct mycfs_rq *mycfs_rq;
    
    for_each_sched_entity(se) {
        mycfs_rq = cfs_rq_of(se);
        put_prev_entity(mycfs_rq, se);
    }
}

static void set_curr_task_mycfs(struct rq *rq)
{
}

static void switched_to_mycfs(struct rq *rq, struct task_struct *p)
{
    printk(KERN_EMERG "switched to mycfs scheduler!\n");
    p->sched_reset_on_fork = 0;
    p->limit = RATIO;
    p->quit_time = rq->clock_task;
    if (!p->se.on_rq)
        return;
    
    /*
     * We were most likely switched from sched_rt, so
     * kick off the schedule if running, otherwise just see
     * if we can still preempt the current task.
     */
    if (rq->curr == p)
        resched_task(rq->curr);
    else
        check_preempt_curr(rq, p, 0);
}

static void
prio_changed_mycfs(struct rq *rq, struct task_struct *p, int oldprio)
{
    BUG();
}

static void task_fork_mycfs(struct task_struct *p)
{
    struct mycfs_rq *mycfs_rq;
    struct sched_entity *se = &p->se, *curr;
    int this_cpu = smp_processor_id();
    struct rq *rq = this_rq();
    unsigned long flags;
    
    raw_spin_lock_irqsave(&rq->lock, flags);
    
    update_rq_clock(rq);
    
//    mycfs_rq = task_cfs_rq(current);
    mycfs_rq = cfs_rq_of(current);
    curr = mycfs_rq->curr;
    
    /*
     * Not only the cpu but also the task_group of the parent might have
     * been changed after parent->se.parent,cfs_rq were copied to
     * child->se.parent,cfs_rq. So call __set_task_cpu() to make those
     * of child point to valid ones.
     */
    rcu_read_lock();
    __set_task_cpu(p, this_cpu);
    rcu_read_unlock();
    
    update_curr(mycfs_rq);
    
    if (curr)
    {
        se->vruntime = curr->vruntime;
        p->limit = task_of(curr)->limit;
    }
    place_entity(mycfs_rq, se, 1);
    
    if (sysctl_sched_child_runs_first && curr && entity_before(curr, se)) {
        /*
         * Upon rescheduling, sched_class::put_prev_task() will place
         * 'current' within the tree based on its new key value.
         */
        swap(curr->vruntime, se->vruntime);
        resched_task(rq->curr);
    }
    
    se->vruntime -= mycfs_rq->min_vruntime;
    
    raw_spin_unlock_irqrestore(&rq->lock, flags);
}

struct sched_entity *__pick_first_entity_mycfs(struct mycfs_rq *mycfs_rq)
{
    struct rb_node *left = mycfs_rq->rb_leftmost;
    
    if (!left)
        return NULL;
    
    return rb_entry(left, struct sched_entity, run_node);
}

static unsigned int get_rr_interval_mycfs(struct rq *rq, struct task_struct *task)
{
    return 0;
}

static struct sched_entity *__pick_next_entity(struct sched_entity *se)
{
    struct rb_node *next = rb_next(&se->run_node);
    
    if (!next)
        return NULL;
    
    return rb_entry(next, struct sched_entity, run_node);
}

/*
 * Pick the next process, keeping these things in mind, in this order:
 * 1) keep things fair between processes/task groups
 * 2) pick the "next" process, since someone really wants that to run
 * 3) pick the "last" process, for cache locality
 * 4) do not run the "skip" process, if something else is available
 */
static struct sched_entity *pick_next_entity(struct mycfs_rq *mycfs_rq)
{
    struct sched_entity *se = __pick_first_entity_mycfs(mycfs_rq);
    
    return se;
}

static void __dequeue_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    if (mycfs_rq->rb_leftmost == &se->run_node) {
//        printk(KERN_EMERG "delete a node == curr!\n");
        struct rb_node *next_node;
        
        next_node = rb_next(&se->run_node);
        mycfs_rq->rb_leftmost = next_node;
    } else {
//        printk(KERN_EMERG "delete a node != curr!\n");
    }

    rb_erase(&se->run_node, &mycfs_rq->tasks_timeline);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    /*
     * We are starting a new run period:
     */
    se->exec_start = rq_of(mycfs_rq)->clock_task;
}

static void
set_next_entity(struct mycfs_rq *mycfs_rq, struct sched_entity *se)
{
    /* 'current' is not kept within the tree. */
    if (se->on_rq) {
        /*
         * Any task has to be enqueued before it get to execute on
         * a CPU. So account for the time it spent waiting on the
         * runqueue.
         */
        update_stats_wait_end(mycfs_rq, se);
        __dequeue_entity(mycfs_rq, se);
    }
    
    update_stats_curr_start(mycfs_rq, se);
    mycfs_rq->curr = se;
    
    se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static struct task_struct *pick_next_task_mycfs(struct rq *rq)
{
    struct task_struct *p;
    struct mycfs_rq *mycfs_rq = &rq->mycfs;
    struct sched_entity *se;
    
    int i;
    
    if (!mycfs_rq->nr_running)
        return NULL;
    
    se = pick_next_entity(mycfs_rq);
   
    if(!se) {
//        printk(KERN_EMERG "Woow! a NULL!\n");
        return NULL;
    }

    p = task_of(se);
    if (p -> enough) {
        u64 dead_time = rq_of(mycfs_rq)->clock_task - p->quit_time;
        if (dead_time >= (TOTAL - p->limit) * BASE) {
            p->enough = 0;
        }
        else
        {
/*            if (mycfs_rq->next) {
                se = mycfs_rq->next;
                p = task_of(se);
            }
            else*/
            {
//                printk(KERN_EMERG "at time %llu, task %d was tried but failed!\n", rq->clock_task, p->pid);
                mycfs_rq->curr = NULL;
                return NULL;
            }
        }
    }
    mycfs_rq->next = NULL;
    set_next_entity(mycfs_rq, se);
    
//    printk(KERN_EMERG "pick once! pid is %d, runtime is %llu, limit = %d\n", p->pid, se->sum_exec_runtime, p->limit);
    
//  printk(KERN_EMERG "at time %llu, task %d was started!\n", rq->clock_task, p->pid);
    return p;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
check_preempt_tick(struct mycfs_rq *mycfs_rq, struct sched_entity *curr)
{
    u64 ideal_runtime, delta_exec;
    struct sched_entity *se;
    s64 delta;
    
    ideal_runtime = sched_slice(mycfs_rq, curr);
    
//    printk(KERN_EMERG "pid is %d, ideal runtime is %llu, limit = %d\n", task_of(curr)->pid, ideal_runtime, task_of(curr)->limit);
    
    
    delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
//    ideal_runtime = RATIO * BASE;
    if (delta_exec > ideal_runtime) {
        resched_task(rq_of(mycfs_rq)->curr);
        /*
         * The current task ran long enough, ensure it doesn't get
         * re-elected due to buddy favours.
         */
        //cam        clear_buddies(mycfs_rq, curr);
        return;
    }
    
    /*
     * Ensure that a task that missed wakeup preemption by a
     * narrow margin doesn't have to wait for a full slice.
     * This also mitigates buddy induced latencies under load.
     */
    if (delta_exec < sysctl_sched_min_granularity)
        return;
    
    se = __pick_first_entity_mycfs(mycfs_rq);;
    if (se) {
        return NULL;
    }
    delta = curr->vruntime - se->vruntime;
    
    if (delta < 0)
        return;
    
    if (delta > ideal_runtime)
        resched_task(rq_of(mycfs_rq)->curr);
}

static void
entity_tick(struct mycfs_rq *mycfs_rq, struct sched_entity *curr, int queued)
{
    /*
     * Update run-time statistics of the 'current'.
     */
    update_curr(mycfs_rq);
    
/*    if (mycfs_rq->nr_running > 1)
        check_preempt_tick(mycfs_rq, curr);
    else*/
    {
        unsigned long ideal_runtime, delta_exec;
        struct sched_entity *se;
        s64 delta;
        
        delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
//        printk(KERN_EMERG "From entity_tick 1! current clock is %llu\n", rq_of(mycfs_rq)->clock_task);
        ideal_runtime = task_of(curr)->limit * BASE;
        if (delta_exec > ideal_runtime) {
            task_of(curr)->enough = 1;
            task_of(curr)->quit_time = rq_of(mycfs_rq)->clock_task;
//            printk(KERN_EMERG "at time %llu, task %d was kicked out!\n", rq_of(mycfs_rq)->clock_task, task_of(curr)->pid);
            resched_task(rq_of(mycfs_rq)->curr);
        }
    }
}

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_mycfs(struct rq *rq, struct task_struct *curr, int queued)
{
    struct mycfs_rq *mycfs_rq;
    struct sched_entity *se = &curr->se;
    
    for_each_sched_entity(se) {
        mycfs_rq = cfs_rq_of(se);
        entity_tick(mycfs_rq, se, queued);
    }
    
    //    update_rq_runnable_avg(rq, 1);
}

static void show_name_mycfs(struct rq *rq)
{
    printk(KERN_EMERG "this info from scheduler: mycfs!\n");
}

/*
 * Simple, special scheduling class for the per-CPU idle tasks:
 */
const struct sched_class mycfs_sched_class = {
    
    .show_name = show_name_mycfs,
    
    /* .next is NULL */
    /* no enqueue/yield_task for idle tasks */
    .next			= &idle_sched_class,
    .enqueue_task		= enqueue_task_mycfs,
    
    /* dequeue is not valid, we print a debug message there: */
    .dequeue_task		= dequeue_task_mycfs,
    
    .check_preempt_curr	= check_preempt_curr_mycfs,
    
    .pick_next_task		= pick_next_task_mycfs,
    .put_prev_task		= put_prev_task_mycfs,
    
    .set_curr_task          = set_curr_task_mycfs,
    .task_tick		= task_tick_mycfs,
    .task_fork		= task_fork_mycfs,
    
    .get_rr_interval	= get_rr_interval_mycfs,
    
    .prio_changed		= prio_changed_mycfs,
    .switched_to		= switched_to_mycfs,
};

void init_mycfs_rq(struct mycfs_rq *mycfs_rq)
{
    int i;
    mycfs_rq->tasks_timeline = RB_ROOT;
    mycfs_rq->h_nr_running = 0;
    for (i = 0; i < MAX_JOBS_IN_MYCFS; i ++) {
        mycfs_rq->jobs[i] = NULL;
    }
    mycfs_rq->min_vruntime = (u64)(-(1LL << 20));
}

void check_task_mycfs(struct rq *rq)
{
    int i;
    struct mycfs_rq *mycfs_rq = &rq->mycfs;
   
    struct sched_entity *entry;
    struct rb_node *next;
    
    next = mycfs_rq->rb_leftmost;
    if (!next) {
        return;
    }
    entry = rb_entry(next, struct sched_entity, run_node);
    if (task_of(entry)->enough) {
        u64 dead_time = rq->clock_task - task_of(entry)->quit_time;
        if (dead_time >= (TOTAL - task_of(entry)->limit) * BASE) {
            task_of(entry)->enough = 0;
            if (mycfs_rq->curr == NULL) {
                resched_task(rq->curr);
            }
        }
    }
    next = rb_next(&entry->run_node);
    
    while(next)
    {
        entry = rb_entry(next, struct sched_entity, run_node);
        if (task_of(entry)->enough) {
            u64 dead_time = rq->clock_task - task_of(entry)->quit_time;
            if (dead_time >= (TOTAL - task_of(entry)->limit) * BASE) {
                task_of(entry)->enough = 0;
            }
        }
        next = rb_next(&entry->run_node);
    }
}
